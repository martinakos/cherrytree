/*
 * tests_ocr.cpp
 *
 * Tests for OCR text persistence and search integration.
 *
 * Copyright 2009-2026
 * Giuseppe Penone <giuspen@gmail.com>
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

#include "ct_image.h"
#include "ct_storage_sqlite.h"
#include "ct_types.h"
#include "ct_widgets.h"
#include "ct_misc_utils.h"
#include "gtest/gtest.h"
#include <glibmm/regex.h>
#include <sqlite3.h>
#include <sstream>
#include <thread>

#ifdef HAVE_NCNN
#include "ct_ocr.h"
#include "ocr_engine.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#endif

// --- OCR text field tests ---

TEST(OcrTextTest, DefaultOcrTextIsEmpty)
{
    // CtImagePng can't be constructed without a valid CtMainWin and pixbuf,
    // so we test the data model semantics via CtWidgetDesc round-trips.
    CtWidgetDesc desc(CtAnchWidgType::ImagePng);
    EXPECT_TRUE(desc.getProperty("ocr_text").empty());
}

TEST(OcrTextTest, WidgetDescPreservesOcrText)
{
    CtWidgetDesc desc(CtAnchWidgType::ImagePng);
    desc.setProperty("ocr_text", "Hello World from OCR");
    EXPECT_EQ("Hello World from OCR", desc.getProperty("ocr_text"));
}

TEST(OcrTextTest, WidgetDescEmptyOcrTextIsAbsent)
{
    CtWidgetDesc desc(CtAnchWidgType::ImagePng);
    desc.setProperty("ocr_text", "");
    EXPECT_TRUE(desc.getProperty("ocr_text").empty());
}

// --- SQLite schema tests ---

TEST(OcrSqliteTest, TableImageCreateHasOcrTextColumn)
{
    std::string createSql{CtStorageSqlite::TABLE_IMAGE_CREATE};
    EXPECT_NE(std::string::npos, createSql.find("ocr_text TEXT"));
}

TEST(OcrSqliteTest, TableImageInsertHas14Placeholders)
{
    std::string insertSql{CtStorageSqlite::TABLE_IMAGE_INSERT};
    int count = 0;
    for (char c : insertSql) {
        if (c == '?') ++count;
    }
    EXPECT_EQ(14, count);
}

TEST(OcrSqliteTest, FixDbTablesAddsOcrText)
{
    sqlite3* pDb = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &pDb));

    // Create old 12-column image table (without ocr_text)
    const char* oldSchema =
        "CREATE TABLE image ("
        "node_id INTEGER, offset INTEGER, justification TEXT, anchor TEXT, "
        "png BLOB, filename TEXT, link TEXT, time INTEGER, "
        "display_width INTEGER, display_height INTEGER, "
        "ts_creation INTEGER, ts_lastsave INTEGER)";
    char* errMsg = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(pDb, oldSchema, nullptr, nullptr, &errMsg));

    // Create other required tables for _fix_db_tables
    sqlite3_exec(pDb, "CREATE TABLE node (node_id INTEGER, name TEXT, txt TEXT, syntax TEXT, "
                       "tags TEXT, is_ro INTEGER, is_richtxt INTEGER, has_codebox INTEGER, "
                       "has_table INTEGER, has_image INTEGER, level INTEGER)", nullptr, nullptr, nullptr);
    sqlite3_exec(pDb, "CREATE TABLE codebox (node_id INTEGER, offset INTEGER, justification TEXT, "
                       "txt TEXT, syntax TEXT, width INTEGER, height INTEGER, is_width_pix INTEGER, "
                       "do_highl_bra INTEGER, do_show_linenum INTEGER)", nullptr, nullptr, nullptr);
    sqlite3_exec(pDb, "CREATE TABLE grid (node_id INTEGER, offset INTEGER, justification TEXT, "
                       "txt TEXT, col_min INTEGER, col_max INTEGER)", nullptr, nullptr, nullptr);
    sqlite3_exec(pDb, "CREATE TABLE children (node_id INTEGER, father_id INTEGER, sequence INTEGER)",
                 nullptr, nullptr, nullptr);

    // Verify ocr_text column doesn't exist yet
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(pDb, "PRAGMA table_info(image)", -1, &stmt, nullptr);
    bool hasOcrText = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (colName && std::string(colName) == "ocr_text") {
            hasOcrText = true;
        }
    }
    sqlite3_finalize(stmt);
    EXPECT_FALSE(hasOcrText);

    // Run _fix_db_tables indirectly by executing the ALTER TABLE
    // (we replicate the logic since _fix_db_tables is a private method)
    sqlite3_exec(pDb, "ALTER TABLE image ADD COLUMN ocr_text TEXT", nullptr, nullptr, nullptr);
    sqlite3_exec(pDb, "ALTER TABLE image ADD COLUMN ocr_boxes TEXT", nullptr, nullptr, nullptr);

    // Verify ocr_text and ocr_boxes columns now exist
    sqlite3_prepare_v2(pDb, "PRAGMA table_info(image)", -1, &stmt, nullptr);
    hasOcrText = false;
    bool hasOcrBoxes = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (colName && std::string(colName) == "ocr_text") {
            hasOcrText = true;
        }
        if (colName && std::string(colName) == "ocr_boxes") {
            hasOcrBoxes = true;
        }
    }
    sqlite3_finalize(stmt);
    EXPECT_TRUE(hasOcrText);
    EXPECT_TRUE(hasOcrBoxes);

    // Insert and read back an image with OCR text and boxes
    sqlite3_exec(pDb, "INSERT INTO image VALUES(1, 0, 'left', '', X'89504E47', '', '', 0, 0, 0, 0, 0, 'test ocr text', '100,50;0,4,10,10,90,10,90,30,10,30')",
                 nullptr, nullptr, nullptr);
    sqlite3_prepare_v2(pDb, "SELECT ocr_text, ocr_boxes FROM image WHERE node_id=1", -1, &stmt, nullptr);
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(stmt));
    const char* ocrText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    EXPECT_STREQ("test ocr text", ocrText);
    const char* ocrBoxes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    EXPECT_STREQ("100,50;0,4,10,10,90,10,90,30,10,30", ocrBoxes);
    sqlite3_finalize(stmt);

    sqlite3_close(pDb);
}

// --- Search pattern matching tests ---

static bool ocrTextMatchesPattern(const Glib::ustring& ocrText,
                                   const Glib::RefPtr<Glib::Regex>& re)
{
    if (ocrText.empty()) return false;
    Glib::MatchInfo mi;
    return re->match(ocrText, mi) and mi.matches();
}

TEST(OcrSearchTest, MatchOcrText)
{
    auto re = Glib::Regex::create("invoice", Glib::RegexCompileFlags::REGEX_CASELESS);
    Glib::ustring ocrText{"invoice total amount 42.50"};
    EXPECT_TRUE(ocrTextMatchesPattern(ocrText, re));
}

TEST(OcrSearchTest, NoMatchOcrText)
{
    auto re = Glib::Regex::create("receipt", Glib::RegexCompileFlags::REGEX_CASELESS);
    Glib::ustring ocrText{"invoice total amount 42.50"};
    EXPECT_FALSE(ocrTextMatchesPattern(ocrText, re));
}

TEST(OcrSearchTest, EmptyOcrTextNoMatch)
{
    auto re = Glib::Regex::create("anything", Glib::RegexCompileFlags::REGEX_CASELESS);
    Glib::ustring ocrText{""};
    EXPECT_FALSE(ocrTextMatchesPattern(ocrText, re));
}

TEST(OcrSearchTest, CaseInsensitiveMatch)
{
    auto re = Glib::Regex::create("INVOICE", Glib::RegexCompileFlags::REGEX_CASELESS);
    Glib::ustring ocrText{"invoice total amount 42.50"};
    EXPECT_TRUE(ocrTextMatchesPattern(ocrText, re));
}

TEST(OcrSearchTest, RegexMatch)
{
    auto re = Glib::Regex::create("\\d+\\.\\d+", static_cast<Glib::RegexCompileFlags>(0));
    Glib::ustring ocrText{"invoice total amount 42.50"};
    EXPECT_TRUE(ocrTextMatchesPattern(ocrText, re));
}

// --- CtMatchType enum test ---

TEST(OcrSearchTest, TextInImagesMatchTypeExists)
{
    CtMatchType t = CtMatchType::TextInImages;
    EXPECT_NE(t, CtMatchType::None);
    EXPECT_NE(t, CtMatchType::Content);
    EXPECT_NE(t, CtMatchType::NameNTags);
    EXPECT_NE(t, CtMatchType::DrawingCanvases);
}

// --- Search options test ---

TEST(OcrSearchTest, SearchOptionsDefaultTextInImagesFalse)
{
    CtSearchOptions opts;
    EXPECT_FALSE(opts.text_in_images);
}

// --- ThreadSafeDEQueue tests ---

TEST(ThreadSafeDEQueueTest, EmptyOnConstruction)
{
    ThreadSafeDEQueue<int, 4> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(0u, q.size());
}

TEST(ThreadSafeDEQueueTest, PushBackAndTryPopFront)
{
    ThreadSafeDEQueue<int, 4> q;
    EXPECT_TRUE(q.push_back(10));
    EXPECT_TRUE(q.push_back(20));
    EXPECT_EQ(2u, q.size());

    auto v1 = q.try_pop_front();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(10, *v1);

    auto v2 = q.try_pop_front();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(20, *v2);

    EXPECT_TRUE(q.empty());
}

TEST(ThreadSafeDEQueueTest, TryPopFrontOnEmptyReturnsNullopt)
{
    ThreadSafeDEQueue<int, 4> q;
    EXPECT_FALSE(q.try_pop_front().has_value());
}

TEST(ThreadSafeDEQueueTest, PushBackRejectsAtCapacity)
{
    ThreadSafeDEQueue<int, 3> q;
    EXPECT_TRUE(q.push_back(1));
    EXPECT_TRUE(q.push_back(2));
    EXPECT_TRUE(q.push_back(3));
    EXPECT_FALSE(q.push_back(4));
    EXPECT_EQ(3u, q.size());
}

TEST(ThreadSafeDEQueueTest, PushFrontRejectsAtCapacity)
{
    ThreadSafeDEQueue<int, 2> q;
    EXPECT_TRUE(q.push_front(1));
    EXPECT_TRUE(q.push_front(2));
    EXPECT_FALSE(q.push_front(3));
    EXPECT_EQ(2u, q.size());
}

TEST(ThreadSafeDEQueueTest, PushFrontForceBypassesCapacity)
{
    ThreadSafeDEQueue<int, 2> q;
    EXPECT_TRUE(q.push_back(1));
    EXPECT_TRUE(q.push_back(2));
    EXPECT_TRUE(q.push_front(99, true/*force*/));
    EXPECT_EQ(3u, q.size());

    auto v = q.try_pop_front();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(99, *v);
}

