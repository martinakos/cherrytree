/*
 * ct_history_storage.h
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

#pragma once

#include "ct_types.h"
#include "ct_filesystem.h"
#include <sqlite3.h>
#include <memory>

class CtHistoryStorage
{
public:
    static std::unique_ptr<CtHistoryStorage> open(const fs::path& docPath, CtDocType type);
    static fs::path get_history_file_path(const fs::path& docPath, CtDocType type);

    ~CtHistoryStorage();

    void writeEntries(const std::vector<CtHistoryEntry>& entries);
    std::vector<CtHistoryEntry> readEntries();
    void migrateFrom(const std::vector<CtHistoryEntry>& oldEntries);
    void close();

private:
    CtHistoryStorage() = default;
    bool _open_db(const fs::path& dbPath);
    void _create_table();

    sqlite3* _pDb{nullptr};
    fs::path _dbPath;
};
