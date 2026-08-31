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
#include "ct_types.h"
#include "gtest/gtest.h"
#include <glibmm/regex.h>
#include <cmath>
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

TEST_F(DrawingCommandTest, AddCanvasCommand_Redo)
{
    auto canvas = makeCanvas(10, 20, 300, 250);
    AddCanvasCommand cmd(model, 1, canvas);

    cmd.execute();
    ASSERT_EQ(1u, model->getNodeById(1)->getDrawingCanvases().size());
    cmd.undo();
    EXPECT_EQ(0u, model->getNodeById(1)->getDrawingCanvases().size());
    cmd.execute();
    ASSERT_EQ(1u, model->getNodeById(1)->getDrawingCanvases().size());
    EXPECT_DOUBLE_EQ(10.0, model->getNodeById(1)->getDrawingCanvases()[0].x);
}

TEST_F(DrawingCommandTest, DeleteCanvasCommand_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(10, 20, 300, 250));
    CtDrawingCanvas snapshot = node->getDrawingCanvases()[0];
    DeleteCanvasCommand cmd(model, 1, snapshot, 0);

    cmd.execute();
    EXPECT_EQ(0u, node->getDrawingCanvases().size());
    cmd.undo();
    ASSERT_EQ(1u, node->getDrawingCanvases().size());
    cmd.execute();
    EXPECT_EQ(0u, node->getDrawingCanvases().size());
}

TEST_F(DrawingCommandTest, DrawStrokeCommand_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto stroke = makeStroke("#0000ff", 3.0, {{10,10},{20,30}});
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[0].color);
}

TEST_F(DrawingCommandTest, MoveCanvasCommand_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(10, 20, 300, 250));
    MoveCanvasCommand cmd(model, 1, 0, 10, 20, 100, 200);

    cmd.execute();
    cmd.undo();
    cmd.execute();
    EXPECT_DOUBLE_EQ(100.0, node->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(200.0, node->getDrawingCanvases()[0].y);
}

TEST_F(DrawingCommandTest, ResizeCanvasCommand_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(10, 20, 300, 250));
    ResizeCanvasCommand cmd(model, 1, 0, 10, 20, 300, 250, 15, 25, 400, 350);

    cmd.execute();
    cmd.undo();
    cmd.execute();
    EXPECT_DOUBLE_EQ(400.0, node->getDrawingCanvases()[0].width);
    EXPECT_DOUBLE_EQ(350.0, node->getDrawingCanvases()[0].height);
}

TEST_F(DrawingCommandTest, EraseStrokeCommand_MiddleIndex)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 1.0, {{1,1},{2,2}}));
    strokes.push_back(makeStroke("#00ff00", 2.0, {{3,3},{4,4}}));
    strokes.push_back(makeStroke("#0000ff", 3.0, {{5,5},{6,6}}));

    CtDrawingStroke erased = strokes[1];
    EraseStrokeCommand cmd(model, 1, 0, erased, 1);

    cmd.execute();
    ASSERT_EQ(2u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[1].color);

    cmd.undo();
    ASSERT_EQ(3u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#00ff00", node->getDrawingCanvases()[0].strokes[1].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[2].color);
}

TEST_F(DrawingCommandTest, AddMultipleCanvases_DeleteMiddle)
{
    auto node = model->getNodeById(1);

    AddCanvasCommand cmd1(model, 1, makeCanvas(10, 10, 100, 100));
    AddCanvasCommand cmd2(model, 1, makeCanvas(20, 20, 200, 200));
    AddCanvasCommand cmd3(model, 1, makeCanvas(30, 30, 300, 300));
    cmd1.execute();
    cmd2.execute();
    cmd3.execute();
    ASSERT_EQ(3u, node->getDrawingCanvases().size());

    CtDrawingCanvas snapshot = node->getDrawingCanvases()[1];
    DeleteCanvasCommand delCmd(model, 1, snapshot, 1);
    delCmd.execute();

    ASSERT_EQ(2u, node->getDrawingCanvases().size());
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(30.0, node->getDrawingCanvases()[1].x);

    delCmd.undo();
    ASSERT_EQ(3u, node->getDrawingCanvases().size());
    EXPECT_DOUBLE_EQ(20.0, node->getDrawingCanvases()[1].x);
}

TEST_F(DrawingCommandTest, AddCanvasCommand_TracksIndex)
{
    AddCanvasCommand cmd1(model, 1, makeCanvas(10, 10, 100, 100));
    AddCanvasCommand cmd2(model, 1, makeCanvas(20, 20, 200, 200));
    cmd1.execute();
    cmd2.execute();
    EXPECT_EQ(0u, cmd1.getCanvasIdx());
    EXPECT_EQ(1u, cmd2.getCanvasIdx());
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

// ── Canvas clipboard (cut/copy/paste) tests ────────────────────────────────

TEST_F(DrawingCommandTest, CopyCanvas_PopulatesClipboard)
{
    auto node = model->getNodeById(1);
    auto& canvases = node->getDrawingCanvasesMut();
    auto c = makeCanvas(10, 20, 300, 250);
    c.name = "MyCanvas";
    c.bgColor = "#aabbcc";
    c.strokes.push_back(makeStroke("#ff0000", 2.0, {{5,5},{10,10},{15,8}}));
    canvases.push_back(std::move(c));

    CtDrawingOverlay::clearClipboard();
    ASSERT_FALSE(CtDrawingOverlay::hasClipboard());

    CtDrawingOverlay::setClipboard(canvases[0]);
    ASSERT_TRUE(CtDrawingOverlay::hasClipboard());

    const auto& clip = CtDrawingOverlay::getClipboard();
    EXPECT_EQ("MyCanvas", clip.name);
    EXPECT_EQ("#aabbcc", clip.bgColor);
    EXPECT_DOUBLE_EQ(10.0, clip.x);
    EXPECT_DOUBLE_EQ(20.0, clip.y);
    EXPECT_DOUBLE_EQ(300.0, clip.width);
    EXPECT_DOUBLE_EQ(250.0, clip.height);
    ASSERT_EQ(1u, clip.strokes.size());
    EXPECT_EQ("#ff0000", clip.strokes[0].color);
    EXPECT_EQ(3u, clip.strokes[0].points.size());
}

TEST_F(DrawingCommandTest, CutCanvas_RemovesAndPopulatesClipboard)
{
    auto node = model->getNodeById(1);
    auto& canvases = node->getDrawingCanvasesMut();
    auto c = makeCanvas(50, 60, 200, 150);
    c.strokes.push_back(makeStroke("#0000ff", 3.0, {{1,1},{2,2}}));
    canvases.push_back(std::move(c));

    CtDrawingOverlay::setClipboard(canvases[0]);

    DeleteCanvasCommand cmd(model, 1, canvases[0], 0);
    cmd.execute();

    EXPECT_EQ(0u, node->getDrawingCanvases().size());
    ASSERT_TRUE(CtDrawingOverlay::hasClipboard());
    EXPECT_DOUBLE_EQ(50.0, CtDrawingOverlay::getClipboard().x);

    cmd.undo();
    ASSERT_EQ(1u, node->getDrawingCanvases().size());
    EXPECT_DOUBLE_EQ(50.0, node->getDrawingCanvases()[0].x);
}

TEST_F(DrawingCommandTest, PasteCanvas_AddsToTargetNode)
{
    auto node = model->getNodeById(1);
    auto& canvases = node->getDrawingCanvasesMut();
    auto c = makeCanvas(10, 20, 300, 250);
    c.bgColor = "#112233";
    c.strokes.push_back(makeStroke("#ff0000", 2.0, {{5,5},{10,10}}));
    canvases.push_back(std::move(c));

    CtDrawingOverlay::setClipboard(canvases[0]);

    auto node2 = model->createNode(2);
    node2->setName("TargetNode");
    model->addNode(node2, 0, -1);

    CtDrawingCanvas pasted = CtDrawingOverlay::getClipboard();
    pasted.x = 100.0;
    pasted.y = 200.0;

    AddCanvasCommand cmd(model, 2, std::move(pasted));
    cmd.execute();

    auto targetNode = model->getNodeById(2);
    ASSERT_EQ(1u, targetNode->getDrawingCanvases().size());
    EXPECT_DOUBLE_EQ(100.0, targetNode->getDrawingCanvases()[0].x);
    EXPECT_DOUBLE_EQ(200.0, targetNode->getDrawingCanvases()[0].y);
    EXPECT_DOUBLE_EQ(300.0, targetNode->getDrawingCanvases()[0].width);
    EXPECT_EQ("#112233", targetNode->getDrawingCanvases()[0].bgColor);
    ASSERT_EQ(1u, targetNode->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", targetNode->getDrawingCanvases()[0].strokes[0].color);

    ASSERT_EQ(1u, node->getDrawingCanvases().size());
}

TEST_F(DrawingCommandTest, PasteCanvas_UndoRemovesPasted)
{
    auto node = model->getNodeById(1);
    auto c = makeCanvas(10, 20, 300, 250);
    c.strokes.push_back(makeStroke("#ff0000", 2.0, {{5,5},{10,10}}));

    AddCanvasCommand cmd(model, 1, std::move(c));
    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases().size());

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases().size());

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases().size());
    EXPECT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, PasteCanvas_PreservesAllProperties)
{
    CtDrawingCanvas c;
    c.x = 10; c.y = 20; c.width = 300; c.height = 250;
    c.cornerRadius = 12.5;
    c.name = "TestCanvas";
    c.bgColor = "#aabbcc";
    c.bgOpacity = 0.75;
    c.showBorderWhenInactive = true;
    c.strokes.push_back(makeStroke("#ff0000", 2.0, {{5,5},{10,10}}));
    auto s2 = makeStroke("#0000ff", 4.0, {{20,20},{30,30},{40,40}});
    s2.opacity = 0.5;
    c.strokes.push_back(std::move(s2));

    CtDrawingOverlay::setClipboard(c);
    CtDrawingCanvas pasted = CtDrawingOverlay::getClipboard();
    pasted.x = 100.0;
    pasted.y = 200.0;

    AddCanvasCommand cmd(model, 1, std::move(pasted));
    cmd.execute();

    const auto& result = model->getNodeById(1)->getDrawingCanvases()[0];
    EXPECT_DOUBLE_EQ(100.0, result.x);
    EXPECT_DOUBLE_EQ(200.0, result.y);
    EXPECT_DOUBLE_EQ(300.0, result.width);
    EXPECT_DOUBLE_EQ(250.0, result.height);
    EXPECT_DOUBLE_EQ(12.5, result.cornerRadius);
    EXPECT_EQ("TestCanvas", result.name);
    EXPECT_EQ("#aabbcc", result.bgColor);
    EXPECT_DOUBLE_EQ(0.75, result.bgOpacity);
    EXPECT_TRUE(result.showBorderWhenInactive);
    ASSERT_EQ(2u, result.strokes.size());
    EXPECT_EQ("#ff0000", result.strokes[0].color);
    EXPECT_DOUBLE_EQ(2.0, result.strokes[0].lineWidth);
    EXPECT_EQ(2u, result.strokes[0].points.size());
    EXPECT_EQ("#0000ff", result.strokes[1].color);
    EXPECT_DOUBLE_EQ(0.5, result.strokes[1].opacity);
    EXPECT_EQ(3u, result.strokes[1].points.size());
}

TEST_F(DrawingCommandTest, ClearClipboard_RemovesStoredCanvas)
{
    CtDrawingOverlay::setClipboard(makeCanvas(10, 20, 300, 250));
    ASSERT_TRUE(CtDrawingOverlay::hasClipboard());

    CtDrawingOverlay::clearClipboard();
    EXPECT_FALSE(CtDrawingOverlay::hasClipboard());
}

TEST_F(DrawingCommandTest, ClipboardClearResetsState)
{
    CtDrawingOverlay::setClipboard(makeCanvas(10, 20, 300, 250));
    EXPECT_TRUE(CtDrawingOverlay::hasClipboard());
    CtDrawingOverlay::clearClipboard();
    EXPECT_FALSE(CtDrawingOverlay::hasClipboard());
}

TEST_F(DrawingCommandTest, PasteCanvas_CenterPosition)
{
    CtDrawingCanvas canvas = makeCanvas(0, 0, 200, 100);
    double hScroll = 50.0, vScroll = 80.0;
    double zoom = 1.0;
    double vpW = 800.0, vpH = 600.0;
    canvas.x = (hScroll + vpW * 0.5 - canvas.width * zoom * 0.5) / zoom;
    canvas.y = (vScroll + vpH * 0.5 - canvas.height * zoom * 0.5) / zoom;
    EXPECT_DOUBLE_EQ(350.0, canvas.x);
    EXPECT_DOUBLE_EQ(330.0, canvas.y);
}