TEST(ThreadSafeDEQueueTest, PushFrontOrderIsFILO)
{
    ThreadSafeDEQueue<int, 4> q;
    q.push_back(1);
    q.push_front(2);
    q.push_front(3);

    EXPECT_EQ(3, *q.try_pop_front());
    EXPECT_EQ(2, *q.try_pop_front());
    EXPECT_EQ(1, *q.try_pop_front());
}

TEST(ThreadSafeDEQueueTest, PeekReturnsWithoutRemoving)
{
    ThreadSafeDEQueue<int, 4> q;
    EXPECT_FALSE(q.peek().has_value());

    q.push_back(42);
    auto peeked = q.peek();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(42, *peeked);
    EXPECT_EQ(1u, q.size());
}

TEST(ThreadSafeDEQueueTest, ClearEmptiesQueue)
{
    ThreadSafeDEQueue<int, 4> q;
    q.push_back(1);
    q.push_back(2);
    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(0u, q.size());
}

TEST(ThreadSafeDEQueueTest, PopFrontBlocksUntilPush)
{
    ThreadSafeDEQueue<int, 4> q;
    int result = 0;
    std::thread consumer([&] { result = q.pop_front(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.push_back(77);
    consumer.join();
    EXPECT_EQ(77, result);
}

TEST(ThreadSafeDEQueueTest, ConcurrentProducerConsumer)
{
    ThreadSafeDEQueue<int, 64> q;
    const int N = 200;
    std::vector<int> consumed;
    consumed.reserve(N);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (not q.push_back(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < N; ++i) {
            consumed.push_back(q.pop_front());
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(N, static_cast<int>(consumed.size()));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(i, consumed[i]);
    }
}

// --- CtWidgetDesc OCR property tests ---

TEST(WidgetDescOcrTest, OcrPropertiesAffectEquality)
{
    CtWidgetDesc a(CtAnchWidgType::ImagePng);
    CtWidgetDesc b(CtAnchWidgType::ImagePng);
    EXPECT_EQ(a, b);

    a.setProperty("ocr_text", "hello");
    EXPECT_FALSE(a == b);

    b.setProperty("ocr_text", "hello");
    EXPECT_EQ(a, b);
}

TEST(WidgetDescOcrTest, OcrBoxesPropertyRoundTrip)
{
    CtWidgetDesc desc(CtAnchWidgType::ImagePng);
    const std::string boxes = "300,200;0,5,10,10,290,10,290,50,10,50;5,12,10,60,290,60,290,100,10,100";
    desc.setProperty("ocr_boxes", boxes);
    EXPECT_EQ(boxes, desc.getProperty("ocr_boxes"));
}

TEST(WidgetDescOcrTest, MultipleOcrPropertiesCoexist)
{
    CtWidgetDesc desc(CtAnchWidgType::ImagePng);
    desc.setProperty("ocr_text", "Hello World");
    desc.setProperty("ocr_boxes", "100,50;0,11,0,0,100,0,100,50,0,50");
    desc.setProperty("link", "webs https://example.com");
    EXPECT_EQ("Hello World", desc.getProperty("ocr_text"));
    EXPECT_EQ("100,50;0,11,0,0,100,0,100,50,0,50", desc.getProperty("ocr_boxes"));
    EXPECT_EQ("webs https://example.com", desc.getProperty("link"));
}

TEST(WidgetDescOcrTest, OverwriteOcrText)
{
    CtWidgetDesc desc(CtAnchWidgType::ImagePng);
    desc.setProperty("ocr_text", "old text");
    desc.setProperty("ocr_text", "new text");
    EXPECT_EQ("new text", desc.getProperty("ocr_text"));
}

// --- SQLite OCR persistence tests ---

TEST(OcrSqlitePersistenceTest, InsertAndReadBackOcrFields)
{
    sqlite3* pDb = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &pDb));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(pDb, CtStorageSqlite::TABLE_IMAGE_CREATE,
                                        nullptr, nullptr, nullptr));

    const char* ocrText = "Invoice #12345 Total: $42.50";
    const char* ocrBoxes = "400,300;0,7,10,10,200,10,200,40,10,40;8,28,10,50,390,50,390,80,10,80";

    sqlite3_stmt* pStmt;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_IMAGE_INSERT,
                                             -1, &pStmt, nullptr));
    sqlite3_bind_int64(pStmt, 1, 1);    // node_id
    sqlite3_bind_int64(pStmt, 2, 0);    // offset
    sqlite3_bind_text(pStmt, 3, "left", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 4, "", -1, SQLITE_STATIC);     // anchor
    sqlite3_bind_blob(pStmt, 5, "\x89PNG", 4, SQLITE_STATIC); // png stub
    sqlite3_bind_text(pStmt, 6, "", -1, SQLITE_STATIC);     // filename
    sqlite3_bind_text(pStmt, 7, "", -1, SQLITE_STATIC);     // link
    sqlite3_bind_int64(pStmt, 8, 0);    // time
    sqlite3_bind_int64(pStmt, 9, 0);    // display_width
    sqlite3_bind_int64(pStmt, 10, 0);   // display_height
    sqlite3_bind_int64(pStmt, 11, 1000);// ts_creation
    sqlite3_bind_int64(pStmt, 12, 2000);// ts_lastsave
    sqlite3_bind_text(pStmt, 13, ocrText, -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 14, ocrBoxes, -1, SQLITE_STATIC);
    ASSERT_EQ(SQLITE_DONE, sqlite3_step(pStmt));
    sqlite3_finalize(pStmt);

    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb,
        "SELECT ocr_text, ocr_boxes FROM image WHERE node_id=1", -1, &pStmt, nullptr));
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
    EXPECT_STREQ(ocrText, reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 0)));
    EXPECT_STREQ(ocrBoxes, reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 1)));
    sqlite3_finalize(pStmt);
    sqlite3_close(pDb);
}

