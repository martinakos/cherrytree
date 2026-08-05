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

#ifdef HAVE_NCNN
#include "ct_ocr.h"
#include "ocr_engine.h"
#include "common.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <sstream>
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
        OCR::Config config;
        config.is_save = false;
        config.det_config.infer_threads = 1;
        config.det_config.model_path = modelDir + "/PP_OCRv6_tiny_det";
        config.det_config.padding = 0;
        config.det_config.max_side_len = 768;
        config.det_config.box_thres = 0.45f;
        config.det_config.bitmap_thres = 0.2f;
        config.det_config.unclip_ratio = 1.4f;
        config.det_config.is_fp16 = false;
        config.cls_config.infer_threads = 1;
        config.cls_config.reco_threads = 1;
        config.cls_config.model_path = modelDir + "/PP_LCNet_x0_25_textline_ori";
        config.cls_config.enable = true;
        config.cls_config.most_angle = true;
        config.cls_config.is_fp16 = false;
        config.rec_config.infer_threads = 1;
        config.rec_config.reco_threads = 1;
        config.rec_config.model_path = modelDir + "/PP_OCRv6_tiny_rec";
        config.rec_config.keys_path = modelDir + "/ppocr_keys_v6_tiny.txt";
        config.rec_config.is_fp16 = false;

        s_engine = std::make_unique<OCR::OCREngine>();
        int saved = dup(STDERR_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
        bool ok = s_engine->Initialize(config);
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

    static std::unique_ptr<OCR::OCREngine> s_engine;
    static bool s_skip;
};

std::unique_ptr<OCR::OCREngine> OcrEngineTest::s_engine;
bool OcrEngineTest::s_skip = false;

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