TEST_F(DrawingCommandTest, PasteCanvas_CenterPositionWithZoom)
{
    CtDrawingCanvas canvas = makeCanvas(0, 0, 200, 100);
    double hScroll = 0.0, vScroll = 0.0;
    double zoom = 2.0;
    double vpW = 800.0, vpH = 600.0;
    canvas.x = (hScroll + vpW * 0.5 - canvas.width * zoom * 0.5) / zoom;
    canvas.y = (vScroll + vpH * 0.5 - canvas.height * zoom * 0.5) / zoom;
    EXPECT_DOUBLE_EQ(100.0, canvas.x);
    EXPECT_DOUBLE_EQ(100.0, canvas.y);
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

// ── CanvasPropertiesCommand tests ──────────────────────────────────────────

TEST_F(DrawingCommandTest, CanvasPropertiesCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    auto& canvases = node->getDrawingCanvasesMut();
    auto c = makeCanvas(10, 20, 300, 250);
    c.name = "OldName";
    c.bgColor = "#ffffff";
    c.bgOpacity = 0.15;
    c.cornerRadius = 8.0;
    c.showBorderWhenInactive = false;
    canvases.push_back(std::move(c));

    CanvasPropertiesCommand cmd(model, 1, 0,
        "OldName", "NewName",
        "#ffffff", "#aabbcc",
        0.15, 0.75,
        8.0, 12.0,
        false, true);

    cmd.execute();
    const auto& result = node->getDrawingCanvases()[0];
    EXPECT_EQ("NewName", result.name);
    EXPECT_EQ("#aabbcc", result.bgColor);
    EXPECT_DOUBLE_EQ(0.75, result.bgOpacity);
    EXPECT_DOUBLE_EQ(12.0, result.cornerRadius);
    EXPECT_TRUE(result.showBorderWhenInactive);

    cmd.undo();
    const auto& reverted = node->getDrawingCanvases()[0];
    EXPECT_EQ("OldName", reverted.name);
    EXPECT_EQ("#ffffff", reverted.bgColor);
    EXPECT_DOUBLE_EQ(0.15, reverted.bgOpacity);
    EXPECT_DOUBLE_EQ(8.0, reverted.cornerRadius);
    EXPECT_FALSE(reverted.showBorderWhenInactive);
}

TEST_F(DrawingCommandTest, CanvasPropertiesCommand_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 200, 150));

    CanvasPropertiesCommand cmd(model, 1, 0,
        "", "Renamed",
        "#ffffff", "#112233",
        0.15, 0.5,
        8.0, 16.0,
        false, true);

    cmd.execute();
    cmd.undo();
    cmd.execute();
    EXPECT_EQ("Renamed", node->getDrawingCanvases()[0].name);
    EXPECT_EQ("#112233", node->getDrawingCanvases()[0].bgColor);
    EXPECT_TRUE(node->getDrawingCanvases()[0].showBorderWhenInactive);
}

// ── New element type command tests ─────────────────────────────────────────

static CtDrawingStroke makeLineStroke(const std::string& color, double w,
                                      double x0, double y0, double x1, double y1)
{
    CtDrawingStroke s;
    s.color = color;
    s.lineWidth = w;
    s.opacity = 1.0;
    s.type = CtDrawingElementType::Line;
    s.points.push_back({x0, y0});
    s.points.push_back({x1, y1});
    return s;
}

static CtDrawingStroke makeShapeStroke(CtDrawingElementType type, const std::string& color,
                                       double w, bool filled,
                                       double x0, double y0, double x1, double y1)
{
    CtDrawingStroke s;
    s.color = color;
    s.lineWidth = w;
    s.opacity = 1.0;
    s.type = type;
    s.filled = filled;
    s.points.push_back({x0, y0});
    s.points.push_back({x1, y1});
    return s;
}

static CtDrawingStroke makeTextStroke(const std::string& text, const std::string& font,
                                      double fontSize, double x, double y)
{
    CtDrawingStroke s;
    s.color = "#000000";
    s.lineWidth = 1.0;
    s.opacity = 1.0;
    s.type = CtDrawingElementType::Text;
    s.textContent = text;
    s.fontFamily = font;
    s.fontSize = fontSize;
    s.points.push_back({x, y});
    return s;
}

TEST_F(DrawingCommandTest, DrawLineStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeLineStroke("#ff0000", 2.0, 10, 20, 100, 80);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Line, node->getDrawingCanvases()[0].strokes[0].type);
    EXPECT_EQ(2u, node->getDrawingCanvases()[0].strokes[0].points.size());

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, DrawRectangleStroke_FilledExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeShapeStroke(CtDrawingElementType::Rectangle, "#00ff00", 3.0, true,
                                   10, 10, 100, 80);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Rectangle, s.type);
    EXPECT_TRUE(s.filled);
    EXPECT_DOUBLE_EQ(10.0, s.points[0].x);
    EXPECT_DOUBLE_EQ(100.0, s.points[1].x);

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, DrawEllipseStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeShapeStroke(CtDrawingElementType::Ellipse, "#0000ff", 2.0, false,
                                   20, 30, 120, 90);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    EXPECT_EQ(CtDrawingElementType::Ellipse, node->getDrawingCanvases()[0].strokes[0].type);
    EXPECT_FALSE(node->getDrawingCanvases()[0].strokes[0].filled);

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, DrawTextStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeTextStroke("Hello World", "Monospace", 16.0, 50, 60);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Text, s.type);
    EXPECT_EQ("Hello World", s.textContent);
    EXPECT_EQ("Monospace", s.fontFamily);
    EXPECT_DOUBLE_EQ(16.0, s.fontSize);
    EXPECT_DOUBLE_EQ(50.0, s.points[0].x);

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

// ── XML round-trip for new element types ───────────────────────────────────

TEST(DrawingXmlTest, RoundTrip_LineStroke)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeLineStroke("#ff0000", 3.0, 10, 20, 100, 80));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Line, result[0].strokes[0].type);
    ASSERT_EQ(2u, result[0].strokes[0].points.size());
    EXPECT_DOUBLE_EQ(10.0, result[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(100.0, result[0].strokes[0].points[1].x);
}

TEST(DrawingXmlTest, RoundTrip_FilledRectangle)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Rectangle, "#00ff00", 2.0, true,
                                         5, 10, 95, 70));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Rectangle, result[0].strokes[0].type);
    EXPECT_TRUE(result[0].strokes[0].filled);
}

TEST(DrawingXmlTest, RoundTrip_Ellipse)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Ellipse, "#0000ff", 4.0, false,
                                         20, 30, 120, 90));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Ellipse, result[0].strokes[0].type);
    EXPECT_FALSE(result[0].strokes[0].filled);
}

TEST(DrawingXmlTest, RoundTrip_TextStroke)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeTextStroke("Test Label", "Serif", 18.0, 40, 50));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    const auto& s = result[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Text, s.type);
    EXPECT_EQ("Test Label", s.textContent);
    EXPECT_EQ("Serif", s.fontFamily);
    EXPECT_DOUBLE_EQ(18.0, s.fontSize);
    EXPECT_DOUBLE_EQ(40.0, s.points[0].x);
    EXPECT_DOUBLE_EQ(50.0, s.points[0].y);
}

TEST(DrawingXmlTest, RoundTrip_MixedElementTypes)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 400, 300);
    c.strokes.push_back(makeStroke("#000000", 2.0, {{1,1},{2,2},{3,3}}));
    c.strokes.push_back(makeLineStroke("#ff0000", 3.0, 10, 20, 100, 80));
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Rectangle, "#00ff00", 2.0, true, 5, 10, 95, 70));
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Ellipse, "#0000ff", 4.0, false, 20, 30, 120, 90));
    c.strokes.push_back(makeTextStroke("Label", "Sans", 14.0, 50, 60));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(5u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Freehand, result[0].strokes[0].type);
    EXPECT_EQ(CtDrawingElementType::Line, result[0].strokes[1].type);
    EXPECT_EQ(CtDrawingElementType::Rectangle, result[0].strokes[2].type);
    EXPECT_TRUE(result[0].strokes[2].filled);
    EXPECT_EQ(CtDrawingElementType::Ellipse, result[0].strokes[3].type);
    EXPECT_EQ(CtDrawingElementType::Text, result[0].strokes[4].type);
    EXPECT_EQ("Label", result[0].strokes[4].textContent);
}

// ── Polyline, Triangle, Diamond element type tests ────────────────────────

static CtDrawingStroke makePolylineStroke(const std::string& color, double w,
                                          std::vector<std::pair<double,double>> pts)
{
    CtDrawingStroke s;
    s.color = color;
    s.lineWidth = w;
    s.opacity = 1.0;
    s.type = CtDrawingElementType::Polyline;
    for (auto& [x, y] : pts) {
        s.points.push_back({x, y});
    }
    return s;
}

TEST_F(DrawingCommandTest, DrawPolylineStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makePolylineStroke("#ff00ff", 2.0, {{10,10},{50,30},{90,10},{120,50}});
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Polyline, s.type);
    EXPECT_EQ(4u, s.points.size());
    EXPECT_DOUBLE_EQ(10.0, s.points[0].x);
    EXPECT_DOUBLE_EQ(120.0, s.points[3].x);

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, DrawTriangleStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeShapeStroke(CtDrawingElementType::Triangle, "#009900", 2.0, true,
                                   20, 20, 100, 80);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Triangle, s.type);
    EXPECT_TRUE(s.filled);
    EXPECT_EQ(2u, s.points.size());

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, DrawDiamondStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeShapeStroke(CtDrawingElementType::Diamond, "#cc6600", 3.0, false,
                                   30, 40, 130, 120);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Diamond, s.type);
    EXPECT_FALSE(s.filled);
    EXPECT_DOUBLE_EQ(30.0, s.points[0].x);
    EXPECT_DOUBLE_EQ(130.0, s.points[1].x);

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

// ── Line style tests ─────────────────────────────────────────────────────

TEST_F(DrawingCommandTest, DrawStroke_LineStylePreserved)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeLineStroke("#ff0000", 2.0, 10, 20, 100, 80);
    stroke.lineStyle = CtDrawingLineStyle::Dashed;
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    EXPECT_EQ(CtDrawingLineStyle::Dashed, node->getDrawingCanvases()[0].strokes[0].lineStyle);

    cmd.undo();
    cmd.execute();
    EXPECT_EQ(CtDrawingLineStyle::Dashed, node->getDrawingCanvases()[0].strokes[0].lineStyle);
}

TEST(DrawingStrokeDefaults, LineStyle_DefaultIsSolid)
{
    CtDrawingStroke s;
    EXPECT_EQ(CtDrawingLineStyle::Solid, s.lineStyle);
}

// ── XML round-trip for new element types ──────────────────────────────────

TEST(DrawingXmlTest, RoundTrip_Polyline)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makePolylineStroke("#ff00ff", 2.0, {{10,10},{50,30},{90,10},{120,50}}));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    const auto& s = result[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Polyline, s.type);
    ASSERT_EQ(4u, s.points.size());
    EXPECT_DOUBLE_EQ(10.0, s.points[0].x);
    EXPECT_DOUBLE_EQ(50.0, s.points[1].x);
    EXPECT_DOUBLE_EQ(90.0, s.points[2].x);
    EXPECT_DOUBLE_EQ(120.0, s.points[3].x);
}

TEST(DrawingXmlTest, RoundTrip_Triangle)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Triangle, "#009900", 2.0, true,
                                         20, 20, 100, 80));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Triangle, result[0].strokes[0].type);
    EXPECT_TRUE(result[0].strokes[0].filled);
}

TEST(DrawingXmlTest, RoundTrip_Diamond)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Diamond, "#cc6600", 3.0, false,
                                         30, 40, 130, 120));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Diamond, result[0].strokes[0].type);
    EXPECT_FALSE(result[0].strokes[0].filled);
    EXPECT_DOUBLE_EQ(30.0, result[0].strokes[0].points[0].x);
}

// ── XML round-trip for line style ────────────────────────────────────────

TEST(DrawingXmlTest, RoundTrip_LineStyle_Dashed)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto s = makeLineStroke("#ff0000", 2.0, 10, 20, 100, 80);
    s.lineStyle = CtDrawingLineStyle::Dashed;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingLineStyle::Dashed, result[0].strokes[0].lineStyle);
}