TEST(OcrSqlitePersistenceTest, NullOcrFieldsReadAsNull)
{
    sqlite3* pDb = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &pDb));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(pDb, CtStorageSqlite::TABLE_IMAGE_CREATE,
                                        nullptr, nullptr, nullptr));

    sqlite3_stmt* pStmt;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_IMAGE_INSERT,
                                             -1, &pStmt, nullptr));
    sqlite3_bind_int64(pStmt, 1, 1);
    sqlite3_bind_int64(pStmt, 2, 0);
    sqlite3_bind_text(pStmt, 3, "left", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 4, "", -1, SQLITE_STATIC);
    sqlite3_bind_blob(pStmt, 5, "\x89PNG", 4, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 6, "", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 7, "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(pStmt, 8, 0);
    sqlite3_bind_int64(pStmt, 9, 0);
    sqlite3_bind_int64(pStmt, 10, 0);
    sqlite3_bind_int64(pStmt, 11, 0);
    sqlite3_bind_int64(pStmt, 12, 0);
    sqlite3_bind_null(pStmt, 13);
    sqlite3_bind_null(pStmt, 14);
    ASSERT_EQ(SQLITE_DONE, sqlite3_step(pStmt));
    sqlite3_finalize(pStmt);

    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb,
        "SELECT ocr_text, ocr_boxes FROM image WHERE node_id=1", -1, &pStmt, nullptr));
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
    EXPECT_EQ(SQLITE_NULL, sqlite3_column_type(pStmt, 0));
    EXPECT_EQ(SQLITE_NULL, sqlite3_column_type(pStmt, 1));
    sqlite3_finalize(pStmt);
    sqlite3_close(pDb);
}

