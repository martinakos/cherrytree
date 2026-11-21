/*
 * ct_timeline_storage.cc
 *
 * Copyright 2009-2025
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_timeline_storage.h"
#include "ct_logging.h"

static const char TABLE_CREATE[]{
    "CREATE TABLE IF NOT EXISTS timeline ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "node_id INTEGER NOT NULL,"
    "timestamp INTEGER NOT NULL,"
    "use_day INTEGER DEFAULT 0,"
    "cursor_pos INTEGER DEFAULT 0,"
    "scroll_pos INTEGER DEFAULT 0,"
    "action_description TEXT DEFAULT '',"
    "action_type TEXT DEFAULT '',"
    "region_offset INTEGER DEFAULT -1,"
    "region_length INTEGER DEFAULT 0,"
    "canvas_idx INTEGER DEFAULT -1,"
    "delta_data BLOB"
    ")"
};

static const char TABLE_INSERT[]{
    "INSERT INTO timeline("
    "node_id,timestamp,use_day,cursor_pos,scroll_pos,"
    "action_description,action_type,region_offset,region_length,"
    "canvas_idx,delta_data) VALUES(?,?,?,?,?,?,?,?,?,?,?)"
};

static const char TABLE_DELETE[]{"DELETE FROM timeline"};

static const char TABLE_SELECT[]{
    "SELECT node_id, timestamp, use_day, cursor_pos, scroll_pos,"
    " action_description, action_type, region_offset, region_length,"
    " canvas_idx, delta_data"
    " FROM timeline ORDER BY id ASC"
};

/*static*/fs::path CtTimelineStorage::get_timeline_file_path(const fs::path& docPath, CtDocType type)
{
    if (CtDocType::MultiFile == type) {
        return docPath / "timeline.db";
    }
    return fs::path{docPath.string() + ".timeline"};
}

/*static*/std::unique_ptr<CtTimelineStorage> CtTimelineStorage::open(const fs::path& docPath, CtDocType type)
{
    auto storage = std::unique_ptr<CtTimelineStorage>(new CtTimelineStorage());
    fs::path dbPath = get_timeline_file_path(docPath, type);
    if (!storage->_open_db(dbPath)) {
        return nullptr;
    }
    storage->_create_table();
    return storage;
}

CtTimelineStorage::~CtTimelineStorage()
{
    close();
}

bool CtTimelineStorage::_open_db(const fs::path& dbPath)
{
    _dbPath = dbPath;
    int rc = sqlite3_open(dbPath.string().c_str(), &_pDb);
    if (rc != SQLITE_OK) {
        spdlog::error("CtTimelineStorage: cannot open {}: {}", dbPath.string(),
                      _pDb ? sqlite3_errmsg(_pDb) : "allocation failed");
        if (_pDb) { sqlite3_close(_pDb); _pDb = nullptr; }
        return false;
    }
    return true;
}

void CtTimelineStorage::_create_table()
{
    if (!_pDb) return;
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_pDb, TABLE_CREATE, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::error("CtTimelineStorage: create table failed: {}", errMsg ? errMsg : "unknown");
        if (errMsg) sqlite3_free(errMsg);
    }
}

void CtTimelineStorage::writeEntries(const std::vector<CtTimelineEntry>& entries)
{
    if (!_pDb) return;

    char* errMsg = nullptr;
    sqlite3_exec(_pDb, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); errMsg = nullptr; }

    sqlite3_exec(_pDb, TABLE_DELETE, nullptr, nullptr, &errMsg);
    if (errMsg) { sqlite3_free(errMsg); errMsg = nullptr; }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_pDb, TABLE_INSERT, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("CtTimelineStorage: prepare insert failed: {}", sqlite3_errmsg(_pDb));
        sqlite3_exec(_pDb, "ROLLBACK", nullptr, nullptr, nullptr);
        return;
    }

    for (const auto& e : entries) {
        sqlite3_bind_int64(stmt, 1, e.nodeId);
        sqlite3_bind_int64(stmt, 2, e.timestamp);
        sqlite3_bind_int(stmt, 3, e.useDay);
        sqlite3_bind_int(stmt, 4, e.cursorPos);
        sqlite3_bind_int(stmt, 5, e.scrollPos);
        sqlite3_bind_text(stmt, 6, e.actionDescription.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, e.actionType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, e.regionOffset);
        sqlite3_bind_int(stmt, 9, e.regionLength);
        sqlite3_bind_int(stmt, 10, e.canvasIdx);
        if (!e.deltaData.empty()) {
            sqlite3_bind_blob(stmt, 11, e.deltaData.data(), static_cast<int>(e.deltaData.size()), SQLITE_TRANSIENT);
        }
        else {
            sqlite3_bind_null(stmt, 11);
        }
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            spdlog::error("CtTimelineStorage: insert step failed: {}", sqlite3_errmsg(_pDb));
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    sqlite3_exec(_pDb, "COMMIT", nullptr, nullptr, &errMsg);
    if (errMsg) sqlite3_free(errMsg);
}

std::vector<CtTimelineEntry> CtTimelineStorage::readEntries()
{
    std::vector<CtTimelineEntry> entries;
    if (!_pDb) return entries;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_pDb, TABLE_SELECT, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("CtTimelineStorage: prepare select failed: {}", sqlite3_errmsg(_pDb));
        return entries;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CtTimelineEntry e;
        e.nodeId = sqlite3_column_int64(stmt, 0);
        e.timestamp = sqlite3_column_int64(stmt, 1);
        e.useDay = sqlite3_column_int(stmt, 2);
        e.cursorPos = sqlite3_column_int(stmt, 3);
        e.scrollPos = sqlite3_column_int(stmt, 4);
        auto col5 = sqlite3_column_text(stmt, 5);
        if (col5) e.actionDescription = reinterpret_cast<const char*>(col5);
        auto col6 = sqlite3_column_text(stmt, 6);
        if (col6) e.actionType = reinterpret_cast<const char*>(col6);
        e.regionOffset = sqlite3_column_int(stmt, 7);
        e.regionLength = sqlite3_column_int(stmt, 8);
        e.canvasIdx = sqlite3_column_int(stmt, 9);
        const void* blob = sqlite3_column_blob(stmt, 10);
        int blobSize = sqlite3_column_bytes(stmt, 10);
        if (blob && blobSize > 0) {
            e.deltaData.assign(static_cast<const char*>(blob), blobSize);
        }
        entries.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return entries;
}

void CtTimelineStorage::close()
{
    if (_pDb) {
        sqlite3_close(_pDb);
        _pDb = nullptr;
    }
}