TEST(DrawingXmlTest, RoundTrip_LineStyle_Dotted)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto s = makeStroke("#000000", 1.0, {{0,0},{10,10}});
    s.lineStyle = CtDrawingLineStyle::Dotted;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingLineStyle::Dotted, result[0].strokes[0].lineStyle);
}

TEST(DrawingXmlTest, RoundTrip_LineStyle_DashDot)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto s = makeShapeStroke(CtDrawingElementType::Rectangle, "#00ff00", 2.0, false,
                              10, 10, 90, 70);
    s.lineStyle = CtDrawingLineStyle::DashDot;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingLineStyle::DashDot, result[0].strokes[0].lineStyle);
}

TEST(DrawingXmlTest, RoundTrip_LineStyle_SolidOmittedInXml)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto s = makeLineStroke("#ff0000", 2.0, 10, 20, 100, 80);
    s.lineStyle = CtDrawingLineStyle::Solid;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingLineStyle::Solid, result[0].strokes[0].lineStyle);
}

TEST(DrawingXmlTest, RoundTrip_AllNewTypesWithLineStyles)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 500, 400);

    auto s1 = makePolylineStroke("#ff00ff", 2.0, {{10,10},{50,30},{90,10}});
    s1.lineStyle = CtDrawingLineStyle::Dashed;
    c.strokes.push_back(std::move(s1));

    auto s2 = makeShapeStroke(CtDrawingElementType::Triangle, "#009900", 3.0, true, 20, 20, 100, 80);
    s2.lineStyle = CtDrawingLineStyle::Dotted;
    c.strokes.push_back(std::move(s2));

    auto s3 = makeShapeStroke(CtDrawingElementType::Diamond, "#cc6600", 4.0, false, 30, 40, 130, 120);
    s3.lineStyle = CtDrawingLineStyle::DashDot;
    c.strokes.push_back(std::move(s3));

    auto s4 = makeLineStroke("#0000ff", 1.0, 0, 0, 100, 100);
    s4.lineStyle = CtDrawingLineStyle::Solid;
    c.strokes.push_back(std::move(s4));

    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(4u, result[0].strokes.size());

    EXPECT_EQ(CtDrawingElementType::Polyline, result[0].strokes[0].type);
    EXPECT_EQ(CtDrawingLineStyle::Dashed, result[0].strokes[0].lineStyle);
    EXPECT_EQ(3u, result[0].strokes[0].points.size());

    EXPECT_EQ(CtDrawingElementType::Triangle, result[0].strokes[1].type);
    EXPECT_EQ(CtDrawingLineStyle::Dotted, result[0].strokes[1].lineStyle);
    EXPECT_TRUE(result[0].strokes[1].filled);

    EXPECT_EQ(CtDrawingElementType::Diamond, result[0].strokes[2].type);
    EXPECT_EQ(CtDrawingLineStyle::DashDot, result[0].strokes[2].lineStyle);
    EXPECT_FALSE(result[0].strokes[2].filled);

    EXPECT_EQ(CtDrawingElementType::Line, result[0].strokes[3].type);
    EXPECT_EQ(CtDrawingLineStyle::Solid, result[0].strokes[3].lineStyle);
}

// ── Rotation tests ──────────────────────────────────────────────────────────

TEST(DrawingStrokeDefaults, Rotation_DefaultIsZero)
{
    CtDrawingStroke s;
    EXPECT_DOUBLE_EQ(0.0, s.rotation);
}

TEST_F(DrawingCommandTest, RotateStrokeCommand_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 2.0, {{10,10},{50,50}}));

    RotateStrokeCommand cmd(model, 1, 0, 0, 0.0, M_PI / 4.0);

    cmd.execute();
    EXPECT_DOUBLE_EQ(M_PI / 4.0, node->getDrawingCanvases()[0].strokes[0].rotation);

    cmd.undo();
    EXPECT_DOUBLE_EQ(0.0, node->getDrawingCanvases()[0].strokes[0].rotation);
}

TEST_F(DrawingCommandTest, RotateStrokeCommand_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 2.0, {{10,10},{50,50}}));

    RotateStrokeCommand cmd(model, 1, 0, 0, 0.0, 1.5);

    cmd.execute();
    cmd.undo();
    EXPECT_DOUBLE_EQ(0.0, node->getDrawingCanvases()[0].strokes[0].rotation);
    cmd.execute();
    EXPECT_DOUBLE_EQ(1.5, node->getDrawingCanvases()[0].strokes[0].rotation);
}

TEST_F(DrawingCommandTest, RotateStrokeCommand_NotifiesObserver)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{50,50}}));

    DrawingObserverLog log;
    model->addObserver(&log);

    RotateStrokeCommand cmd(model, 1, 0, 0, 0.0, M_PI / 2.0);
    cmd.execute();
    EXPECT_EQ(1, log.drawingChangedCount);
    EXPECT_EQ(1, log.lastDrawingNodeId);

    cmd.undo();
    EXPECT_EQ(2, log.drawingChangedCount);

    model->removeObserver(&log);
}

TEST(DrawingXmlTest, RoundTrip_Rotation)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto s = makeStroke("#ff0000", 2.0, {{10,10},{50,50}});
    s.rotation = M_PI / 3.0;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_NEAR(M_PI / 3.0, result[0].strokes[0].rotation, 1e-6);
}

TEST(DrawingXmlTest, RoundTrip_ZeroRotationOmitted)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto s = makeStroke("#ff0000", 2.0, {{10,10},{50,50}});
    s.rotation = 0.0;
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_DOUBLE_EQ(0.0, result[0].strokes[0].rotation);
}

// ── Drawing canvas search tests ────────────────────────────────────────────

static bool canvasNameMatchesPattern(const CtDrawingCanvas& canvas,
                                     const Glib::RefPtr<Glib::Regex>& re)
{
    Glib::MatchInfo mi;
    return not canvas.name.empty() and re->match(Glib::ustring{canvas.name}, mi) and mi.matches();
}

static std::vector<std::string> canvasTextMatches(const CtDrawingCanvas& canvas,
                                                  const Glib::RefPtr<Glib::Regex>& re)
{
    std::vector<std::string> hits;
    for (const auto& stroke : canvas.strokes) {
        if (stroke.type != CtDrawingElementType::Text or stroke.textContent.empty()) continue;
        Glib::MatchInfo mi;
        if (re->match(Glib::ustring{stroke.textContent}, mi) and mi.matches()) {
            hits.push_back(stroke.textContent);
        }
    }
    return hits;
}