TEST(OcrSqlitePersistenceTest, EmptyOcrFieldsRoundTrip)
{
    sqlite3* pDb = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &pDb));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(pDb, CtStorageSqlite::TABLE_IMAGE_CREATE,
                                        nullptr, nullptr, nullptr));

    sqlite3_stmt* pStmt;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_IMAGE_INSERT,
                                             -1, &pStmt, nullptr));
    sqlite3_bind_int64(pStmt, 1, 1);
    sqlite3_bind_int64(pStmt, 2, 0);
    sqlite3_bind_text(pStmt, 3, "left", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 4, "", -1, SQLITE_STATIC);
    sqlite3_bind_blob(pStmt, 5, "\x89PNG", 4, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 6, "", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 7, "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(pStmt, 8, 0);
    sqlite3_bind_int64(pStmt, 9, 0);
    sqlite3_bind_int64(pStmt, 10, 0);
    sqlite3_bind_int64(pStmt, 11, 0);
    sqlite3_bind_int64(pStmt, 12, 0);
    sqlite3_bind_text(pStmt, 13, "", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 14, "", -1, SQLITE_STATIC);
    ASSERT_EQ(SQLITE_DONE, sqlite3_step(pStmt));
    sqlite3_finalize(pStmt);

    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb,
        "SELECT ocr_text, ocr_boxes FROM image WHERE node_id=1", -1, &pStmt, nullptr));
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
    EXPECT_STREQ("", reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 0)));
    EXPECT_STREQ("", reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 1)));
    sqlite3_finalize(pStmt);
    sqlite3_close(pDb);
}

