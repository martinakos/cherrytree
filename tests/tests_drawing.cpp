/*
 * tests_drawing.cpp
 *
 * Tests for drawing canvas persistence and commands.
 *
 * Copyright 2009-2026
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

#include "ct_document_model.h"
#include "ct_drawing.h"
#include "ct_drawing_commands.h"
#include "ct_storage_xml.h"
#include "gtest/gtest.h"
#include <memory>

static CtDrawingStroke makeStroke(const std::string& color, double lineWidth,
                                 std::vector<std::pair<double,double>> pts)
{
    CtDrawingStroke s;
    s.color = color;
    s.lineWidth = lineWidth;
    s.opacity = 1.0;
    for (auto& [x, y] : pts) {
        s.points.push_back({x, y});
    }
    return s;
}

static CtDrawingCanvas makeCanvas(double x, double y, double w, double h)
{
    CtDrawingCanvas c;
    c.x = x;
    c.y = y;
    c.width = w;
    c.height = h;
    c.cornerRadius = 8.0;
    return c;
}

// ── Observer that tracks drawing notifications ──────────────────────────────

struct DrawingObserverLog : public CtDocumentObserver {
    int drawingChangedCount{0};
    gint64 lastDrawingNodeId{0};

    void onNodeChanged(gint64) override {}
    void onNodeAdded(gint64, gint64) override {}
    void onNodeDeleted(gint64) override {}
    void onNodeMoved(gint64, gint64, int) override {}
    void onTreeStructureChanged() override {}

    void onNodeDrawingChanged(gint64 nodeId) override {
        ++drawingChangedCount;
        lastDrawingNodeId = nodeId;
    }

    void clear() { drawingChangedCount = 0; lastDrawingNodeId = 0; }
};

// ── Drawing command tests ───────────────────────────────────────────────────

class DrawingCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = std::make_shared<CtDocumentModel>();
        auto node = model->createNode(1);
        node->setName("TestNode");
        model->addNode(node, 0, -1);
    }

    std::shared_ptr<CtDocumentModel> model;
};

TEST_F(DrawingCommandTest, AddCanvasCommand_ExecuteAndUndo)
{
    DrawingObserverLog log;
    model->addObserver(&log);

    auto canvas = makeCanvas(10, 20, 300, 250);
    AddCanvasCommand cmd(model, 1, canvas);

    cmd.execute();
    ASSERT_EQ(1u, model->getNodeById(1)->getDrawingCanvases().size());
    EXPECT_DOUBLE_EQ(10.0, model->getNodeById(1)->getDrawingCanvases()[0].x);
    EXPECT_EQ(1, log.drawingChangedCount);

    cmd.undo();
    EXPECT_EQ(0u, model->getNodeById(1)->getDrawingCanvases().size());
    EXPECT_EQ(2, log.drawingChangedCount);

    model->removeObserver(&log);
}

TEST_F(DrawingCommandTest, DeleteCanvasCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    auto& canvases = node->getDrawingCanvasesMut();
    canvases.push_back(makeCanvas(10, 20, 300, 250));
    canvases[0].strokes.push_back(makeStroke("#ff0000", 2.0, {{5,5},{10,10},{15,8}}));

    CtDrawingCanvas snapshot = canvases[0];
    DeleteCanvasCommand cmd(model, 1, snapshot, 0);

    cmd.execute();
    EXPECT_EQ(0u, node->getDrawingCanvases().size());

    cmd.undo();
    ASSERT_EQ(1u, node->getDrawingCanvases().size());
    EXPECT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
}

TEST_F(DrawingCommandTest, DrawStrokeCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeStroke("#0000ff", 3.0, {{10,10},{20,30},{30,20}});
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ(3u, node->getDrawingCanvases()[0].strokes[0].points.size());

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, EraseStrokeCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 2.0, {{1,1},{2,2}}));
    strokes.push_back(makeStroke("#00ff00", 4.0, {{5,5},{6,6}}));

    CtDrawingStroke erasedStroke = strokes[0];
    EraseStrokeCommand cmd(model, 1, 0, erasedStroke, 0);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#00ff00", node->getDrawingCanvases()[0].strokes[0].color);

    cmd.undo();
    ASSERT_EQ(2u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#00ff00", node->getDrawingCanvases()[0].strokes[1].color);
}

TEST_F(DrawingCommandTest, MoveCanvasCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(10, 20, 300, 250));

    MoveCanvasCommand cmd(model, 1, 0, 10, 20, 100, 200);

    cmd.execute();
    EXPECT_DOUBLE_EQ(100.0, node->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(200.0, node->getDrawingCanvases()[0].y);

    cmd.undo();
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(20.0, node->getDrawingCanvases()[0].y);
}

TEST_F(DrawingCommandTest, ResizeCanvasCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(10, 20, 300, 250));

    ResizeCanvasCommand cmd(model, 1, 0,
                            10, 20, 300, 250,
                            15, 25, 400, 350);

    cmd.execute();
    EXPECT_DOUBLE_EQ(15.0, node->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(25.0, node->getDrawingCanvases()[0].y);
    EXPECT_DOUBLE_EQ(400.0, node->getDrawingCanvases()[0].width);
    EXPECT_DOUBLE_EQ(350.0, node->getDrawingCanvases()[0].height);

    cmd.undo();
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(20.0, node->getDrawingCanvases()[0].y);
    EXPECT_DOUBLE_EQ(300.0, node->getDrawingCanvases()[0].width);
    EXPECT_DOUBLE_EQ(250.0, node->getDrawingCanvases()[0].height);
}

// ── XML serialization round-trip ────────────────────────────────────────────

TEST(DrawingXmlTest, RoundTrip_EmptyCanvases)
{
    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    std::vector<CtDrawingCanvas> empty;
    CtXmlHelper::drawing_canvases_to_xml(root, empty);

    auto result = CtXmlHelper::drawing_canvases_from_xml(root);
    EXPECT_TRUE(result.empty());
}

TEST(DrawingXmlTest, RoundTrip_CanvasWithStrokes)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c1 = makeCanvas(100.5, 200.5, 300, 250);
    c1.cornerRadius = 12.0;
    c1.strokes.push_back(makeStroke("#ff0000", 2.0, {{10,20},{15,30},{20,25}}));
    c1.strokes.push_back(makeStroke("#0000ff", 4.0, {{50,50},{60,70}}));
    canvases.push_back(std::move(c1));

    auto c2 = makeCanvas(400, 500, 200, 150);
    canvases.push_back(std::move(c2));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);

    auto result = CtXmlHelper::drawing_canvases_from_xml(root);
    ASSERT_EQ(2u, result.size());

    EXPECT_DOUBLE_EQ(100.5, result[0].x);
    EXPECT_DOUBLE_EQ(200.5, result[0].y);
    EXPECT_DOUBLE_EQ(300.0, result[0].width);
    EXPECT_DOUBLE_EQ(250.0, result[0].height);
    EXPECT_DOUBLE_EQ(12.0, result[0].cornerRadius);
    ASSERT_EQ(2u, result[0].strokes.size());

    EXPECT_EQ("#ff0000", result[0].strokes[0].color);
    EXPECT_DOUBLE_EQ(2.0, result[0].strokes[0].lineWidth);
    ASSERT_EQ(3u, result[0].strokes[0].points.size());
    EXPECT_DOUBLE_EQ(10.0, result[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(20.0, result[0].strokes[0].points[0].y);
    EXPECT_DOUBLE_EQ(15.0, result[0].strokes[0].points[1].x);
    EXPECT_DOUBLE_EQ(30.0, result[0].strokes[0].points[1].y);

    EXPECT_EQ("#0000ff", result[0].strokes[1].color);
    EXPECT_DOUBLE_EQ(4.0, result[0].strokes[1].lineWidth);
    ASSERT_EQ(2u, result[0].strokes[1].points.size());

    EXPECT_DOUBLE_EQ(400.0, result[1].x);
    EXPECT_DOUBLE_EQ(500.0, result[1].y);
    EXPECT_EQ(0u, result[1].strokes.size());
}

TEST(DrawingXmlTest, RoundTrip_StrokeOpacity)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 100, 100);
    auto s = makeStroke("#000000", 1.0, {{0,0},{10,10}});
    s.opacity = 0.5;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);

    auto result = CtXmlHelper::drawing_canvases_from_xml(root);
    ASSERT_EQ(1u, result.size());
    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_DOUBLE_EQ(0.5, result[0].strokes[0].opacity);
}

// ── SQLite serialization round-trip ─────────────────────────────────────────

#include <sqlite3.h>

class DrawingSqliteTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &_db));
        exec("CREATE TABLE IF NOT EXISTS drawing_canvas ("
             "node_id INTEGER, canvas_index INTEGER,"
             "x REAL, y REAL, width REAL, height REAL,"
             "corner_radius REAL DEFAULT 8.0,"
             "PRIMARY KEY (node_id, canvas_index))");
        exec("CREATE TABLE IF NOT EXISTS drawing_stroke ("
             "node_id INTEGER, canvas_index INTEGER, stroke_index INTEGER,"
             "color TEXT, width REAL, opacity REAL DEFAULT 1.0, points TEXT,"
             "PRIMARY KEY (node_id, canvas_index, stroke_index))");
    }

    void TearDown() override {
        sqlite3_close(_db);
    }

    void exec(const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(_db, sql, nullptr, nullptr, &err);
        if (err) {
            std::string msg = err;
            sqlite3_free(err);
            FAIL() << "SQL error: " << msg;
        }
        ASSERT_EQ(SQLITE_OK, rc);
    }

    void writeCanvases(gint64 nodeId, const std::vector<CtDrawingCanvas>& canvases) {
        std::string delC = "DELETE FROM drawing_canvas WHERE node_id=" + std::to_string(nodeId);
        std::string delS = "DELETE FROM drawing_stroke WHERE node_id=" + std::to_string(nodeId);
        exec(delC.c_str());
        exec(delS.c_str());

        for (size_t ci = 0; ci < canvases.size(); ++ci) {
            const auto& canvas = canvases[ci];
            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(_db, "INSERT INTO drawing_canvas VALUES(?,?,?,?,?,?,?)", -1, &stmt, nullptr);
            sqlite3_bind_int64(stmt, 1, nodeId);
            sqlite3_bind_int(stmt, 2, static_cast<int>(ci));
            sqlite3_bind_double(stmt, 3, canvas.x);
            sqlite3_bind_double(stmt, 4, canvas.y);
            sqlite3_bind_double(stmt, 5, canvas.width);
            sqlite3_bind_double(stmt, 6, canvas.height);
            sqlite3_bind_double(stmt, 7, canvas.cornerRadius);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            for (size_t si = 0; si < canvas.strokes.size(); ++si) {
                const auto& stroke = canvas.strokes[si];
                sqlite3_stmt* sStmt = nullptr;
                sqlite3_prepare_v2(_db, "INSERT INTO drawing_stroke VALUES(?,?,?,?,?,?,?)", -1, &sStmt, nullptr);
                sqlite3_bind_int64(sStmt, 1, nodeId);
                sqlite3_bind_int(sStmt, 2, static_cast<int>(ci));
                sqlite3_bind_int(sStmt, 3, static_cast<int>(si));
                sqlite3_bind_text(sStmt, 4, stroke.color.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(sStmt, 5, stroke.lineWidth);
                sqlite3_bind_double(sStmt, 6, stroke.opacity);

                std::string pointsStr;
                for (size_t pi = 0; pi < stroke.points.size(); ++pi) {
                    if (pi > 0) pointsStr += ";";
                    pointsStr += std::to_string(stroke.points[pi].x) + "," +
                                 std::to_string(stroke.points[pi].y);
                }
                sqlite3_bind_text(sStmt, 7, pointsStr.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(sStmt);
                sqlite3_finalize(sStmt);
            }
        }
    }

    std::vector<CtDrawingCanvas> readCanvases(gint64 nodeId) {
        std::vector<CtDrawingCanvas> canvases;
        sqlite3_stmt* cStmt = nullptr;
        sqlite3_prepare_v2(_db,
            "SELECT canvas_index, x, y, width, height, corner_radius "
            "FROM drawing_canvas WHERE node_id=? ORDER BY canvas_index",
            -1, &cStmt, nullptr);
        sqlite3_bind_int64(cStmt, 1, nodeId);

        while (sqlite3_step(cStmt) == SQLITE_ROW) {
            CtDrawingCanvas canvas;
            int canvasIdx = sqlite3_column_int(cStmt, 0);
            canvas.x = sqlite3_column_double(cStmt, 1);
            canvas.y = sqlite3_column_double(cStmt, 2);
            canvas.width = sqlite3_column_double(cStmt, 3);
            canvas.height = sqlite3_column_double(cStmt, 4);
            canvas.cornerRadius = sqlite3_column_double(cStmt, 5);

            sqlite3_stmt* sStmt = nullptr;
            sqlite3_prepare_v2(_db,
                "SELECT color, width, opacity, points FROM drawing_stroke "
                "WHERE node_id=? AND canvas_index=? ORDER BY stroke_index",
                -1, &sStmt, nullptr);
            sqlite3_bind_int64(sStmt, 1, nodeId);
            sqlite3_bind_int(sStmt, 2, canvasIdx);

            while (sqlite3_step(sStmt) == SQLITE_ROW) {
                CtDrawingStroke stroke;
                stroke.color = reinterpret_cast<const char*>(sqlite3_column_text(sStmt, 0));
                stroke.lineWidth = sqlite3_column_double(sStmt, 1);
                stroke.opacity = sqlite3_column_double(sStmt, 2);
                std::string pointsStr = reinterpret_cast<const char*>(sqlite3_column_text(sStmt, 3));
                size_t pos = 0;
                while (pos < pointsStr.size()) {
                    size_t commaPos = pointsStr.find(',', pos);
                    if (commaPos == std::string::npos) break;
                    size_t semiPos = pointsStr.find(';', commaPos);
                    if (semiPos == std::string::npos) semiPos = pointsStr.size();
                    double px = std::stod(pointsStr.substr(pos, commaPos - pos));
                    double py = std::stod(pointsStr.substr(commaPos + 1, semiPos - commaPos - 1));
                    stroke.points.push_back({px, py});
                    pos = semiPos + 1;
                }
                canvas.strokes.push_back(std::move(stroke));
            }
            sqlite3_finalize(sStmt);
            canvases.push_back(std::move(canvas));
        }
        sqlite3_finalize(cStmt);
        return canvases;
    }

    sqlite3* _db{nullptr};
};

TEST_F(DrawingSqliteTest, RoundTrip_CanvasWithStrokes)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(100.5, 200.5, 300, 250);
    c.cornerRadius = 12.0;
    c.strokes.push_back(makeStroke("#ff0000", 2.0, {{10,20},{15,30},{20,25}}));
    c.strokes.push_back(makeStroke("#0000ff", 4.0, {{50,50},{60,70}}));
    canvases.push_back(std::move(c));

    writeCanvases(42, canvases);
    auto result = readCanvases(42);

    ASSERT_EQ(1u, result.size());
    EXPECT_DOUBLE_EQ(100.5, result[0].x);
    EXPECT_DOUBLE_EQ(200.5, result[0].y);
    EXPECT_DOUBLE_EQ(300.0, result[0].width);
    EXPECT_DOUBLE_EQ(250.0, result[0].height);
    EXPECT_DOUBLE_EQ(12.0, result[0].cornerRadius);
    ASSERT_EQ(2u, result[0].strokes.size());
    EXPECT_EQ("#ff0000", result[0].strokes[0].color);
    EXPECT_EQ(3u, result[0].strokes[0].points.size());
    EXPECT_EQ("#0000ff", result[0].strokes[1].color);
    EXPECT_EQ(2u, result[0].strokes[1].points.size());
}

TEST_F(DrawingSqliteTest, RoundTrip_EmptyCanvases)
{
    std::vector<CtDrawingCanvas> canvases;
    writeCanvases(42, canvases);
    auto result = readCanvases(42);
    EXPECT_TRUE(result.empty());
}

TEST_F(DrawingSqliteTest, RoundTrip_CanvasNoStrokes)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(10, 20, 300, 250));
    writeCanvases(42, canvases);
    auto result = readCanvases(42);

    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(0u, result[0].strokes.size());
    EXPECT_DOUBLE_EQ(10.0, result[0].x);
}

TEST_F(DrawingSqliteTest, Overwrite_ReplacesExistingData)
{
    std::vector<CtDrawingCanvas> v1;
    auto c1 = makeCanvas(10, 20, 300, 250);
    c1.strokes.push_back(makeStroke("#ff0000", 2.0, {{1,1},{2,2}}));
    v1.push_back(std::move(c1));
    writeCanvases(42, v1);

    std::vector<CtDrawingCanvas> v2;
    auto c2 = makeCanvas(50, 60, 400, 300);
    c2.strokes.push_back(makeStroke("#00ff00", 3.0, {{5,5},{6,6},{7,7}}));
    v2.push_back(std::move(c2));
    writeCanvases(42, v2);

    auto result = readCanvases(42);
    ASSERT_EQ(1u, result.size());
    EXPECT_DOUBLE_EQ(50.0, result[0].x);
    EXPECT_EQ("#00ff00", result[0].strokes[0].color);
    EXPECT_EQ(3u, result[0].strokes[0].points.size());
}

TEST_F(DrawingSqliteTest, MultipleNodes_Independent)
{
    std::vector<CtDrawingCanvas> v1;
    v1.push_back(makeCanvas(10, 10, 100, 100));
    writeCanvases(1, v1);

    std::vector<CtDrawingCanvas> v2;
    v2.push_back(makeCanvas(20, 20, 200, 200));
    v2.push_back(makeCanvas(30, 30, 300, 300));
    writeCanvases(2, v2);

    auto r1 = readCanvases(1);
    auto r2 = readCanvases(2);
    EXPECT_EQ(1u, r1.size());
    EXPECT_EQ(2u, r2.size());
    EXPECT_DOUBLE_EQ(10.0, r1[0].x);
    EXPECT_DOUBLE_EQ(20.0, r2[0].x);
    EXPECT_DOUBLE_EQ(30.0, r2[1].x);
}

TEST_F(DrawingSqliteTest, DeleteNode_CleansUpDrawingData)
{
    std::vector<CtDrawingCanvas> v1;
    auto c = makeCanvas(10, 20, 300, 250);
    c.strokes.push_back(makeStroke("#ff0000", 2.0, {{1,1},{2,2}}));
    v1.push_back(std::move(c));
    writeCanvases(42, v1);

    exec("DELETE FROM drawing_canvas WHERE node_id=42");
    exec("DELETE FROM drawing_stroke WHERE node_id=42");

    auto result = readCanvases(42);
    EXPECT_TRUE(result.empty());
}

// ── Model-level drawing canvas observer test ────────────────────────────────

TEST(DrawingModelTest, NotifyDrawingChanged_FiresObserver)
{
    CtDocumentModel model;
    auto node = model.createNode(1);
    node->setName("TestNode");
    model.addNode(node, 0, -1);

    DrawingObserverLog log;
    model.addObserver(&log);

    model.notifyNodeDrawingChanged(1);
    EXPECT_EQ(1, log.drawingChangedCount);
    EXPECT_EQ(1, log.lastDrawingNodeId);

    model.notifyNodeDrawingChanged(1);
    EXPECT_EQ(2, log.drawingChangedCount);

    model.removeObserver(&log);
}

// ── Multiple canvases with multiple strokes ─────────────────────────────────

TEST(DrawingXmlTest, RoundTrip_MultipleCanvasesMultipleStrokes)
{
    std::vector<CtDrawingCanvas> canvases;
    for (int i = 0; i < 3; ++i) {
        auto c = makeCanvas(i * 100.0, i * 50.0, 200, 150);
        for (int j = 0; j < 4; ++j) {
            c.strokes.push_back(makeStroke("#112233", 1.0 + j,
                {{double(j), double(j+1)}, {double(j+2), double(j+3)}}));
        }
        canvases.push_back(std::move(c));
    }

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);

    auto result = CtXmlHelper::drawing_canvases_from_xml(root);
    ASSERT_EQ(3u, result.size());
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(i * 100.0, result[i].x);
        EXPECT_DOUBLE_EQ(i * 50.0, result[i].y);
        ASSERT_EQ(4u, result[i].strokes.size());
        for (int j = 0; j < 4; ++j) {
            EXPECT_DOUBLE_EQ(1.0 + j, result[i].strokes[j].lineWidth);
            ASSERT_EQ(2u, result[i].strokes[j].points.size());
        }
    }
}