TEST(DrawingSearchTest, MatchCanvasName)
{
    auto re = Glib::Regex::create("Diagram", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    c.name = "Architecture Diagram";
    EXPECT_TRUE(canvasNameMatchesPattern(c, re));
}

TEST(DrawingSearchTest, NoMatchCanvasName)
{
    auto re = Glib::Regex::create("flowchart", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    c.name = "Architecture Diagram";
    EXPECT_FALSE(canvasNameMatchesPattern(c, re));
}

TEST(DrawingSearchTest, EmptyCanvasName_NoMatch)
{
    auto re = Glib::Regex::create("anything", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    EXPECT_FALSE(canvasNameMatchesPattern(c, re));
}

TEST(DrawingSearchTest, MatchTextStrokeContent)
{
    auto re = Glib::Regex::create("hello", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    c.strokes.push_back(makeTextStroke("Hello World", "Sans", 14.0, 10, 20));
    c.strokes.push_back(makeStroke("#000000", 2.0, {{0,0},{10,10}}));  // freehand, no text
    c.strokes.push_back(makeTextStroke("Goodbye", "Sans", 14.0, 30, 40));

    auto hits = canvasTextMatches(c, re);
    ASSERT_EQ(1u, hits.size());
    EXPECT_EQ("Hello World", hits[0]);
}

TEST(DrawingSearchTest, MatchMultipleTextStrokes)
{
    auto re = Glib::Regex::create("note", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    c.strokes.push_back(makeTextStroke("Note 1", "Sans", 14.0, 10, 20));
    c.strokes.push_back(makeTextStroke("Another Note", "Sans", 14.0, 30, 40));
    c.strokes.push_back(makeTextStroke("No match", "Sans", 14.0, 50, 60));

    auto hits = canvasTextMatches(c, re);
    ASSERT_EQ(2u, hits.size());
    EXPECT_EQ("Note 1", hits[0]);
    EXPECT_EQ("Another Note", hits[1]);
}

TEST(DrawingSearchTest, NoTextStrokes_NoMatch)
{
    auto re = Glib::Regex::create("anything", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    c.strokes.push_back(makeStroke("#000000", 2.0, {{0,0},{10,10}}));
    c.strokes.push_back(makeLineStroke("#ff0000", 1.0, 0, 0, 50, 50));

    auto hits = canvasTextMatches(c, re);
    EXPECT_TRUE(hits.empty());
}

TEST(DrawingSearchTest, SkipsNonTextStrokeTypes)
{
    auto re = Glib::Regex::create(".*", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    auto rect = makeShapeStroke(CtDrawingElementType::Rectangle, "#00ff00", 2.0, true, 0, 0, 50, 50);
    rect.textContent = "should not match";
    c.strokes.push_back(std::move(rect));

    auto hits = canvasTextMatches(c, re);
    EXPECT_TRUE(hits.empty());
}

TEST(DrawingSearchTest, CaseSensitiveMatch)
{
    auto re = Glib::Regex::create("Hello", static_cast<Glib::RegexCompileFlags>(0));
    CtDrawingCanvas c;
    c.name = "hello canvas";
    c.strokes.push_back(makeTextStroke("hello world", "Sans", 14.0, 10, 20));
    c.strokes.push_back(makeTextStroke("Hello World", "Sans", 14.0, 30, 40));

    EXPECT_FALSE(canvasNameMatchesPattern(c, re));
    auto hits = canvasTextMatches(c, re);
    ASSERT_EQ(1u, hits.size());
    EXPECT_EQ("Hello World", hits[0]);
}

TEST(DrawingSearchTest, RegexPatternMatch)
{
    auto re = Glib::Regex::create("^Step \\d+$", Glib::RegexCompileFlags::REGEX_CASELESS);
    CtDrawingCanvas c;
    c.strokes.push_back(makeTextStroke("Step 1", "Sans", 14.0, 10, 20));
    c.strokes.push_back(makeTextStroke("Step 2", "Sans", 14.0, 30, 40));
    c.strokes.push_back(makeTextStroke("Step one", "Sans", 14.0, 50, 60));

    auto hits = canvasTextMatches(c, re);
    ASSERT_EQ(2u, hits.size());
    EXPECT_EQ("Step 1", hits[0]);
    EXPECT_EQ("Step 2", hits[1]);
}

TEST(DrawingSearchTest, SearchOptionsDefault_DrawingCanvasesEnabled)
{
    CtSearchOptions opts;
    EXPECT_TRUE(opts.drawing_canvases);
}

TEST(DrawingSearchTest, MatchType_DrawingCanvasesExists)
{
    CtMatchType mt = CtMatchType::DrawingCanvases;
    EXPECT_NE(CtMatchType::None, mt);
    EXPECT_NE(CtMatchType::Content, mt);
    EXPECT_NE(CtMatchType::NameNTags, mt);
}

TEST(DrawingSearchTest, ModelCanvasSearchableContent)
{
    auto model = std::make_shared<CtDocumentModel>();
    auto node = model->createNode(1);
    node->setName("TestNode");
    model->addNode(node, 0, -1);

    auto& canvases = node->getDrawingCanvasesMut();
    CtDrawingCanvas c;
    c.name = "FlowChart";
    c.strokes.push_back(makeTextStroke("Start", "Sans", 14.0, 10, 20));
    c.strokes.push_back(makeTextStroke("Process Data", "Sans", 14.0, 50, 60));
    c.strokes.push_back(makeStroke("#000000", 2.0, {{0,0},{10,10}}));
    canvases.push_back(std::move(c));

    auto re = Glib::Regex::create("flow", Glib::RegexCompileFlags::REGEX_CASELESS);
    const auto& stored = node->getDrawingCanvases();
    ASSERT_EQ(1u, stored.size());
    EXPECT_TRUE(canvasNameMatchesPattern(stored[0], re));

    auto re2 = Glib::Regex::create("data", Glib::RegexCompileFlags::REGEX_CASELESS);
    auto hits = canvasTextMatches(stored[0], re2);
    ASSERT_EQ(1u, hits.size());
    EXPECT_EQ("Process Data", hits[0]);
}

TEST(DrawingSearchTest, MultipleCanvases_IndependentMatching)
{
    auto model = std::make_shared<CtDocumentModel>();
    auto node = model->createNode(1);
    node->setName("TestNode");
    model->addNode(node, 0, -1);

    auto& canvases = node->getDrawingCanvasesMut();

    CtDrawingCanvas c1;
    c1.name = "Network Diagram";
    c1.strokes.push_back(makeTextStroke("Server A", "Sans", 14.0, 10, 20));
    canvases.push_back(std::move(c1));

    CtDrawingCanvas c2;
    c2.name = "User Flow";
    c2.strokes.push_back(makeTextStroke("Login Page", "Sans", 14.0, 10, 20));
    c2.strokes.push_back(makeTextStroke("Server Response", "Sans", 14.0, 50, 60));
    canvases.push_back(std::move(c2));

    auto re = Glib::Regex::create("server", Glib::RegexCompileFlags::REGEX_CASELESS);
    const auto& stored = node->getDrawingCanvases();

    auto hits1 = canvasTextMatches(stored[0], re);
    ASSERT_EQ(1u, hits1.size());
    EXPECT_EQ("Server A", hits1[0]);

    auto hits2 = canvasTextMatches(stored[1], re);
    ASSERT_EQ(1u, hits2.size());
    EXPECT_EQ("Server Response", hits2[0]);

    EXPECT_FALSE(canvasNameMatchesPattern(stored[0], re));
    EXPECT_FALSE(canvasNameMatchesPattern(stored[1], re));
}

// ── Bezier point editing via MoveStrokeCommand (Select tool) ──────────────

static CtDrawingStroke makeBezierStroke(const std::string& color, double w,
                                        double p0x, double p0y,
                                        double cp1x, double cp1y,
                                        double cp2x, double cp2y,
                                        double p3x, double p3y)
{
    CtDrawingStroke s;
    s.color = color;
    s.lineWidth = w;
    s.opacity = 1.0;
    s.type = CtDrawingElementType::BezierCurve;
    s.points.push_back({p0x, p0y});
    s.points.push_back({cp1x, cp1y});
    s.points.push_back({cp2x, cp2y});
    s.points.push_back({p3x, p3y});
    return s;
}

TEST_F(DrawingCommandTest, MoveStrokeCommand_BezierControlPoint_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeBezierStroke("#ff0000", 2.0,
        10, 50,   30, 10,   70, 90,   90, 50));

    std::vector<CtDrawingPoint> oldPoints = strokes[0].points;
    std::vector<CtDrawingPoint> newPoints = oldPoints;
    newPoints[1] = {40, 5};

    MoveStrokeCommand cmd(model, 1, 0, 0, oldPoints, newPoints);

    cmd.execute();
    EXPECT_DOUBLE_EQ(40.0, node->getDrawingCanvases()[0].strokes[0].points[1].x);
    EXPECT_DOUBLE_EQ(5.0, node->getDrawingCanvases()[0].strokes[0].points[1].y);
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(90.0, node->getDrawingCanvases()[0].strokes[0].points[3].x);

    cmd.undo();
    EXPECT_DOUBLE_EQ(30.0, node->getDrawingCanvases()[0].strokes[0].points[1].x);
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].strokes[0].points[1].y);
}

TEST_F(DrawingCommandTest, MoveStrokeCommand_BezierEndpoint_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeBezierStroke("#0000ff", 3.0,
        10, 50,   30, 10,   70, 90,   90, 50));

    std::vector<CtDrawingPoint> oldPoints = strokes[0].points;
    std::vector<CtDrawingPoint> newPoints = oldPoints;
    newPoints[0] = {15, 55};

    MoveStrokeCommand cmd(model, 1, 0, 0, oldPoints, newPoints);

    cmd.execute();
    EXPECT_DOUBLE_EQ(15.0, node->getDrawingCanvases()[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(55.0, node->getDrawingCanvases()[0].strokes[0].points[0].y);

    cmd.undo();
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(50.0, node->getDrawingCanvases()[0].strokes[0].points[0].y);
}

TEST_F(DrawingCommandTest, MoveStrokeCommand_BezierPoint_Redo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeBezierStroke("#00ff00", 2.0,
        10, 50,   30, 10,   70, 90,   90, 50));

    std::vector<CtDrawingPoint> oldPoints = strokes[0].points;
    std::vector<CtDrawingPoint> newPoints = oldPoints;
    newPoints[2] = {80, 85};

    MoveStrokeCommand cmd(model, 1, 0, 0, oldPoints, newPoints);

    cmd.execute();
    cmd.undo();
    EXPECT_DOUBLE_EQ(70.0, node->getDrawingCanvases()[0].strokes[0].points[2].x);
    EXPECT_DOUBLE_EQ(90.0, node->getDrawingCanvases()[0].strokes[0].points[2].y);

    cmd.execute();
    EXPECT_DOUBLE_EQ(80.0, node->getDrawingCanvases()[0].strokes[0].points[2].x);
    EXPECT_DOUBLE_EQ(85.0, node->getDrawingCanvases()[0].strokes[0].points[2].y);
}

TEST_F(DrawingCommandTest, MoveStrokeCommand_BezierPoint_NotifiesObserver)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeBezierStroke("#ff0000", 2.0, 10, 50, 30, 10, 70, 90, 90, 50));

    DrawingObserverLog log;
    model->addObserver(&log);

    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    std::vector<CtDrawingPoint> oldPoints = strokes[0].points;
    std::vector<CtDrawingPoint> newPoints = oldPoints;
    newPoints[3] = {100, 60};

    MoveStrokeCommand cmd(model, 1, 0, 0, oldPoints, newPoints);
    cmd.execute();
    EXPECT_EQ(1, log.drawingChangedCount);
    EXPECT_EQ(1, log.lastDrawingNodeId);

    cmd.undo();
    EXPECT_EQ(2, log.drawingChangedCount);

    model->removeObserver(&log);
}

TEST(DrawingXmlTest, RoundTrip_BezierCurve)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeBezierStroke("#ff0000", 2.5,
        10, 50,   30, 10,   70, 90,   90, 50));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    const auto& s = result[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::BezierCurve, s.type);
    ASSERT_EQ(4u, s.points.size());
    EXPECT_DOUBLE_EQ(10.0, s.points[0].x);
    EXPECT_DOUBLE_EQ(50.0, s.points[0].y);
    EXPECT_DOUBLE_EQ(30.0, s.points[1].x);
    EXPECT_DOUBLE_EQ(10.0, s.points[1].y);
    EXPECT_DOUBLE_EQ(70.0, s.points[2].x);
    EXPECT_DOUBLE_EQ(90.0, s.points[2].y);
    EXPECT_DOUBLE_EQ(90.0, s.points[3].x);
    EXPECT_DOUBLE_EQ(50.0, s.points[3].y);
    EXPECT_DOUBLE_EQ(2.5, s.lineWidth);
    EXPECT_EQ("#ff0000", s.color);
}

TEST(DrawingXmlTest, RoundTrip_BezierCurve_EditedPoints)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    auto bez = makeBezierStroke("#0000ff", 3.0,
        10, 50,   30, 10,   70, 90,   90, 50);
    bez.points[1] = {45, 5};
    bez.points[2] = {55, 95};
    c.strokes.push_back(std::move(bez));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    const auto& s = result[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::BezierCurve, s.type);
    EXPECT_DOUBLE_EQ(45.0, s.points[1].x);
    EXPECT_DOUBLE_EQ(5.0, s.points[1].y);
    EXPECT_DOUBLE_EQ(55.0, s.points[2].x);
    EXPECT_DOUBLE_EQ(95.0, s.points[2].y);
}

// ── Multi-select stroke operations via CompoundCommand ────────────────────

TEST_F(DrawingCommandTest, MultiDelete_CompoundEraseDescending)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 1.0, {{1,1},{2,2}}));
    strokes.push_back(makeStroke("#00ff00", 2.0, {{3,3},{4,4}}));
    strokes.push_back(makeStroke("#0000ff", 3.0, {{5,5},{6,6}}));
    strokes.push_back(makeStroke("#ffff00", 4.0, {{7,7},{8,8}}));

    auto compound = std::make_unique<CompoundCommand>("Delete strokes");
    compound->setNodeId(1);

    // erase indices 1 and 3 in descending order
    compound->addCommand(std::make_unique<EraseStrokeCommand>(
        model, 1, 0, strokes[3], 3));
    compound->addCommand(std::make_unique<EraseStrokeCommand>(
        model, 1, 0, strokes[1], 1));

    compound->execute();
    ASSERT_EQ(2u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[1].color);

    compound->undo();
    ASSERT_EQ(4u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#00ff00", node->getDrawingCanvases()[0].strokes[1].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[2].color);
    EXPECT_EQ("#ffff00", node->getDrawingCanvases()[0].strokes[3].color);

    compound->redo();
    ASSERT_EQ(2u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[1].color);
}

TEST_F(DrawingCommandTest, MultiPaste_CompoundDrawStroke)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 1.0, {{1,1},{2,2}}));

    auto compound = std::make_unique<CompoundCommand>("Paste strokes");
    compound->setNodeId(1);
    compound->addCommand(std::make_unique<DrawStrokeCommand>(
        model, 1, 0, makeStroke("#00ff00", 2.0, {{10,10},{20,20}})));
    compound->addCommand(std::make_unique<DrawStrokeCommand>(
        model, 1, 0, makeStroke("#0000ff", 3.0, {{30,30},{40,40}})));

    compound->execute();
    ASSERT_EQ(3u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#00ff00", node->getDrawingCanvases()[0].strokes[1].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[2].color);

    compound->undo();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);

    compound->redo();
    ASSERT_EQ(3u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, MultiMove_CompoundMoveStroke)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 1.0, {{10,10},{20,20}}));
    strokes.push_back(makeStroke("#00ff00", 2.0, {{30,30},{40,40}}));

    auto origPts0 = strokes[0].points;
    auto origPts1 = strokes[1].points;
    auto newPts0 = origPts0;
    auto newPts1 = origPts1;
    for (auto& p : newPts0) { p.x += 50; p.y += 50; }
    for (auto& p : newPts1) { p.x += 50; p.y += 50; }

    auto compound = std::make_unique<CompoundCommand>("Move strokes");
    compound->setNodeId(1);
    compound->addCommand(std::make_unique<MoveStrokeCommand>(
        model, 1, 0, 0, origPts0, newPts0));
    compound->addCommand(std::make_unique<MoveStrokeCommand>(
        model, 1, 0, 1, origPts1, newPts1));

    compound->execute();
    EXPECT_DOUBLE_EQ(60.0, node->getDrawingCanvases()[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(80.0, node->getDrawingCanvases()[0].strokes[1].points[0].x);

    compound->undo();
    EXPECT_DOUBLE_EQ(10.0, node->getDrawingCanvases()[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(30.0, node->getDrawingCanvases()[0].strokes[1].points[0].x);

    compound->redo();
    EXPECT_DOUBLE_EQ(60.0, node->getDrawingCanvases()[0].strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(80.0, node->getDrawingCanvases()[0].strokes[1].points[0].x);
}

TEST_F(DrawingCommandTest, StrokeClipboard_SetGetClear)
{
    CtDrawingOverlay::clearStrokeClipboard();
    EXPECT_FALSE(CtDrawingOverlay::hasStrokeClipboard());

    std::vector<CtDrawingStroke> clipboard;
    clipboard.push_back(makeStroke("#ff0000", 2.0, {{1,1},{2,2}}));
    clipboard.push_back(makeStroke("#00ff00", 3.0, {{3,3},{4,4}}));
    CtDrawingOverlay::setStrokeClipboard(clipboard);

    ASSERT_TRUE(CtDrawingOverlay::hasStrokeClipboard());
    ASSERT_EQ(2u, CtDrawingOverlay::getStrokeClipboard().size());
    EXPECT_EQ("#ff0000", CtDrawingOverlay::getStrokeClipboard()[0].color);
    EXPECT_EQ("#00ff00", CtDrawingOverlay::getStrokeClipboard()[1].color);

    CtDrawingOverlay::clearStrokeClipboard();
    EXPECT_FALSE(CtDrawingOverlay::hasStrokeClipboard());
}

TEST_F(DrawingCommandTest, StrokeProperties_ChangeColor)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.color = "#0000ff";

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_DOUBLE_EQ(2.0, node->getDrawingCanvases()[0].strokes[0].lineWidth);

    cmd.undo();
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);

    cmd.redo();
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[0].color);
}

TEST_F(DrawingCommandTest, StrokeProperties_ChangeLineWidth)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.lineWidth = 8.0;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_DOUBLE_EQ(8.0, node->getDrawingCanvases()[0].strokes[0].lineWidth);
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);

    cmd.undo();
    EXPECT_DOUBLE_EQ(2.0, node->getDrawingCanvases()[0].strokes[0].lineWidth);
}