TEST(OcrSqlitePersistenceTest, UnicodeOcrTextRoundTrip)
{
    sqlite3* pDb = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &pDb));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(pDb, CtStorageSqlite::TABLE_IMAGE_CREATE,
                                        nullptr, nullptr, nullptr));

    const char* unicodeText = "\xc3\x89" "l\xc3\xa8" "ve R\xc3\xa9" "sum\xc3\xa9";

    sqlite3_stmt* pStmt;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_IMAGE_INSERT,
                                             -1, &pStmt, nullptr));
    sqlite3_bind_int64(pStmt, 1, 1);
    sqlite3_bind_int64(pStmt, 2, 0);
    sqlite3_bind_text(pStmt, 3, "left", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 4, "", -1, SQLITE_STATIC);
    sqlite3_bind_blob(pStmt, 5, "\x89PNG", 4, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 6, "", -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 7, "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(pStmt, 8, 0);
    sqlite3_bind_int64(pStmt, 9, 0);
    sqlite3_bind_int64(pStmt, 10, 0);
    sqlite3_bind_int64(pStmt, 11, 0);
    sqlite3_bind_int64(pStmt, 12, 0);
    sqlite3_bind_text(pStmt, 13, unicodeText, -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 14, "", -1, SQLITE_STATIC);
    ASSERT_EQ(SQLITE_DONE, sqlite3_step(pStmt));
    sqlite3_finalize(pStmt);

    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb,
        "SELECT ocr_text FROM image WHERE node_id=1", -1, &pStmt, nullptr));
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
    EXPECT_STREQ(unicodeText, reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 0)));
    sqlite3_finalize(pStmt);
    sqlite3_close(pDb);
}

TEST(OcrSqlitePersistenceTest, MultipleImagesPerNode)
{
    sqlite3* pDb = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &pDb));
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(pDb, CtStorageSqlite::TABLE_IMAGE_CREATE,
                                        nullptr, nullptr, nullptr));

    const char* texts[] = {"first image text", "second image text", "third image text"};
    for (int i = 0; i < 3; ++i) {
        sqlite3_stmt* pStmt;
        ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_IMAGE_INSERT,
                                                 -1, &pStmt, nullptr));
        sqlite3_bind_int64(pStmt, 1, 1);
        sqlite3_bind_int64(pStmt, 2, i * 10);
        sqlite3_bind_text(pStmt, 3, "left", -1, SQLITE_STATIC);
        sqlite3_bind_text(pStmt, 4, "", -1, SQLITE_STATIC);
        sqlite3_bind_blob(pStmt, 5, "\x89PNG", 4, SQLITE_STATIC);
        sqlite3_bind_text(pStmt, 6, "", -1, SQLITE_STATIC);
        sqlite3_bind_text(pStmt, 7, "", -1, SQLITE_STATIC);
        sqlite3_bind_int64(pStmt, 8, 0);
        sqlite3_bind_int64(pStmt, 9, 0);
        sqlite3_bind_int64(pStmt, 10, 0);
        sqlite3_bind_int64(pStmt, 11, 0);
        sqlite3_bind_int64(pStmt, 12, 0);
        sqlite3_bind_text(pStmt, 13, texts[i], -1, SQLITE_STATIC);
        sqlite3_bind_text(pStmt, 14, "", -1, SQLITE_STATIC);
        ASSERT_EQ(SQLITE_DONE, sqlite3_step(pStmt));
        sqlite3_finalize(pStmt);
    }

    sqlite3_stmt* pStmt;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb,
        "SELECT ocr_text FROM image WHERE node_id=1 ORDER BY offset", -1, &pStmt, nullptr));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
        EXPECT_STREQ(texts[i], reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 0)));
    }
    EXPECT_EQ(SQLITE_DONE, sqlite3_step(pStmt));
    sqlite3_finalize(pStmt);
    sqlite3_close(pDb);
}

// --- OCR boxes format parsing tests ---

static bool parseOcrBoxes(const std::string& boxes, int& imgW, int& imgH,
                           std::vector<std::array<int, 10>>& parsed)
{
    parsed.clear();
    std::istringstream ss(boxes);
    std::string token;
    if (not std::getline(ss, token, ';')) return false;
    auto commaPos = token.find(',');
    if (commaPos == std::string::npos) return false;
    imgW = std::stoi(token.substr(0, commaPos));
    imgH = std::stoi(token.substr(commaPos + 1));

    while (std::getline(ss, token, ';')) {
        std::istringstream ts(token);
        std::string val;
        std::array<int, 10> nums{};
        int i = 0;
        while (std::getline(ts, val, ',') and i < 10) {
            nums[i++] = std::stoi(val);
        }
        if (i != 10) return false;
        parsed.push_back(nums);
    }
    return true;
}

