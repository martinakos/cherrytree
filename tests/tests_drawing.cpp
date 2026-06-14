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