TEST_F(DrawingCommandTest, StrokeProperties_ChangeOpacity)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.opacity = 0.5;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_DOUBLE_EQ(0.5, node->getDrawingCanvases()[0].strokes[0].opacity);

    cmd.undo();
    EXPECT_DOUBLE_EQ(1.0, node->getDrawingCanvases()[0].strokes[0].opacity);
}

TEST_F(DrawingCommandTest, StrokeProperties_ChangeLineStyle)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.lineStyle = CtDrawingLineStyle::Dashed;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_EQ(CtDrawingLineStyle::Dashed, node->getDrawingCanvases()[0].strokes[0].lineStyle);

    cmd.undo();
    EXPECT_EQ(CtDrawingLineStyle::Solid, node->getDrawingCanvases()[0].strokes[0].lineStyle);
}

TEST_F(DrawingCommandTest, StrokeProperties_ChangeArrowHead)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto stroke = makeStroke("#ff0000", 2.0, {{10,10},{50,50}});
    stroke.type = CtDrawingElementType::Line;
    node->getDrawingCanvasesMut()[0].strokes.push_back(stroke);

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.arrowHead = CtDrawingArrowHead::Both;
    newStroke.arrowStyle = CtDrawingArrowStyle::Open;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_EQ(CtDrawingArrowHead::Both, node->getDrawingCanvases()[0].strokes[0].arrowHead);
    EXPECT_EQ(CtDrawingArrowStyle::Open, node->getDrawingCanvases()[0].strokes[0].arrowStyle);

    cmd.undo();
    EXPECT_EQ(CtDrawingArrowHead::None, node->getDrawingCanvases()[0].strokes[0].arrowHead);
    EXPECT_EQ(CtDrawingArrowStyle::Solid, node->getDrawingCanvases()[0].strokes[0].arrowStyle);
}

TEST_F(DrawingCommandTest, StrokeProperties_ChangeFilled)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto stroke = makeStroke("#ff0000", 2.0, {{10,10},{50,10},{50,50},{10,50}});
    stroke.type = CtDrawingElementType::Rectangle;
    node->getDrawingCanvasesMut()[0].strokes.push_back(stroke);

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.filled = true;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_TRUE(node->getDrawingCanvases()[0].strokes[0].filled);

    cmd.undo();
    EXPECT_FALSE(node->getDrawingCanvases()[0].strokes[0].filled);
}

TEST_F(DrawingCommandTest, StrokeProperties_MultiplePropsAtOnce)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.color = "#00ff00";
    newStroke.lineWidth = 5.0;
    newStroke.opacity = 0.75;
    newStroke.lineStyle = CtDrawingLineStyle::Dotted;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ("#00ff00", s.color);
    EXPECT_DOUBLE_EQ(5.0, s.lineWidth);
    EXPECT_DOUBLE_EQ(0.75, s.opacity);
    EXPECT_EQ(CtDrawingLineStyle::Dotted, s.lineStyle);

    cmd.undo();
    const auto& s2 = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ("#ff0000", s2.color);
    EXPECT_DOUBLE_EQ(2.0, s2.lineWidth);
    EXPECT_DOUBLE_EQ(1.0, s2.opacity);
    EXPECT_EQ(CtDrawingLineStyle::Solid, s2.lineStyle);
}

TEST_F(DrawingCommandTest, StrokeProperties_PointsPreserved)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,30},{30,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.color = "#0000ff";

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    const auto& pts = node->getDrawingCanvases()[0].strokes[0].points;
    ASSERT_EQ(3u, pts.size());
    EXPECT_DOUBLE_EQ(10.0, pts[0].x);
    EXPECT_DOUBLE_EQ(10.0, pts[0].y);
    EXPECT_DOUBLE_EQ(20.0, pts[1].x);
    EXPECT_DOUBLE_EQ(30.0, pts[1].y);
}

TEST_F(DrawingCommandTest, StrokeProperties_CompoundMultiStroke)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));
    strokes.push_back(makeStroke("#00ff00", 3.0, {{30,30},{40,40}}));
    strokes.push_back(makeStroke("#0000ff", 4.0, {{50,50},{60,60}}));

    auto compound = std::make_unique<CompoundCommand>("Change stroke properties");
    compound->setNodeId(1);

    for (size_t i = 0; i < 3; ++i) {
        CtDrawingStroke oldS = strokes[i];
        CtDrawingStroke newS = oldS;
        newS.lineWidth = 7.0;
        newS.opacity = 0.25;
        compound->addCommand(std::make_unique<StrokePropertiesCommand>(
            model, 1, 0, i, std::move(oldS), std::move(newS)));
    }

    compound->execute();
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(7.0, node->getDrawingCanvases()[0].strokes[i].lineWidth);
        EXPECT_DOUBLE_EQ(0.25, node->getDrawingCanvases()[0].strokes[i].opacity);
    }
    EXPECT_EQ("#ff0000", node->getDrawingCanvases()[0].strokes[0].color);
    EXPECT_EQ("#00ff00", node->getDrawingCanvases()[0].strokes[1].color);
    EXPECT_EQ("#0000ff", node->getDrawingCanvases()[0].strokes[2].color);

    compound->undo();
    EXPECT_DOUBLE_EQ(2.0, node->getDrawingCanvases()[0].strokes[0].lineWidth);
    EXPECT_DOUBLE_EQ(3.0, node->getDrawingCanvases()[0].strokes[1].lineWidth);
    EXPECT_DOUBLE_EQ(4.0, node->getDrawingCanvases()[0].strokes[2].lineWidth);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(1.0, node->getDrawingCanvases()[0].strokes[i].opacity);
    }

    compound->redo();
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(7.0, node->getDrawingCanvases()[0].strokes[i].lineWidth);
        EXPECT_DOUBLE_EQ(0.25, node->getDrawingCanvases()[0].strokes[i].opacity);
    }
}

TEST_F(DrawingCommandTest, StrokeProperties_NotifiesObserver)
{
    DrawingObserverLog log;
    model->addObserver(&log);

    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    node->getDrawingCanvasesMut()[0].strokes.push_back(
        makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.color = "#00ff00";

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    log.clear();
    cmd.execute();
    EXPECT_EQ(1, log.drawingChangedCount);
    EXPECT_EQ(1, log.lastDrawingNodeId);

    cmd.undo();
    EXPECT_EQ(2, log.drawingChangedCount);

    model->removeObserver(&log);
}

// ── CtDrawingPoint equality ────────────────────────────────────────────────

TEST(DrawingPointTest, Equality)
{
    CtDrawingPoint a{1.0, 2.0};
    CtDrawingPoint b{1.0, 2.0};
    CtDrawingPoint c{1.0, 3.0};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(DrawingPointTest, VectorEquality)
{
    std::vector<CtDrawingPoint> v1{{1.0,2.0},{3.0,4.0}};
    std::vector<CtDrawingPoint> v2{{1.0,2.0},{3.0,4.0}};
    std::vector<CtDrawingPoint> v3{{1.0,2.0},{3.0,5.0}};
    std::vector<CtDrawingPoint> v4{{1.0,2.0}};
    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
    EXPECT_NE(v1, v4);
}

// ── Group scale undo: StrokePropertiesCommand restores type ────────────────

TEST_F(DrawingCommandTest, StrokeProperties_RestoresTypeOnUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;

    CtDrawingStroke rect;
    rect.type = CtDrawingElementType::Rectangle;
    rect.color = "#000000";
    rect.lineWidth = 2.0;
    rect.rotation = M_PI / 4.0;
    rect.points = {{100,100},{200,200}};
    strokes.push_back(rect);

    CtDrawingStroke oldStroke = strokes[0];

    CtDrawingStroke newStroke = oldStroke;
    newStroke.type = CtDrawingElementType::Freehand;
    newStroke.points = {{120,80},{220,120},{200,220},{100,180},{120,80}};
    newStroke.rotation = 0.0;

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    EXPECT_EQ(CtDrawingElementType::Freehand, node->getDrawingCanvases()[0].strokes[0].type);
    EXPECT_EQ(5u, node->getDrawingCanvases()[0].strokes[0].points.size());
    EXPECT_DOUBLE_EQ(0.0, node->getDrawingCanvases()[0].strokes[0].rotation);

    cmd.undo();
    EXPECT_EQ(CtDrawingElementType::Rectangle, node->getDrawingCanvases()[0].strokes[0].type);
    EXPECT_EQ(2u, node->getDrawingCanvases()[0].strokes[0].points.size());
    EXPECT_DOUBLE_EQ(M_PI / 4.0, node->getDrawingCanvases()[0].strokes[0].rotation);
}

// ── Group scale undo: compound with type change + move + rotate ────────────

TEST_F(DrawingCommandTest, CompoundCommand_GroupScaleWithBakedBoxShape)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 400, 400));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;

    CtDrawingStroke triangle;
    triangle.type = CtDrawingElementType::Triangle;
    triangle.color = "#ff0000";
    triangle.lineWidth = 2.0;
    triangle.rotation = 0.5;
    triangle.points = {{50,50},{150,150}};
    strokes.push_back(triangle);

    CtDrawingStroke line;
    line.type = CtDrawingElementType::Freehand;
    line.color = "#0000ff";
    line.lineWidth = 2.0;
    line.rotation = 0.3;
    line.points = {{200,200},{250,210},{300,250}};
    strokes.push_back(line);

    CtDrawingStroke origTriangle = strokes[0];
    CtDrawingStroke origLine = strokes[1];

    auto compound = std::make_unique<CompoundCommand>("Scale strokes");
    compound->setNodeId(1);

    CtDrawingStroke newTriangle = origTriangle;
    newTriangle.type = CtDrawingElementType::Freehand;
    newTriangle.points = {{90,40},{45,160},{155,160},{90,40}};
    newTriangle.rotation = 0.0;
    compound->addCommand(std::make_unique<StrokePropertiesCommand>(
        model, 1, 0, 0, origTriangle, newTriangle));

    std::vector<CtDrawingPoint> newLinePts = {{180,180},{230,190},{280,230}};
    compound->addCommand(std::make_unique<MoveStrokeCommand>(
        model, 1, 0, 1, origLine.points, newLinePts));
    compound->addCommand(std::make_unique<RotateStrokeCommand>(
        model, 1, 0, 1, 0.3, 0.0));

    compound->execute();

    EXPECT_EQ(CtDrawingElementType::Freehand, strokes[0].type);
    EXPECT_EQ(4u, strokes[0].points.size());
    EXPECT_DOUBLE_EQ(0.0, strokes[0].rotation);
    EXPECT_EQ(3u, strokes[1].points.size());
    EXPECT_DOUBLE_EQ(0.0, strokes[1].rotation);

    compound->undo();

    EXPECT_EQ(CtDrawingElementType::Triangle, strokes[0].type);
    EXPECT_EQ(2u, strokes[0].points.size());
    EXPECT_DOUBLE_EQ(0.5, strokes[0].rotation);
    EXPECT_DOUBLE_EQ(50.0, strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(50.0, strokes[0].points[0].y);

    EXPECT_EQ(3u, strokes[1].points.size());
    EXPECT_DOUBLE_EQ(0.3, strokes[1].rotation);
    EXPECT_DOUBLE_EQ(200.0, strokes[1].points[0].x);
}

// ── Group scale undo: all 2-point shape types ──────────────────────────────

