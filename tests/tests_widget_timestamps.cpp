/*
 * tests_widget_timestamps.cpp
 *
 * Tests for widget creation/modification timestamps across all widget types,
 * including undo/redo, legacy data without timestamps, and XML/model round-trips.
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
#include "ct_node_content.h"
#include "ct_widget_commands.h"
#include "ct_drawing.h"
#include "ct_drawing_commands.h"
#include "ct_storage_xml.h"
#include "gtest/gtest.h"
#include <libxml++/libxml++.h>
#include <ctime>
#include <thread>
#include <chrono>

// ── Helpers ─────────────────────────────────────────────────────────────────

static CtWidgetDesc makeWidget(CtAnchWidgType type, gint64 tsCr = 0, gint64 tsLs = 0)
{
    CtWidgetDesc w(type);
    w.setProperty("char_offset", "0");
    if (tsCr != 0) w.setProperty("ts_creation", std::to_string(tsCr));
    if (tsLs != 0) w.setProperty("ts_lastsave", std::to_string(tsLs));
    return w;
}

static CtWidgetDesc makeCodebox(gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::CodeBox, tsCr, tsLs);
    w.setProperty("syntax_highlighting", "python3");
    w.setProperty("frame_width", "700");
    w.setProperty("frame_height", "100");
    w.setProperty("width_in_pixels", "1");
    w.setProperty("highlight_brackets", "0");
    w.setProperty("show_line_numbers", "0");
    w.contentData = "print('hello')";
    return w;
}

static CtWidgetDesc makeImage(gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::ImagePng, tsCr, tsLs);
    w.setProperty("link", "");
    w.contentData = "PNGDATA";
    return w;
}

static CtWidgetDesc makeAnchor(const std::string& name, gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::ImageAnchor, tsCr, tsLs);
    w.setProperty("anchor", name);
    return w;
}

static CtWidgetDesc makeEmbFile(const std::string& filename, gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::ImageEmbFile, tsCr, tsLs);
    w.setProperty("filename", filename);
    w.setProperty("time", "0");
    w.contentData = "FILEBLOB";
    return w;
}

static CtWidgetDesc makeLatex(gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::ImageLatex, tsCr, tsLs);
    w.contentData = "\\frac{1}{2}";
    return w;
}

static CtWidgetDesc makeLightTable(gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::TableLight, tsCr, tsLs);
    w.setProperty("col_max", "60");
    w.tableData = {{"A", "B"}, {"C", "D"}};
    return w;
}

static CtWidgetDesc makeRichTable(gint64 tsCr = 0, gint64 tsLs = 0)
{
    auto w = makeWidget(CtAnchWidgType::TableRich, tsCr, tsLs);
    w.setProperty("col_max", "60");
    CtCellContent cellA; cellA.textSpans.emplace_back("A");
    CtCellContent cellB; cellB.textSpans.emplace_back("B");
    w.richTableData = {{cellA, cellB}};
    return w;
}

static CtDrawingCanvas makeCanvas(gint64 tsCr = 0, gint64 tsLs = 0)
{
    CtDrawingCanvas c;
    c.x = 10; c.y = 20; c.width = 300; c.height = 250;
    c.tsCreation = tsCr;
    c.tsLastSave = tsLs;
    return c;
}

static CtDrawingStroke makeStroke()
{
    CtDrawingStroke s;
    s.color = "#000000";
    s.lineWidth = 2.0;
    s.points = {{5.0, 5.0}, {10.0, 10.0}};
    return s;
}

// ── Fixture: model with one node ────────────────────────────────────────────

class WidgetTimestampTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = std::make_shared<CtDocumentModel>();
        auto node = model->createNode(1);
        node->setName("TestNode");
        model->addNode(node, 0, -1);
    }

    void insertTextAndWidget(CtWidgetDesc widget) {
        auto node = model->getNodeById(1);
        std::map<std::string, std::string> attrs;
        node->getContent().insertText(0, "Hello", attrs);
        widget.setProperty("char_offset", "5");
        node->getContent().insertWidget(5, widget);
    }

    std::shared_ptr<CtDocumentModel> model;
};

// ═══════════════════════════════════════════════════════════════════════════
// 1. CtWidgetDesc: timestamp storage and accessors
// ═══════════════════════════════════════════════════════════════════════════

TEST(WidgetDescTimestamp, DefaultTimestampsAreZero)
{
    CtWidgetDesc w(CtAnchWidgType::ImagePng);
    EXPECT_EQ(0, w.getTsCreation());
    EXPECT_EQ(0, w.getTsLastSave());
}

TEST(WidgetDescTimestamp, SetAndGetTimestamps)
{
    CtWidgetDesc w(CtAnchWidgType::CodeBox);
    w.setProperty("ts_creation", "1700000000");
    w.setProperty("ts_lastsave", "1700001000");
    EXPECT_EQ(1700000000, w.getTsCreation());
    EXPECT_EQ(1700001000, w.getTsLastSave());
}

TEST(WidgetDescTimestamp, TimestampsPreservedInEquality)
{
    CtWidgetDesc a(CtAnchWidgType::ImagePng);
    a.setProperty("ts_creation", "100");
    a.setProperty("ts_lastsave", "200");

    CtWidgetDesc b(CtAnchWidgType::ImagePng);
    b.setProperty("ts_creation", "100");
    b.setProperty("ts_lastsave", "200");

    EXPECT_EQ(a, b);

    b.setProperty("ts_lastsave", "300");
    EXPECT_FALSE(a == b);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. All widget types: timestamps stored and retrieved from model
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, Image_TimestampsStoredInModel)
{
    auto w = makeImage(1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, Anchor_TimestampsStoredInModel)
{
    auto w = makeAnchor("myanchor", 1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, EmbFile_TimestampsStoredInModel)
{
    auto w = makeEmbFile("notes.txt", 1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, Latex_TimestampsStoredInModel)
{
    auto w = makeLatex(1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, Codebox_TimestampsStoredInModel)
{
    auto w = makeCodebox(1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, LightTable_TimestampsStoredInModel)
{
    auto w = makeLightTable(1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, RichTable_TimestampsStoredInModel)
{
    auto w = makeRichTable(1000, 2000);
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, DrawingCanvas_TimestampsStoredInModel)
{
    auto canvas = makeCanvas(1000, 2000);
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(canvas);
    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Legacy widgets without timestamps default to zero (dashes in UI)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, NoTimestampProperties_DefaultToZero)
{
    CtWidgetDesc w(CtAnchWidgType::ImagePng);
    w.setProperty("char_offset", "0");
    w.setProperty("link", "");
    w.contentData = "PNG";
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_EQ(0, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, Codebox_NoTimestampProperties_DefaultToZero)
{
    CtWidgetDesc w(CtAnchWidgType::CodeBox);
    w.setProperty("char_offset", "0");
    w.setProperty("syntax_highlighting", "plain_text");
    w.setProperty("frame_width", "700");
    w.setProperty("frame_height", "100");
    w.setProperty("width_in_pixels", "1");
    w.setProperty("highlight_brackets", "0");
    w.setProperty("show_line_numbers", "0");
    w.contentData = "code";
    insertTextAndWidget(w);
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_EQ(0, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, DrawingCanvas_NoTimestamp_DefaultToZero)
{
    CtDrawingCanvas c;
    c.x = 0; c.y = 0; c.width = 200; c.height = 150;
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(c);
    EXPECT_EQ(0, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_EQ(0, node->getDrawingCanvases()[0].tsLastSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. setWidgetTsLastSave — model-level timestamp mutation
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, SetWidgetTsLastSave_UpdatesModel)
{
    insertTextAndWidget(makeImage(1000, 2000));
    auto& content = model->getNodeById(1)->getContent();
    gint64 old = content.setWidgetTsLastSave(5, 3000);
    EXPECT_EQ(2000, old);
    EXPECT_EQ(3000, content.getWidgetDescAt(5).getTsLastSave());
    EXPECT_EQ(1000, content.getWidgetDescAt(5).getTsCreation());
}

TEST_F(WidgetTimestampTest, SetWidgetTsLastSave_NonexistentOffset_ReturnsZero)
{
    insertTextAndWidget(makeImage(1000, 2000));
    auto& content = model->getNodeById(1)->getContent();
    gint64 old = content.setWidgetTsLastSave(99, 3000);
    EXPECT_EQ(0, old);
}

TEST_F(WidgetTimestampTest, SetWidgetTsLastSave_PreservesCreation)
{
    insertTextAndWidget(makeCodebox(500, 600));
    auto& content = model->getNodeById(1)->getContent();
    content.setWidgetTsLastSave(5, 700);
    auto desc = content.getWidgetDescAt(5);
    EXPECT_EQ(500, desc.getTsCreation());
    EXPECT_EQ(700, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, SetWidgetTsLastSave_ZeroCreationStaysZero)
{
    insertTextAndWidget(makeImage(0, 0));
    auto& content = model->getNodeById(1)->getContent();
    content.setWidgetTsLastSave(5, 9999);
    auto desc = content.getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_EQ(9999, desc.getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. replaceWidget preserves/overwrites timestamps correctly
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, ReplaceWidget_ReturnsOldTimestamps)
{
    insertTextAndWidget(makeImage(1000, 2000));
    auto& content = model->getNodeById(1)->getContent();

    auto replacement = makeImage(3000, 4000);
    replacement.setProperty("char_offset", "5");
    auto old = content.replaceWidget(5, replacement);
    EXPECT_EQ(1000, old.getTsCreation());
    EXPECT_EQ(2000, old.getTsLastSave());

    auto current = content.getWidgetDescAt(5);
    EXPECT_EQ(3000, current.getTsCreation());
    EXPECT_EQ(4000, current.getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Codebox content edit command: timestamps in undo/redo
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditCodeboxContent_Execute_UpdatesTimestamp)
{
    insertTextAndWidget(makeCodebox(1000, 2000));

    EditCodeboxContentCommand cmd(
        model, 1, 5,
        "print('hello')", "print('world')"
    );

    cmd.execute();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_GT(desc.getTsLastSave(), 2000);
}

TEST_F(WidgetTimestampTest, EditCodeboxContent_Undo_RestoresOldTimestamp)
{
    insertTextAndWidget(makeCodebox(1000, 2000));

    EditCodeboxContentCommand cmd(
        model, 1, 5,
        "print('hello')", "print('world')"
    );

    cmd.execute();
    auto afterExec = model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave();
    EXPECT_GT(afterExec, 2000);

    cmd.undo();
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, EditCodeboxContent_Redo_ReappliesNewTimestamp)
{
    insertTextAndWidget(makeCodebox(1000, 2000));

    EditCodeboxContentCommand cmd(
        model, 1, 5,
        "print('hello')", "print('world')"
    );

    cmd.execute();
    auto newTs = model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave();

    cmd.undo();
    EXPECT_EQ(2000, model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave());

    cmd.redo();
    EXPECT_EQ(newTs, model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Light table cell edit command: timestamps in undo/redo
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditTableCell_Execute_UpdatesTimestamp)
{
    insertTextAndWidget(makeLightTable(1000, 2000));

    EditTableCellCommand cmd(model, 1, 5, 0, 0, "A", "X");
    cmd.execute();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_GT(desc.getTsLastSave(), 2000);
}

TEST_F(WidgetTimestampTest, EditTableCell_Undo_RestoresOldTimestamp)
{
    insertTextAndWidget(makeLightTable(1000, 2000));

    EditTableCellCommand cmd(model, 1, 5, 0, 0, "A", "X");
    cmd.execute();
    cmd.undo();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, EditTableCell_Redo_ReappliesNewTimestamp)
{
    insertTextAndWidget(makeLightTable(1000, 2000));

    EditTableCellCommand cmd(model, 1, 5, 0, 0, "A", "X");
    cmd.execute();
    auto newTs = model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave();

    cmd.undo();
    cmd.redo();
    EXPECT_EQ(newTs, model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Rich table cell edit command: timestamps in undo/redo (model-only path)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditRichCell_Execute_UpdatesTimestamp)
{
    insertTextAndWidget(makeRichTable(1000, 2000));

    CtCellContent oldCell; oldCell.textSpans.emplace_back("A");
    CtCellContent newCell; newCell.textSpans.emplace_back("X");

    EditRichCellCommand cmd(model, nullptr, 1, 5, 0, 0, oldCell, newCell);
    cmd.execute();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_GT(desc.getTsLastSave(), 2000);
}

TEST_F(WidgetTimestampTest, EditRichCell_Undo_RestoresOldTimestamp)
{
    insertTextAndWidget(makeRichTable(1000, 2000));

    CtCellContent oldCell; oldCell.textSpans.emplace_back("A");
    CtCellContent newCell; newCell.textSpans.emplace_back("X");

    EditRichCellCommand cmd(model, nullptr, 1, 5, 0, 0, oldCell, newCell);
    cmd.execute();
    cmd.undo();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(1000, desc.getTsCreation());
    EXPECT_EQ(2000, desc.getTsLastSave());
}

TEST_F(WidgetTimestampTest, EditRichCell_Redo_ReappliesNewTimestamp)
{
    insertTextAndWidget(makeRichTable(1000, 2000));

    CtCellContent oldCell; oldCell.textSpans.emplace_back("A");
    CtCellContent newCell; newCell.textSpans.emplace_back("X");

    EditRichCellCommand cmd(model, nullptr, 1, 5, 0, 0, oldCell, newCell);
    cmd.execute();
    auto newTs = model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave();

    cmd.undo();
    cmd.redo();
    EXPECT_EQ(newTs, model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Drawing canvas commands: timestamps in undo/redo
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, DrawStroke_Execute_UpdatesCanvasTimestamp)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    DrawStrokeCommand cmd(model, 1, 0, makeStroke());
    cmd.execute();

    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_GT(node->getDrawingCanvases()[0].tsLastSave, 2000);
}

TEST_F(WidgetTimestampTest, DrawStroke_Undo_RestoresOldCanvasTimestamp)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    DrawStrokeCommand cmd(model, 1, 0, makeStroke());
    cmd.execute();
    cmd.undo();

    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, DrawStroke_Redo_ReappliesNewTimestamp)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    DrawStrokeCommand cmd(model, 1, 0, makeStroke());
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, EraseStroke_UndoRedo_TimestampRoundTrip)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));
    node->getDrawingCanvasesMut()[0].strokes.push_back(makeStroke());

    EraseStrokeCommand cmd(model, 1, 0, makeStroke(), 0);
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(newTs, 2000);

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, MoveCanvas_UndoRedo_TimestampRoundTrip)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    MoveCanvasCommand cmd(model, 1, 0, 10, 20, 100, 200);
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(newTs, 2000);

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, ResizeCanvas_UndoRedo_TimestampRoundTrip)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    ResizeCanvasCommand cmd(model, 1, 0,
        10, 20, 300, 250,
        10, 20, 400, 350);
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(newTs, 2000);

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, MoveStroke_UndoRedo_TimestampRoundTrip)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));
    node->getDrawingCanvasesMut()[0].strokes.push_back(makeStroke());

    auto& pts = node->getDrawingCanvasesMut()[0].strokes[0].points;
    std::vector<CtDrawingPoint> oldPts = pts;
    std::vector<CtDrawingPoint> newPts = {{55.0, 55.0}, {60.0, 60.0}};
    MoveStrokeCommand cmd(model, 1, 0, 0, oldPts, newPts);
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(newTs, 2000);

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, RotateStroke_UndoRedo_TimestampRoundTrip)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));
    node->getDrawingCanvasesMut()[0].strokes.push_back(makeStroke());

    RotateStrokeCommand cmd(model, 1, 0, 0, 0.0, 1.57);
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(newTs, 2000);

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, CanvasProperties_UndoRedo_TimestampRoundTrip)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    CanvasPropertiesCommand cmd(model, 1, 0,
        "", "canvas1",
        "#ffffff", "#000000",
        0.15, 0.50,
        8.0, 4.0,
        false, true);
    cmd.execute();
    auto newTs = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(newTs, 2000);

    cmd.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    cmd.redo();
    EXPECT_EQ(newTs, node->getDrawingCanvases()[0].tsLastSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. Drawing canvas: creation timestamp unchanged by modification commands
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, DrawStroke_DoesNotChangeCreationTimestamp)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    DrawStrokeCommand cmd(model, 1, 0, makeStroke());
    cmd.execute();
    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);

    cmd.undo();
    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. Legacy canvas without timestamps: modification still works
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, DrawStroke_LegacyCanvas_ZeroCreationPreserved)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(0, 0));

    DrawStrokeCommand cmd(model, 1, 0, makeStroke());
    cmd.execute();
    EXPECT_EQ(0, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_GT(node->getDrawingCanvases()[0].tsLastSave, 0);

    cmd.undo();
    EXPECT_EQ(0, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_EQ(0, node->getDrawingCanvases()[0].tsLastSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. Legacy codebox without timestamps: modification updates tsLastSave only
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditCodeboxContent_LegacyWidget_ZeroCreationPreserved)
{
    insertTextAndWidget(makeCodebox(0, 0));

    EditCodeboxContentCommand cmd(model, 1, 5, "old", "new");
    cmd.execute();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_GT(desc.getTsLastSave(), 0);

    cmd.undo();
    desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_EQ(0, desc.getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 13. Legacy light table without timestamps: modification updates tsLastSave only
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditTableCell_LegacyWidget_ZeroCreationPreserved)
{
    insertTextAndWidget(makeLightTable(0, 0));

    EditTableCellCommand cmd(model, 1, 5, 0, 0, "A", "X");
    cmd.execute();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_GT(desc.getTsLastSave(), 0);

    cmd.undo();
    desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_EQ(0, desc.getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 14. Legacy rich table without timestamps: modification updates tsLastSave only
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditRichCell_LegacyWidget_ZeroCreationPreserved)
{
    insertTextAndWidget(makeRichTable(0, 0));

    CtCellContent oldCell; oldCell.textSpans.emplace_back("A");
    CtCellContent newCell; newCell.textSpans.emplace_back("X");

    EditRichCellCommand cmd(model, nullptr, 1, 5, 0, 0, oldCell, newCell);
    cmd.execute();

    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_GT(desc.getTsLastSave(), 0);

    cmd.undo();
    desc = model->getNodeById(1)->getContent().getWidgetDescAt(5);
    EXPECT_EQ(0, desc.getTsCreation());
    EXPECT_EQ(0, desc.getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 15. Multiple edits: each command captures the timestamp from the *previous* edit
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, MultipleCodeboxEdits_ChainedTimestamps)
{
    insertTextAndWidget(makeCodebox(1000, 2000));

    EditCodeboxContentCommand cmd1(model, 1, 5, "a", "b");
    cmd1.execute();
    auto ts1 = model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave();
    EXPECT_GT(ts1, 2000);

    EditCodeboxContentCommand cmd2(model, 1, 5, "b", "c");
    cmd2.execute();
    auto ts2 = model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave();
    EXPECT_GE(ts2, ts1);

    cmd2.undo();
    EXPECT_EQ(ts1, model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave());

    cmd1.undo();
    EXPECT_EQ(2000, model->getNodeById(1)->getContent().getWidgetDescAt(5).getTsLastSave());
}

// ═══════════════════════════════════════════════════════════════════════════
// 16. Widget desc XML round-trip preserves timestamps
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, XmlRoundTrip_ImageTimestampsPreserved)
{
    insertTextAndWidget(makeImage(1000, 2000));
    auto& content = model->getNodeById(1)->getContent();
    auto xml = content.toXml();
    EXPECT_NE(std::string::npos, xml.find("ts_creation=\"1000\""));
    EXPECT_NE(std::string::npos, xml.find("ts_lastsave=\"2000\""));
}

TEST_F(WidgetTimestampTest, XmlRoundTrip_CodeboxTimestampsPreserved)
{
    insertTextAndWidget(makeCodebox(1000, 2000));
    auto& content = model->getNodeById(1)->getContent();
    auto xml = content.toXml();
    EXPECT_NE(std::string::npos, xml.find("ts_creation=\"1000\""));
    EXPECT_NE(std::string::npos, xml.find("ts_lastsave=\"2000\""));
}

TEST_F(WidgetTimestampTest, XmlRoundTrip_TableTimestampsPreserved)
{
    insertTextAndWidget(makeLightTable(1000, 2000));
    auto& content = model->getNodeById(1)->getContent();
    auto xml = content.toXml();
    EXPECT_NE(std::string::npos, xml.find("ts_creation=\"1000\""));
    EXPECT_NE(std::string::npos, xml.find("ts_lastsave=\"2000\""));
}

TEST_F(WidgetTimestampTest, XmlRoundTrip_NoTimestamps_OmittedFromXml)
{
    CtWidgetDesc w(CtAnchWidgType::ImagePng);
    w.setProperty("link", "");
    w.contentData = "PNG";
    insertTextAndWidget(w);
    auto& content = model->getNodeById(1)->getContent();
    auto xml = content.toXml();
    EXPECT_EQ(std::string::npos, xml.find("ts_creation"));
    EXPECT_EQ(std::string::npos, xml.find("ts_lastsave"));
}

// ═══════════════════════════════════════════════════════════════════════════
// 17. Drawing canvas XML round-trip preserves timestamps
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, DrawingXml_TimestampsPreservedInRoundTrip)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(1000, 2000));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);

    auto parsed = CtXmlHelper::drawing_canvases_from_xml(root);
    ASSERT_EQ(1u, parsed.size());
    EXPECT_EQ(1000, parsed[0].tsCreation);
    EXPECT_EQ(2000, parsed[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, DrawingXml_NoTimestamps_ParsedAsZero)
{
    std::vector<CtDrawingCanvas> canvases;
    canvases.push_back(makeCanvas(0, 0));

    xmlpp::Document doc;
    auto* root = doc.create_root_node("node");
    CtXmlHelper::drawing_canvases_to_xml(root, canvases);

    auto parsed = CtXmlHelper::drawing_canvases_from_xml(root);
    ASSERT_EQ(1u, parsed.size());
    EXPECT_EQ(0, parsed[0].tsCreation);
    EXPECT_EQ(0, parsed[0].tsLastSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// 18. Widget modification command on nonexistent widget (robustness)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, EditCodeboxContent_NoWidgetAtOffset_NoTimestampCrash)
{
    auto node = model->getNodeById(1);
    std::map<std::string, std::string> attrs;
    node->getContent().insertText(0, "Hello", attrs);

    EditCodeboxContentCommand cmd(model, 1, 99, "old", "new");
    cmd.execute();
    auto desc = model->getNodeById(1)->getContent().getWidgetDescAt(99);
    EXPECT_EQ(CtAnchWidgType::None, desc.type);
}

// ═══════════════════════════════════════════════════════════════════════════
// 19. Multiple drawing commands in sequence: timestamps chain correctly
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, MultipleDrawingCommands_ChainedTimestamps)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    DrawStrokeCommand cmd1(model, 1, 0, makeStroke());
    cmd1.execute();
    auto ts1 = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GT(ts1, 2000);

    CtDrawingStroke s2 = makeStroke();
    s2.color = "#ff0000";
    DrawStrokeCommand cmd2(model, 1, 0, s2);
    cmd2.execute();
    auto ts2 = node->getDrawingCanvases()[0].tsLastSave;
    EXPECT_GE(ts2, ts1);

    // Undo second: goes back to ts1
    cmd2.undo();
    EXPECT_EQ(ts1, node->getDrawingCanvases()[0].tsLastSave);

    // Undo first: goes back to original
    cmd1.undo();
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);

    // Redo both in order
    cmd1.redo();
    EXPECT_EQ(ts1, node->getDrawingCanvases()[0].tsLastSave);

    cmd2.redo();
    EXPECT_EQ(ts2, node->getDrawingCanvases()[0].tsLastSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// 20. AddCanvas / DeleteCanvas preserve creation timestamps
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(WidgetTimestampTest, AddCanvas_PreservesTimestamps)
{
    auto canvas = makeCanvas(1000, 2000);
    AddCanvasCommand cmd(model, 1, canvas);
    cmd.execute();

    auto node = model->getNodeById(1);
    ASSERT_EQ(1u, node->getDrawingCanvases().size());
    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);
}

TEST_F(WidgetTimestampTest, DeleteCanvas_Undo_RestoresTimestamps)
{
    auto node = model->getNodeById(1);
    node->getDrawingCanvasesMut().push_back(makeCanvas(1000, 2000));

    DeleteCanvasCommand cmd(model, 1, node->getDrawingCanvases()[0], 0);
    cmd.execute();
    EXPECT_EQ(0u, node->getDrawingCanvases().size());

    cmd.undo();
    ASSERT_EQ(1u, node->getDrawingCanvases().size());
    EXPECT_EQ(1000, node->getDrawingCanvases()[0].tsCreation);
    EXPECT_EQ(2000, node->getDrawingCanvases()[0].tsLastSave);
}