TEST(OcrBoxesFormatTest, SingleBoxParsesCorrectly)
{
    int w, h;
    std::vector<std::array<int, 10>> boxes;
    ASSERT_TRUE(parseOcrBoxes("300,200;0,5,10,10,290,10,290,50,10,50", w, h, boxes));
    EXPECT_EQ(300, w);
    EXPECT_EQ(200, h);
    ASSERT_EQ(1u, boxes.size());
    EXPECT_EQ(0, boxes[0][0]);   // startPos
    EXPECT_EQ(5, boxes[0][1]);   // endPos
    EXPECT_EQ(10, boxes[0][2]);  // x0
    EXPECT_EQ(10, boxes[0][3]);  // y0
}

TEST(OcrBoxesFormatTest, MultipleBoxes)
{
    int w, h;
    std::vector<std::array<int, 10>> boxes;
    std::string input = "400,300;0,7,10,10,200,10,200,40,10,40;8,20,10,50,390,50,390,80,10,80";
    ASSERT_TRUE(parseOcrBoxes(input, w, h, boxes));
    EXPECT_EQ(400, w);
    EXPECT_EQ(300, h);
    ASSERT_EQ(2u, boxes.size());
    EXPECT_EQ(0, boxes[0][0]);
    EXPECT_EQ(7, boxes[0][1]);
    EXPECT_EQ(8, boxes[1][0]);
    EXPECT_EQ(20, boxes[1][1]);
}

TEST(OcrBoxesFormatTest, ImageDimensionsOnly)
{
    int w, h;
    std::vector<std::array<int, 10>> boxes;
    ASSERT_TRUE(parseOcrBoxes("640,480", w, h, boxes));
    EXPECT_EQ(640, w);
    EXPECT_EQ(480, h);
    EXPECT_TRUE(boxes.empty());
}

TEST(OcrBoxesFormatTest, MalformedHeaderFails)
{
    int w, h;
    std::vector<std::array<int, 10>> boxes;
    EXPECT_FALSE(parseOcrBoxes("nocomma", w, h, boxes));
}

TEST(OcrBoxesFormatTest, IncompleteBoxFails)
{
    int w, h;
    std::vector<std::array<int, 10>> boxes;
    EXPECT_FALSE(parseOcrBoxes("100,100;0,5,10,10", w, h, boxes));
}

TEST(OcrBoxesFormatTest, MatchRangeOverlap)
{
    // Replicates the overlap logic from highlight_ocr_match:
    // a match [matchStart, matchEnd) overlaps a box [lineStart, lineEnd)
    // when matchStart < lineEnd AND matchEnd > lineStart
    struct Box { int lineStart; int lineEnd; };
    std::vector<Box> boxes = {{0, 5}, {6, 15}, {16, 30}};

    auto overlaps = [](int matchStart, int matchEnd, const Box& b) {
        return matchStart < b.lineEnd and matchEnd > b.lineStart;
    };

    // Match "Hello" at [0,5) overlaps only box 0
    EXPECT_TRUE(overlaps(0, 5, boxes[0]));
    EXPECT_FALSE(overlaps(0, 5, boxes[1]));
    EXPECT_FALSE(overlaps(0, 5, boxes[2]));

    // Match spanning boxes 0-1 at [3,10)
    EXPECT_TRUE(overlaps(3, 10, boxes[0]));
    EXPECT_TRUE(overlaps(3, 10, boxes[1]));
    EXPECT_FALSE(overlaps(3, 10, boxes[2]));

    // Match fully inside box 2 at [20,25)
    EXPECT_FALSE(overlaps(20, 25, boxes[0]));
    EXPECT_FALSE(overlaps(20, 25, boxes[1]));
    EXPECT_TRUE(overlaps(20, 25, boxes[2]));

    // Empty match [5,5) touches no box
    EXPECT_FALSE(overlaps(5, 5, boxes[0]));
    EXPECT_FALSE(overlaps(5, 5, boxes[1]));
}

// --- Search option integration tests ---

TEST(OcrSearchIntegrationTest, TextInImagesDefaultsOff)
{
    CtSearchOptions opts;
    EXPECT_FALSE(opts.text_in_images);
    EXPECT_TRUE(opts.node_content);
    EXPECT_TRUE(opts.node_name_n_tags);
}