TEST_F(DrawingCommandTest, StrokeProperties_RestoresAllBoxShapeTypes)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 500, 500));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;

    struct ShapeTestCase {
        CtDrawingElementType type;
        size_t expectedBakedPoints;
    };
    std::vector<ShapeTestCase> cases = {
        {CtDrawingElementType::Rectangle, 5},
        {CtDrawingElementType::RoundedRectangle, 5},
        {CtDrawingElementType::Triangle, 4},
        {CtDrawingElementType::Diamond, 5},
        {CtDrawingElementType::Ellipse, 33},
    };

    for (size_t i = 0; i < cases.size(); ++i) {
        CtDrawingStroke s;
        s.type = cases[i].type;
        s.color = "#000000";
        s.lineWidth = 2.0;
        s.rotation = 0.7;
        s.points = {{10.0 + i * 100.0, 10.0}, {90.0 + i * 100.0, 90.0}};
        strokes.push_back(s);
    }

    for (size_t i = 0; i < cases.size(); ++i) {
        CtDrawingStroke oldStroke = strokes[i];

        CtDrawingStroke newStroke = oldStroke;
        newStroke.type = CtDrawingElementType::Freehand;
        newStroke.rotation = 0.0;
        newStroke.points.resize(cases[i].expectedBakedPoints, {0.0, 0.0});

        StrokePropertiesCommand cmd(model, 1, 0, i, oldStroke, newStroke);

        cmd.execute();
        EXPECT_EQ(CtDrawingElementType::Freehand, strokes[i].type)
            << "After execute, shape index " << i;
        EXPECT_EQ(cases[i].expectedBakedPoints, strokes[i].points.size())
            << "After execute, shape index " << i;

        cmd.undo();
        EXPECT_EQ(cases[i].type, strokes[i].type)
            << "After undo, shape index " << i;
        EXPECT_EQ(2u, strokes[i].points.size())
            << "After undo, shape index " << i;
        EXPECT_DOUBLE_EQ(0.7, strokes[i].rotation)
            << "After undo, shape index " << i;
    }
}

// ── getDrawingCanvasIdx tests ──────────────────────────────────────────────

TEST_F(DrawingCommandTest, DrawStrokeCommand_ReportsCanvasIdx)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    DrawStrokeCommand cmd(model, 1, 2, makeStroke("#000", 1.0, {{0,0},{1,1}}));
    EXPECT_EQ(2, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, EraseStrokeCommand_ReportsCanvasIdx)
{
    EraseStrokeCommand cmd(model, 1, 5, makeStroke("#000", 1.0, {{0,0}}), 0);
    EXPECT_EQ(5, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, RotateStrokeCommand_ReportsCanvasIdx)
{
    RotateStrokeCommand cmd(model, 1, 3, 0, 0.0, 1.0);
    EXPECT_EQ(3, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, MoveStrokeCommand_ReportsCanvasIdx)
{
    MoveStrokeCommand cmd(model, 1, 7, 0, {}, {});
    EXPECT_EQ(7, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, StrokePropertiesCommand_ReportsCanvasIdx)
{
    CtDrawingStroke s;
    StrokePropertiesCommand cmd(model, 1, 4, 0, s, s);
    EXPECT_EQ(4, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, AddCanvasCommand_ReportsCanvasIdx)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 100, 100));
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 100, 100, 100));

    AddCanvasCommand cmd(model, 1, makeCanvas(0, 200, 100, 100));
    cmd.execute();
    EXPECT_EQ(2, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, DeleteCanvasCommand_ReportsCanvasIdx)
{
    DeleteCanvasCommand cmd(model, 1, makeCanvas(0, 0, 100, 100), 3);
    EXPECT_EQ(3, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, MoveCanvasCommand_ReportsCanvasIdx)
{
    MoveCanvasCommand cmd(model, 1, 1, 0, 0, 10, 10);
    EXPECT_EQ(1, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, ResizeCanvasCommand_ReportsCanvasIdx)
{
    ResizeCanvasCommand cmd(model, 1, 6, 0, 0, 100, 100, 10, 10, 200, 200);
    EXPECT_EQ(6, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, CanvasPropertiesCommand_ReportsCanvasIdx)
{
    CanvasPropertiesCommand cmd(model, 1, 9, "", "", "#fff", "#000", 1.0, 0.5, 8.0, 4.0, false, true);
    EXPECT_EQ(9, cmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, BaseCommand_ReportsNoCanvasIdx)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    DrawStrokeCommand drawCmd(model, 1, 0, makeStroke("#000", 1.0, {{0,0},{1,1}}));
    CtCommand& base = drawCmd;
    EXPECT_EQ(0, base.getDrawingCanvasIdx());

    CompoundCommand textCmd("text edit");
    EXPECT_EQ(-1, textCmd.getDrawingCanvasIdx());
}

TEST_F(DrawingCommandTest, CompoundCommand_DrawingCanvasIdx)
{
    CompoundCommand compound("Move strokes");
    compound.setNodeId(1);
    EXPECT_EQ(-1, compound.getDrawingCanvasIdx());

    compound.setDrawingCanvasIdx(2);
    EXPECT_EQ(2, compound.getDrawingCanvasIdx());
}

// ── Arrow / DoubleArrow shape tests ─────────────────────────────────────────

TEST_F(DrawingCommandTest, DrawArrowStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeShapeStroke(CtDrawingElementType::Arrow, "#ff6600", 2.0, false,
                                   10, 20, 110, 80);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Arrow, s.type);
    EXPECT_FALSE(s.filled);
    EXPECT_EQ(2u, s.points.size());

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST_F(DrawingCommandTest, DrawDoubleArrowStroke_ExecuteAndUndo)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    auto stroke = makeShapeStroke(CtDrawingElementType::DoubleArrow, "#0066ff", 3.0, true,
                                   20, 30, 120, 90);
    DrawStrokeCommand cmd(model, 1, 0, stroke);

    cmd.execute();
    ASSERT_EQ(1u, node->getDrawingCanvases()[0].strokes.size());
    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::DoubleArrow, s.type);
    EXPECT_TRUE(s.filled);
    EXPECT_EQ(2u, s.points.size());

    cmd.undo();
    EXPECT_EQ(0u, node->getDrawingCanvases()[0].strokes.size());
}

TEST(DrawingXmlTest, RoundTrip_Arrow)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::Arrow, "#ff6600", 2.0, false,
                                         10, 20, 110, 80));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::Arrow, result[0].strokes[0].type);
    EXPECT_FALSE(result[0].strokes[0].filled);
    EXPECT_DOUBLE_EQ(10.0, result[0].strokes[0].points[0].x);
}

TEST(DrawingXmlTest, RoundTrip_DoubleArrow)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 300, 250);
    c.strokes.push_back(makeShapeStroke(CtDrawingElementType::DoubleArrow, "#0066ff", 3.0, true,
                                         20, 30, 120, 90));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_EQ(CtDrawingElementType::DoubleArrow, result[0].strokes[0].type);
    EXPECT_TRUE(result[0].strokes[0].filled);
}

// ── Arrow baking: rotated 2-point Arrow converts to Freehand polygon ────────

TEST_F(DrawingCommandTest, StrokeProperties_ArrowBakesToFreehandPolygon)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));

    CtDrawingStroke arrow;
    arrow.type = CtDrawingElementType::Arrow;
    arrow.color = "#ff0000";
    arrow.lineWidth = 2.0;
    arrow.opacity = 1.0;
    arrow.rotation = M_PI / 6.0;
    arrow.points = {{10, 20}, {110, 80}};
    node->getDrawingCanvasesMut()[0].strokes.push_back(arrow);

    CtDrawingStroke baked = arrow;
    double x0 = 10, y0 = 20, x1 = 110, y1 = 80;
    double bw = x1 - x0, bh = y1 - y0;
    double midY = (y0 + y1) / 2.0;
    double scx = (x0 + x1) / 2.0, scy = midY;
    double cosA = std::cos(arrow.rotation);
    double sinA = std::sin(arrow.rotation);

    std::vector<CtDrawingPoint> visualPts = {
        {x0, y0+bh*0.3}, {x0+bw*0.65, y0+bh*0.3}, {x0+bw*0.65, y0},
        {x1, midY}, {x0+bw*0.65, y1}, {x0+bw*0.65, y0+bh*0.7},
        {x0, y0+bh*0.7}
    };
    baked.points.resize(visualPts.size() + 1);
    for (size_t v = 0; v < visualPts.size(); ++v) {
        double dx = visualPts[v].x - scx;
        double dy = visualPts[v].y - scy;
        baked.points[v].x = scx + dx * cosA - dy * sinA;
        baked.points[v].y = scy + dx * sinA + dy * cosA;
    }
    baked.points[visualPts.size()] = baked.points[0];
    baked.type = CtDrawingElementType::Freehand;
    baked.rotation = 0.0;

    StrokePropertiesCommand cmd(model, 1, 0, 0, arrow, baked);
    cmd.execute();

    const auto& s = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Freehand, s.type);
    EXPECT_DOUBLE_EQ(0.0, s.rotation);
    EXPECT_EQ(8u, s.points.size());
    EXPECT_EQ(s.points.front(), s.points.back());

    cmd.undo();
    const auto& orig = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Arrow, orig.type);
    EXPECT_DOUBLE_EQ(M_PI / 6.0, orig.rotation);
    EXPECT_EQ(2u, orig.points.size());
}

// ── Closed freehand polygon supports fill ───────────────────────────────────

TEST(DrawingPointTest, ClosedPolygonDetection)
{
    CtDrawingStroke s;
    s.type = CtDrawingElementType::Freehand;
    s.points = {{10, 20}, {50, 60}, {90, 20}, {10, 20}};
    EXPECT_EQ(s.points.front(), s.points.back());
    EXPECT_EQ(4u, s.points.size());

    s.filled = true;
    EXPECT_TRUE(s.filled);
}

// ── Group operations undo as single step ────────────────────────────────────

TEST_F(DrawingCommandTest, CompoundCommand_GroupScaleUndoesInOneStep)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeShapeStroke(CtDrawingElementType::Rectangle, "#ff0000", 2.0, false, 10, 10, 50, 50));
    strokes.push_back(makeShapeStroke(CtDrawingElementType::Ellipse, "#00ff00", 2.0, false, 60, 10, 100, 50));
    strokes.push_back(makeShapeStroke(CtDrawingElementType::Triangle, "#0000ff", 2.0, false, 110, 10, 150, 50));

    CtCommandManager mgr;
    auto compound = std::make_unique<CompoundCommand>("Scale strokes");
    compound->setNodeId(1);
    compound->setDocumentModel(model);

    for (size_t i = 0; i < 3; ++i) {
        auto oldPts = strokes[i].points;
        std::vector<CtDrawingPoint> newPts;
        for (auto& p : oldPts) {
            newPts.push_back({p.x * 2.0, p.y * 2.0});
        }
        compound->addCommand(std::make_unique<MoveStrokeCommand>(
            model, 1, 0, i, oldPts, newPts));
    }

    mgr.executeCommand(std::move(compound));

    EXPECT_DOUBLE_EQ(20.0, strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(120.0, strokes[1].points[0].x);
    EXPECT_DOUBLE_EQ(220.0, strokes[2].points[0].x);

    EXPECT_TRUE(mgr.canUndo());
    mgr.undo();

    EXPECT_DOUBLE_EQ(10.0, strokes[0].points[0].x);
    EXPECT_DOUBLE_EQ(60.0, strokes[1].points[0].x);
    EXPECT_DOUBLE_EQ(110.0, strokes[2].points[0].x);
    EXPECT_FALSE(mgr.canUndo());

    mgr.redo();
    EXPECT_DOUBLE_EQ(20.0, strokes[0].points[0].x);
    EXPECT_FALSE(mgr.canRedo());
}

TEST_F(DrawingCommandTest, CompoundCommand_GroupDeleteUndoesInOneStep)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#ff0000", 2.0, {{10,10},{20,20}}));
    strokes.push_back(makeStroke("#00ff00", 2.0, {{30,30},{40,40}}));
    strokes.push_back(makeStroke("#0000ff", 2.0, {{50,50},{60,60}}));

    CtCommandManager mgr;
    auto compound = std::make_unique<CompoundCommand>("Delete strokes");
    compound->setNodeId(1);
    compound->setDocumentModel(model);

    for (int i = 2; i >= 0; --i) {
        compound->addCommand(std::make_unique<EraseStrokeCommand>(
            model, 1, 0, strokes[i], static_cast<size_t>(i)));
    }

    mgr.executeCommand(std::move(compound));
    EXPECT_EQ(0u, strokes.size());

    mgr.undo();
    ASSERT_EQ(3u, strokes.size());
    EXPECT_EQ("#ff0000", strokes[0].color);
    EXPECT_EQ("#00ff00", strokes[1].color);
    EXPECT_EQ("#0000ff", strokes[2].color);
    EXPECT_FALSE(mgr.canUndo());
}

TEST_F(DrawingCommandTest, CompoundCommand_GroupPropertyChangeUndoesInOneStep)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 300, 250));
    auto& strokes = node->getDrawingCanvasesMut()[0].strokes;
    strokes.push_back(makeStroke("#000000", 2.0, {{10,10},{20,20}}));
    strokes.push_back(makeStroke("#000000", 2.0, {{30,30},{40,40}}));

    CtCommandManager mgr;
    auto compound = std::make_unique<CompoundCommand>("Change stroke properties");
    compound->setNodeId(1);
    compound->setDocumentModel(model);

    for (size_t i = 0; i < 2; ++i) {
        CtDrawingStroke oldStroke = strokes[i];
        CtDrawingStroke newStroke = oldStroke;
        newStroke.filled = true;
        compound->addCommand(std::make_unique<StrokePropertiesCommand>(
            model, 1, 0, i, std::move(oldStroke), std::move(newStroke)));
    }

    mgr.executeCommand(std::move(compound));
    EXPECT_TRUE(strokes[0].filled);
    EXPECT_TRUE(strokes[1].filled);

    mgr.undo();
    EXPECT_FALSE(strokes[0].filled);
    EXPECT_FALSE(strokes[1].filled);
    EXPECT_FALSE(mgr.canUndo());
}

TEST_F(DrawingCommandTest, AllDrawingCommands_ShowNodeIdInUndoRedoList)
{
    const gint64 nodeId = 1;
    auto node = model->getNodeById(nodeId);
    auto canvas = makeCanvas(0, 0, 300, 250);
    canvas.strokes.push_back(makeStroke("#000000", 2.0, {{10,10},{20,20}}));
    node->getDrawingCanvasesMut().push_back(std::move(canvas));

    CtCommandManager mgr;
    const std::string prefix = "[" + std::to_string(nodeId) + "] ";

    mgr.executeCommand(std::make_unique<DrawStrokeCommand>(
        model, nodeId, 0, makeStroke("#ff0000", 1.0, {{0,0},{5,5}})));

    mgr.executeCommand(std::make_unique<EraseStrokeCommand>(
        model, nodeId, 0,
        node->getDrawingCanvasesMut()[0].strokes.back(),
        node->getDrawingCanvasesMut()[0].strokes.size() - 1));

    mgr.executeCommand(std::make_unique<RotateStrokeCommand>(
        model, nodeId, 0, 0, 0.0, 1.5));

    mgr.executeCommand(std::make_unique<MoveStrokeCommand>(
        model, nodeId, 0, 0,
        node->getDrawingCanvasesMut()[0].strokes[0].points,
        std::vector<CtDrawingPoint>{{15,15},{25,25}}));

    CtDrawingStroke oldStroke = node->getDrawingCanvasesMut()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.filled = true;
    mgr.executeCommand(std::make_unique<StrokePropertiesCommand>(
        model, nodeId, 0, 0, std::move(oldStroke), std::move(newStroke)));

    mgr.executeCommand(std::make_unique<AddCanvasCommand>(
        model, nodeId, makeCanvas(10, 10, 100, 80)));

    mgr.executeCommand(std::make_unique<DeleteCanvasCommand>(
        model, nodeId, node->getDrawingCanvasesMut().back(),
        node->getDrawingCanvasesMut().size() - 1));

    mgr.executeCommand(std::make_unique<MoveCanvasCommand>(
        model, nodeId, 0, 0.0, 0.0, 50.0, 50.0));

    mgr.executeCommand(std::make_unique<ResizeCanvasCommand>(
        model, nodeId, 0, 50.0, 50.0, 300.0, 250.0, 60.0, 60.0, 400.0, 300.0));

    mgr.executeCommand(std::make_unique<CanvasPropertiesCommand>(
        model, nodeId, 0, "", "test", "", "#ffffff", 1.0, 0.8, 8.0, 12.0, false, true));

    auto compound = std::make_unique<CompoundCommand>("Group operation");
    compound->setNodeId(nodeId);
    compound->addCommand(std::make_unique<DrawStrokeCommand>(
        model, nodeId, 0, makeStroke("#00ff00", 1.0, {{1,1},{2,2}})));
    mgr.executeCommand(std::move(compound));

    auto undoDescs = mgr.getUndoStackDescriptions();
    ASSERT_EQ(11u, undoDescs.size());
    for (size_t i = 0; i < undoDescs.size(); ++i) {
        EXPECT_EQ(0u, undoDescs[i].find(prefix))
            << "Undo entry missing node ID prefix: \"" << undoDescs[i] << "\"";
    }

    while (mgr.canUndo()) mgr.undo();

    auto redoDescs = mgr.getRedoStackDescriptions();
    ASSERT_EQ(11u, redoDescs.size());
    for (size_t i = 0; i < redoDescs.size(); ++i) {
        EXPECT_EQ(0u, redoDescs[i].find(prefix))
            << "Redo entry missing node ID prefix: \"" << redoDescs[i] << "\"";
    }
}

// ── CtFontTransform tests ──────────────────────────────────────────────────

TEST(FontTransformTest, Identity)
{
    CtFontTransform ft;
    EXPECT_TRUE(ft.isIdentity());
    EXPECT_DOUBLE_EQ(1.0, ft.a);
    EXPECT_DOUBLE_EQ(0.0, ft.b);
    EXPECT_DOUBLE_EQ(0.0, ft.c);
    EXPECT_DOUBLE_EQ(1.0, ft.d);
}

TEST(FontTransformTest, NonIdentity)
{
    CtFontTransform ft;
    ft.a = 2.0;
    EXPECT_FALSE(ft.isIdentity());
}

TEST(FontTransformTest, Equality)
{
    CtFontTransform a, b;
    EXPECT_EQ(a, b);
    b.a = 1.5;
    EXPECT_NE(a, b);
    a.a = 1.5;
    EXPECT_EQ(a, b);
}

// ── FontTransform XML round-trip ───────────────────────────────────────────

TEST(DrawingXmlTest, RoundTrip_FontTransform)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 400, 300);
    CtDrawingStroke s;
    s.type = CtDrawingElementType::Text;
    s.color = "#000000";
    s.lineWidth = 1.0;
    s.opacity = 1.0;
    s.textContent = "Hello";
    s.fontFamily = "Serif";
    s.fontSize = 18.0;
    s.rotation = 0.5;
    s.fontTransform = {1.5, 0.1, 0.2, 0.8};
    s.points.push_back({50, 60});
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);
    auto result = CtXmlHelper::drawing_canvases_from_xml(root);

    ASSERT_EQ(1u, result.size());
    ASSERT_EQ(1u, result[0].strokes.size());
    const auto& rs = result[0].strokes[0];
    EXPECT_EQ(CtDrawingElementType::Text, rs.type);
    EXPECT_EQ("Hello", rs.textContent);
    EXPECT_EQ("Serif", rs.fontFamily);
    EXPECT_DOUBLE_EQ(18.0, rs.fontSize);
    EXPECT_NEAR(0.5, rs.rotation, 1e-6);
    EXPECT_NEAR(1.5, rs.fontTransform.a, 1e-6);
    EXPECT_NEAR(0.1, rs.fontTransform.b, 1e-6);
    EXPECT_NEAR(0.2, rs.fontTransform.c, 1e-6);
    EXPECT_NEAR(0.8, rs.fontTransform.d, 1e-6);
}

TEST(DrawingXmlTest, RoundTrip_FontTransformIdentity_NotWritten)
{
    std::vector<CtDrawingCanvas> canvases;
    auto c = makeCanvas(0, 0, 400, 300);
    CtDrawingStroke s;
    s.type = CtDrawingElementType::Text;
    s.color = "#000000";
    s.lineWidth = 1.0;
    s.opacity = 1.0;
    s.textContent = "Test";
    s.points.push_back({10, 20});
    c.strokes.push_back(std::move(s));
    canvases.push_back(std::move(c));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);

    std::string xml = doc.write_to_string();
    EXPECT_EQ(std::string::npos, xml.find("font_tf_a"));

    auto result = CtXmlHelper::drawing_canvases_from_xml(root);
    ASSERT_EQ(1u, result.size());
    ASSERT_EQ(1u, result[0].strokes.size());
    EXPECT_TRUE(result[0].strokes[0].fontTransform.isIdentity());
}

// ── FontTransform StrokePropertiesCommand ──────────────────────────────────

TEST_F(DrawingCommandTest, StrokeProperties_ChangeFontTransform)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0, 400, 300));
    CtDrawingStroke s;
    s.type = CtDrawingElementType::Text;
    s.color = "#000000";
    s.lineWidth = 1.0;
    s.opacity = 1.0;
    s.textContent = "Scale me";
    s.points.push_back({50, 60});
    node->getDrawingCanvasesMut()[0].strokes.push_back(std::move(s));

    CtDrawingStroke oldStroke = node->getDrawingCanvases()[0].strokes[0];
    CtDrawingStroke newStroke = oldStroke;
    newStroke.fontTransform = {2.0, 0.3, -0.1, 1.5};
    newStroke.points[0] = {70, 80};

    StrokePropertiesCommand cmd(model, 1, 0, 0, oldStroke, newStroke);

    cmd.execute();
    const auto& after = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_NEAR(2.0, after.fontTransform.a, 1e-9);
    EXPECT_NEAR(0.3, after.fontTransform.b, 1e-9);
    EXPECT_NEAR(-0.1, after.fontTransform.c, 1e-9);
    EXPECT_NEAR(1.5, after.fontTransform.d, 1e-9);
    EXPECT_NEAR(70.0, after.points[0].x, 1e-9);
    EXPECT_NEAR(80.0, after.points[0].y, 1e-9);

    cmd.undo();
    const auto& reverted = node->getDrawingCanvases()[0].strokes[0];
    EXPECT_TRUE(reverted.fontTransform.isIdentity());
    EXPECT_NEAR(50.0, reverted.points[0].x, 1e-9);
    EXPECT_NEAR(60.0, reverted.points[0].y, 1e-9);
}

// ── SQLite INSERT with explicit column names vs wrong column order ─────────

TEST(DrawingSqliteMigrationTest, InsertWithExplicitColumns_WrongPhysicalOrder)
{
    sqlite3* db = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));

    auto sqlExec = [&](const char* sql) {
        char* err = nullptr;
        sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    };

    sqlExec("CREATE TABLE drawing_stroke ("
            "node_id INTEGER, canvas_index INTEGER, stroke_index INTEGER,"
            "color TEXT, width REAL, opacity REAL DEFAULT 1.0, points TEXT,"
            "PRIMARY KEY (node_id, canvas_index, stroke_index))");

    // Simulate the OLD (wrong) migration order: font_tf before line_style
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN element_type INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN filled INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN text_content TEXT DEFAULT ''");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_family TEXT DEFAULT 'Sans'");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_size REAL DEFAULT 14.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_a REAL DEFAULT 1.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_b REAL DEFAULT 0.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_c REAL DEFAULT 0.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_d REAL DEFAULT 1.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN line_style INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN rotation REAL DEFAULT 0.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN arrow_head INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN arrow_style INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN fill_color TEXT DEFAULT '#ffffff'");

    // INSERT using explicit column names (as the fixed code does)
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(db,
        "INSERT INTO drawing_stroke(node_id,canvas_index,stroke_index,"
        "color,width,opacity,points,element_type,filled,text_content,font_family,font_size,"
        "line_style,rotation,arrow_head,arrow_style,fill_color,"
        "font_tf_a,font_tf_b,font_tf_c,font_tf_d) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, nullptr));

    sqlite3_bind_int64(stmt, 1, 1);
    sqlite3_bind_int(stmt, 2, 0);
    sqlite3_bind_int(stmt, 3, 0);
    sqlite3_bind_text(stmt, 4, "#ff0000", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, 2.0);
    sqlite3_bind_double(stmt, 6, 0.8);
    sqlite3_bind_text(stmt, 7, "10.0,20.0;30.0,40.0", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, 7);     // element_type = Text
    sqlite3_bind_int(stmt, 9, 0);
    sqlite3_bind_text(stmt, 10, "Hello", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, "Serif", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 12, 18.0);
    sqlite3_bind_int(stmt, 13, 2);    // line_style
    sqlite3_bind_double(stmt, 14, 0.5); // rotation
    sqlite3_bind_int(stmt, 15, 1);    // arrow_head
    sqlite3_bind_int(stmt, 16, 0);    // arrow_style
    sqlite3_bind_text(stmt, 17, "#00ff00", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 18, 1.5); // font_tf_a
    sqlite3_bind_double(stmt, 19, 0.1); // font_tf_b
    sqlite3_bind_double(stmt, 20, 0.2); // font_tf_c
    sqlite3_bind_double(stmt, 21, 0.8); // font_tf_d
    ASSERT_EQ(SQLITE_DONE, sqlite3_step(stmt));
    sqlite3_finalize(stmt);

    // Read back by column name — should get correct values
    sqlite3_stmt* sel = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT color, rotation, fill_color, font_tf_a, font_tf_b, font_tf_c, font_tf_d, line_style, text_content "
        "FROM drawing_stroke WHERE node_id=1",
        -1, &sel, nullptr);
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(sel));

    EXPECT_STREQ("#ff0000", reinterpret_cast<const char*>(sqlite3_column_text(sel, 0)));
    EXPECT_DOUBLE_EQ(0.5, sqlite3_column_double(sel, 1));
    EXPECT_STREQ("#00ff00", reinterpret_cast<const char*>(sqlite3_column_text(sel, 2)));
    EXPECT_DOUBLE_EQ(1.5, sqlite3_column_double(sel, 3));
    EXPECT_DOUBLE_EQ(0.1, sqlite3_column_double(sel, 4));
    EXPECT_DOUBLE_EQ(0.2, sqlite3_column_double(sel, 5));
    EXPECT_DOUBLE_EQ(0.8, sqlite3_column_double(sel, 6));
    EXPECT_EQ(2, sqlite3_column_int(sel, 7));
    EXPECT_STREQ("Hello", reinterpret_cast<const char*>(sqlite3_column_text(sel, 8)));

    sqlite3_finalize(sel);
    sqlite3_close(db);
}