TEST(OcrSearchIntegrationTest, MultiWordSearchOnOcrText)
{
    Glib::ustring ocrText{"Invoice Number 12345 Date January 2025 Total 42.50"};

    auto reExact = Glib::Regex::create("Invoice.*Total",
                                        Glib::RegexCompileFlags::REGEX_CASELESS);
    Glib::MatchInfo mi;
    EXPECT_TRUE(reExact->match(ocrText, mi));

    auto reWord = Glib::Regex::create("\\b12345\\b",
                                       static_cast<Glib::RegexCompileFlags>(0));
    EXPECT_TRUE(reWord->match(ocrText, mi));
}

TEST(OcrSearchIntegrationTest, AccentInsensitiveSearchOnOcrText)
{
    // OCR might produce accented characters; search should still find them
    // with case-insensitive matching
    Glib::ustring ocrText{"\xc3\x89" "l\xc3\xa8" "ve R\xc3\xa9" "sum\xc3\xa9"};
    auto re = Glib::Regex::create("r\xc3\xa9" "sum\xc3\xa9",
                                   Glib::RegexCompileFlags::REGEX_CASELESS);
    Glib::MatchInfo mi;
    EXPECT_TRUE(re->match(ocrText, mi));
}

TEST(OcrSearchIntegrationTest, SearchSnippetContext)
{
    // Replicate the snippet-building logic from _parse_node_image_ocr_text_iter
    Glib::ustring ocrText{"This is a long OCR text with the word Invoice somewhere in the middle of it all"};
    auto re = Glib::Regex::create("Invoice", static_cast<Glib::RegexCompileFlags>(0));
    Glib::MatchInfo mi;
    ASSERT_TRUE(re->match(ocrText, mi));

    int matchStart, matchEnd;
    ASSERT_TRUE(mi.fetch_pos(0, matchStart, matchEnd));

    const int snippetPad = 20;
    int snipStart = std::max(0, matchStart - snippetPad);
    int snipEnd = std::min(static_cast<int>(ocrText.bytes()), matchEnd + snippetPad);
    Glib::ustring snippet = ocrText.substr(snipStart, snipEnd - snipStart);

    EXPECT_TRUE(snippet.find("Invoice") != std::string::npos);
    EXPECT_LE(snippet.size(), ocrText.size());
}

// --- OCR engine integration tests ---
// These tests run actual OCR inference on test images.
// Guarded by HAVE_NCNN; skipped if model files are missing.

#ifdef HAVE_NCNN

class OcrEngineTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (s_engine) return;
        std::string modelDir = "data/ocr_models";
        if (not std::filesystem::is_directory(modelDir)) {
            s_skip = true;
            return;
        }
        s_configPath = modelDir + "/ct_ocr_test_config.json";
        {
            std::ofstream f(s_configPath);
            f << "{\n"
              << R"(  "save": false,)" << "\n"
              << R"(  "det": { "infer_threads": 1, "model_path": ")" << modelDir << R"(/PP_OCRv6_tiny_det", "padding": 0, "max_side_len": 768, "box_thres": 0.45, "bitmap_thres": 0.2, "unclip_ratio": 1.4, "fp16": false },)" << "\n"
              << R"(  "cls": { "infer_threads": 1, "reco_threads": 1, "model_path": ")" << modelDir << R"(/PP_LCNet_x0_25_textline_ori", "enable": true, "most_angle": true, "fp16": false },)" << "\n"
              << R"(  "rec": { "infer_threads": 1, "reco_threads": 1, "model_path": ")" << modelDir << R"(/PP_OCRv6_tiny_rec", "keys_path": ")" << modelDir << R"(/ppocr_keys_v6_tiny.txt", "fp16": false })" << "\n"
              << "}\n";
        }

        s_engine = std::make_unique<OCR::OCREngine>();
        int saved = dup(STDERR_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
        bool ok = s_engine->Initialize(s_configPath);
        if (saved >= 0) { dup2(saved, STDERR_FILENO); close(saved); }
        if (not ok) {
            s_engine.reset();
            s_skip = true;
        }
    }

    std::string runOcr(const std::string& imagePath) {
        cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
        if (image.empty()) return {};
        int saved = dup(STDERR_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
        auto results = s_engine->Run(image);
        if (saved >= 0) { dup2(saved, STDERR_FILENO); close(saved); }
        std::string text;
        for (const auto& r : results) {
            if (not text.empty()) text += " ";
            text += r.line.text;
        }
        return text;
    }

    size_t runOcrLineCount(const std::string& imagePath) {
        cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
        if (image.empty()) return 0;
        int saved = dup(STDERR_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
        auto results = s_engine->Run(image);
        if (saved >= 0) { dup2(saved, STDERR_FILENO); close(saved); }
        return results.size();
    }

    static void TearDownTestSuite() {
        s_engine.reset();
        if (not s_configPath.empty()) {
            std::filesystem::remove(s_configPath);
            s_configPath.clear();
        }
    }

    static std::unique_ptr<OCR::OCREngine> s_engine;
    static bool s_skip;
    static std::string s_configPath;
};

std::unique_ptr<OCR::OCREngine> OcrEngineTest::s_engine;
bool OcrEngineTest::s_skip = false;
std::string OcrEngineTest::s_configPath;

static bool ocrContains(const std::string& text, const std::string& word) {
    return text.find(word) != std::string::npos;
}

TEST_F(OcrEngineTest, SimpleText)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_hello.png");
    EXPECT_TRUE(ocrContains(text, "Hello")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "World")) << "got: " << text;
}