TEST(DrawingSqliteMigrationTest, PositionalInsert_WrongOnMigratedDB)
{
    sqlite3* db = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));

    auto sqlExec = [&](const char* sql) {
        char* err = nullptr;
        sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    };

    sqlExec("CREATE TABLE drawing_stroke ("
            "node_id INTEGER, canvas_index INTEGER, stroke_index INTEGER,"
            "color TEXT, width REAL, opacity REAL DEFAULT 1.0, points TEXT,"
            "PRIMARY KEY (node_id, canvas_index, stroke_index))");

    // Wrong migration order (font_tf before line_style)
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN element_type INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN filled INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN text_content TEXT DEFAULT ''");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_family TEXT DEFAULT 'Sans'");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_size REAL DEFAULT 14.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_a REAL DEFAULT 1.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_b REAL DEFAULT 0.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_c REAL DEFAULT 0.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN font_tf_d REAL DEFAULT 1.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN line_style INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN rotation REAL DEFAULT 0.0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN arrow_head INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN arrow_style INTEGER DEFAULT 0");
    sqlExec("ALTER TABLE drawing_stroke ADD COLUMN fill_color TEXT DEFAULT '#ffffff'");

    // Positional INSERT (the old bug): puts rotation value into font_tf_b column
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(db,
        "INSERT INTO drawing_stroke VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, nullptr));
    sqlite3_bind_int64(stmt, 1, 1);
    sqlite3_bind_int(stmt, 2, 0);
    sqlite3_bind_int(stmt, 3, 0);
    sqlite3_bind_text(stmt, 4, "#ff0000", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, 2.0);
    sqlite3_bind_double(stmt, 6, 1.0);
    sqlite3_bind_text(stmt, 7, "10,20", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, 0);
    sqlite3_bind_int(stmt, 9, 0);
    sqlite3_bind_text(stmt, 10, "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, "Sans", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 12, 14.0);
    sqlite3_bind_int(stmt, 13, 2);       // intended: line_style=2
    sqlite3_bind_double(stmt, 14, 0.5);  // intended: rotation=0.5
    sqlite3_bind_int(stmt, 15, 0);
    sqlite3_bind_int(stmt, 16, 0);
    sqlite3_bind_text(stmt, 17, "#00ff00", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 18, 1.0);
    sqlite3_bind_double(stmt, 19, 0.0);
    sqlite3_bind_double(stmt, 20, 0.0);
    sqlite3_bind_double(stmt, 21, 1.0);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Read back — rotation and line_style should be in the WRONG columns
    sqlite3_stmt* sel = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT rotation, line_style, font_tf_a FROM drawing_stroke WHERE node_id=1",
        -1, &sel, nullptr);
    ASSERT_EQ(SQLITE_ROW, sqlite3_step(sel));

    // With positional INSERT on wrong-order table:
    // position 13 (line_style=2) went into font_tf_a column
    // position 14 (rotation=0.5) went into font_tf_b column
    // So rotation column got font_tf_a value (1.0), not 0.5
    EXPECT_NE(0.5, sqlite3_column_double(sel, 0));
    // And font_tf_a column got line_style value (2), not 1.0
    EXPECT_NE(1.0, sqlite3_column_double(sel, 2));

    sqlite3_finalize(sel);
    sqlite3_close(db);
}

// ── Scroll extent computation tests ────────────────────────────────────────
// These verify the pure computation that updateScrollableExtent() performs:
// given canvas positions/sizes and a zoom factor, compute the required
// maxRight/maxBottom and the needed bottom margin / horizontal size request.

static constexpr double SCROLL_MARGIN = 50.0;

static void computeScrollExtent(const std::vector<CtDrawingCanvas>& canvases,
                                double zoom,
                                double& maxRight, double& maxBottom)
{
    maxRight = 0.0;
    maxBottom = 0.0;
    for (const auto& c : canvases) {
        double r = (c.x + c.width) * zoom;
        double b = (c.y + c.height) * zoom;
        if (r > maxRight) maxRight = r;
        if (b > maxBottom) maxBottom = b;
    }
    maxRight += SCROLL_MARGIN;
    maxBottom += SCROLL_MARGIN;
}

TEST(ScrollExtent, EmptyCanvases_NoExtent)
{
    std::vector<CtDrawingCanvas> canvases;
    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);
    EXPECT_DOUBLE_EQ(SCROLL_MARGIN, maxRight);
    EXPECT_DOUBLE_EQ(SCROLL_MARGIN, maxBottom);
}

TEST(ScrollExtent, SingleCanvasWithinViewport)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(10, 20, 300, 250));

    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);

    EXPECT_DOUBLE_EQ(10 + 300 + SCROLL_MARGIN, maxRight);
    EXPECT_DOUBLE_EQ(20 + 250 + SCROLL_MARGIN, maxBottom);

    double viewportWidth = 800.0;
    double textHeight = 600.0;
    EXPECT_LT(maxRight, viewportWidth);
    EXPECT_LT(maxBottom, textHeight);
}

TEST(ScrollExtent, CanvasExtendsBeyondViewportRight)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(600, 20, 400, 250));

    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);

    EXPECT_DOUBLE_EQ(600 + 400 + SCROLL_MARGIN, maxRight);

    double viewportWidth = 800.0;
    EXPECT_GT(maxRight, viewportWidth);

    int sizeRequest = static_cast<int>(maxRight);
    EXPECT_EQ(1050, sizeRequest);
}

TEST(ScrollExtent, CanvasExtendsBeyondViewportBottom)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(10, 500, 300, 400));

    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);

    EXPECT_DOUBLE_EQ(500 + 400 + SCROLL_MARGIN, maxBottom);

    double textHeight = 200.0;
    int neededMargin = static_cast<int>(maxBottom - textHeight);
    EXPECT_EQ(750, neededMargin);
}

TEST(ScrollExtent, MultipleCanvases_UseFarthestExtent)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(10, 20, 300, 250));
    canvases.push_back(makeCanvas(500, 100, 600, 200));
    canvases.push_back(makeCanvas(50, 800, 200, 300));

    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);

    EXPECT_DOUBLE_EQ(500 + 600 + SCROLL_MARGIN, maxRight);
    EXPECT_DOUBLE_EQ(800 + 300 + SCROLL_MARGIN, maxBottom);
}

TEST(ScrollExtent, ZoomScalesExtent)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(100, 200, 300, 250));

    double maxRight1 = 0, maxBottom1 = 0;
    computeScrollExtent(canvases, 1.0, maxRight1, maxBottom1);

    double maxRight2 = 0, maxBottom2 = 0;
    computeScrollExtent(canvases, 2.0, maxRight2, maxBottom2);

    EXPECT_DOUBLE_EQ((100 + 300) * 2.0 + SCROLL_MARGIN, maxRight2);
    EXPECT_DOUBLE_EQ((200 + 250) * 2.0 + SCROLL_MARGIN, maxBottom2);
    EXPECT_GT(maxRight2, maxRight1);
    EXPECT_GT(maxBottom2, maxBottom1);
}

TEST(ScrollExtent, BottomMarginCalculation)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(10, 100, 300, 500));

    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);

    double textHeight = 200.0;
    int neededMargin = 0;
    if (maxBottom > textHeight) {
        neededMargin = static_cast<int>(maxBottom - textHeight);
    }
    EXPECT_EQ(static_cast<int>(100 + 500 + SCROLL_MARGIN - textHeight), neededMargin);
    EXPECT_GT(neededMargin, 0);

    textHeight = 1000.0;
    neededMargin = 0;
    if (maxBottom > textHeight) {
        neededMargin = static_cast<int>(maxBottom - textHeight);
    }
    EXPECT_EQ(0, neededMargin);
}

TEST(ScrollExtent, HorizontalSizeRequestDecision)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(700, 20, 400, 250));

    double maxRight = 0, maxBottom = 0;
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);

    double viewportWidth = 800.0;

    bool needsHorizontalScroll = (maxRight > viewportWidth && viewportWidth > 1);
    EXPECT_TRUE(needsHorizontalScroll);
    EXPECT_EQ(static_cast<int>(maxRight), 1150);

    canvases.clear();
    canvases.push_back(makeCanvas(10, 20, 300, 250));
    computeScrollExtent(canvases, 1.0, maxRight, maxBottom);
    needsHorizontalScroll = (maxRight > viewportWidth && viewportWidth > 1);
    EXPECT_FALSE(needsHorizontalScroll);
}

// ── Image paste zoom compensation ──────────────────────────────────────────────

TEST(ImageZoomCompensation, AtZoom100_NoChange)
{
    const int origW = 520, origH = 511;
    const double zoomSf = 1.0;
    int w = origW, h = origH;
    if (std::abs(zoomSf - 1.0) > 0.001) {
        w = std::max(1, (int)std::round(origW / zoomSf));
        h = std::max(1, (int)std::round(origH / zoomSf));
    }
    EXPECT_EQ(origW, w);
    EXPECT_EQ(origH, h);
}

TEST(ImageZoomCompensation, AtZoom138_ScalesDown)
{
    const int origW = 520, origH = 511;
    const double zoomSf = 1.375;
    int w = std::max(1, (int)std::round(origW / zoomSf));
    int h = std::max(1, (int)std::round(origH / zoomSf));
    EXPECT_EQ(378, w);
    EXPECT_EQ(372, h);
    // After apply_zoom the displayed size should approximate the original
    int displayW = (int)(w * zoomSf);
    int displayH = (int)(h * zoomSf);
    EXPECT_NEAR(origW, displayW, 2);
    EXPECT_NEAR(origH, displayH, 2);
}

TEST(ImageZoomCompensation, AtZoom200_HalvesSize)
{
    const int origW = 800, origH = 600;
    const double zoomSf = 2.0;
    int w = std::max(1, (int)std::round(origW / zoomSf));
    int h = std::max(1, (int)std::round(origH / zoomSf));
    EXPECT_EQ(400, w);
    EXPECT_EQ(300, h);
    int displayW = (int)(w * zoomSf);
    int displayH = (int)(h * zoomSf);
    EXPECT_EQ(origW, displayW);
    EXPECT_EQ(origH, displayH);
}

TEST(ImageZoomCompensation, SmallImage_NeverZero)
{
    const int origW = 1, origH = 1;
    const double zoomSf = 3.0;
    int w = std::max(1, (int)std::round(origW / zoomSf));
    int h = std::max(1, (int)std::round(origH / zoomSf));
    EXPECT_GE(w, 1);
    EXPECT_GE(h, 1);
}

// ── Canvas clipboard clear callback ────────────────────────────────────────────

TEST_F(DrawingCommandTest, ClipboardClear_OnNewCopy_ResetsHasClipboard)
{
    CtDrawingOverlay::setClipboard(makeCanvas(10, 20, 300, 250));
    EXPECT_TRUE(CtDrawingOverlay::hasClipboard());
    // Simulates what the GTK clear callback does when another operation
    // claims the clipboard — _clipboard.reset() is now called.
    CtDrawingOverlay::clearClipboard();
    EXPECT_FALSE(CtDrawingOverlay::hasClipboard());
}