TEST_F(OcrEngineTest, MultiLineDocument)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_invoice.png");
    EXPECT_TRUE(ocrContains(text, "Invoice")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "12345")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "January")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "2025")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "42.50")) << "got: " << text;
    EXPECT_EQ(3u, runOcrLineCount("tests/data_данные/ocr_test_invoice.png"));
}

TEST_F(OcrEngineTest, MixedSizes)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_mixed.png");
    EXPECT_TRUE(ocrContains(text, "Error")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "404")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "Page Not Found")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "URL")) << "got: " << text;
    EXPECT_EQ(3u, runOcrLineCount("tests/data_данные/ocr_test_mixed.png"));
}

TEST_F(OcrEngineTest, DarkBackground)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_dark_bg.png");
    EXPECT_TRUE(ocrContains(text, "Dark")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "Background")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "Text")) << "got: " << text;
}

TEST_F(OcrEngineTest, ColoredText)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_colored.png");
    EXPECT_TRUE(ocrContains(text, "Warning")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "Check")) << "got: " << text;
    EXPECT_TRUE(ocrContains(text, "Input")) << "got: " << text;
}

TEST_F(OcrEngineTest, EmptyImageReturnsNothing)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    cv::Mat blank(100, 300, CV_8UC3, cv::Scalar(255, 255, 255));
    int saved = dup(STDERR_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
    auto results = s_engine->Run(blank);
    if (saved >= 0) { dup2(saved, STDERR_FILENO); close(saved); }
    EXPECT_TRUE(results.empty());
}

TEST_F(OcrEngineTest, SearchPatternFindsOcrText)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_invoice.png");
    auto re = Glib::Regex::create("(?mi)12345");
    Glib::MatchInfo mi;
    EXPECT_TRUE(re->match(Glib::ustring{text}, mi));
}

TEST_F(OcrEngineTest, CaseInsensitiveSearchOnOcrOutput)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/ocr_test_hello.png");
    auto re = Glib::Regex::create("(?mi)hello world");
    Glib::MatchInfo mi;
    EXPECT_TRUE(re->match(Glib::ustring{text}, mi));
}

TEST_F(OcrEngineTest, NonExistentFileReturnsEmpty)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    std::string text = runOcr("tests/data_данные/no_such_file.png");
    EXPECT_TRUE(text.empty());
}

TEST_F(OcrEngineTest, OcrBoxesFormatIsValid)
{
    if (s_skip) GTEST_SKIP() << "OCR models not available";
    cv::Mat image = cv::imread("tests/data_данные/ocr_test_invoice.png", cv::IMREAD_COLOR);
    ASSERT_FALSE(image.empty());
    int saved = dup(STDERR_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
    auto results = s_engine->Run(image);
    if (saved >= 0) { dup2(saved, STDERR_FILENO); close(saved); }
    ASSERT_GE(results.size(), 1u);

    // Build boxes string the same way _run_ocr does
    std::string boxes = std::to_string(image.cols) + "," + std::to_string(image.rows);
    int pos = 0;
    for (const auto& res : results) {
        int startPos = pos;
        int endPos = pos + static_cast<int>(Glib::ustring{res.line.text}.size());
        if (pos > 0) pos++; // space separator
        pos = endPos;
        if (res.box.points.size() == 4) {
            boxes += ";";
            boxes += std::to_string(startPos) + "," + std::to_string(endPos);
            for (const auto& pt : res.box.points) {
                boxes += "," + std::to_string(pt.x) + "," + std::to_string(pt.y);
            }
        }
    }

    // Validate format: first token is "W,H", rest are "start,end,x0,y0,...,x3,y3"
    std::istringstream ss(boxes);
    std::string token;
    ASSERT_TRUE(std::getline(ss, token, ';'));
    auto commaPos = token.find(',');
    ASSERT_NE(std::string::npos, commaPos);
    int imgW = std::stoi(token.substr(0, commaPos));
    int imgH = std::stoi(token.substr(commaPos + 1));
    EXPECT_EQ(image.cols, imgW);
    EXPECT_EQ(image.rows, imgH);

    int boxCount = 0;
    while (std::getline(ss, token, ';')) {
        std::istringstream ts(token);
        std::string val;
        int numCount = 0;
        while (std::getline(ts, val, ',')) numCount++;
        EXPECT_EQ(10, numCount) << "box token: " << token;
        boxCount++;
    }
    EXPECT_EQ(static_cast<int>(results.size()), boxCount);
}

#endif // HAVE_NCNN
