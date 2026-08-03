/*
 * tests_delta_engine.cpp
 *
 * GTK-free unit tests for CtDeltaEngine: round-trip verification for every
 * delta type and multi-operation chain reversal.
 */

#include "ct_delta_engine.h"
#include "ct_node_content.h"
#include "ct_text_commands.h"
#include "ct_widget_commands.h"
#include <gtest/gtest.h>
#include <glibmm.h>

namespace {

// Helpers to build delta strings matching the command serialization format

std::string serializeAttrs(const std::map<std::string, std::string>& attrs)
{
    std::string result;
    for (const auto& [k, v] : attrs) {
        if (!result.empty()) result += ';';
        result += k + '=' + v;
    }
    return result;
}

std::string makeINS(int offset, const Glib::ustring& text,
                     const std::map<std::string, std::string>& attrs = {})
{
    return "INS|" + std::to_string(offset) + "|" + Glib::Base64::encode(text)
         + "|" + serializeAttrs(attrs);
}

std::string makeDEL(int start, const std::vector<std::pair<Glib::ustring, std::map<std::string, std::string>>>& textSpans,
                     const std::vector<CtWidgetDesc>& widgets = {})
{
    std::string result = "DEL|" + std::to_string(start);
    size_t spanIdx = 0;
    size_t widgetIdx = 0;

    for (const auto& [text, attrs] : textSpans) {
        result += "|T" + Glib::Base64::encode(text) + "|" + serializeAttrs(attrs);
    }
    for (const auto& wd : widgets) {
        std::string xml = wd.toXml().raw();
        result += "|W" + Glib::Base64::encode(xml);
    }
    return result;
}

std::string makeFMT(int start, int length, const std::string& attr, const std::string& value,
                     const std::vector<std::tuple<int,int,std::string>>& spanChanges)
{
    std::string result = "FMT|" + std::to_string(start) + "|" + std::to_string(length)
                       + "|" + attr + "|" + value;
    for (const auto& [off, len, oldVal] : spanChanges) {
        result += "|" + std::to_string(off) + "," + std::to_string(len) + "," + oldVal;
    }
    return result;
}

std::string makeRFM(int start, int length, const std::string& attr,
                     const std::vector<std::tuple<int,int,std::string>>& spanChanges)
{
    std::string result = "RFM|" + std::to_string(start) + "|" + std::to_string(length)
                       + "|" + attr + "|";
    for (const auto& [off, len, oldVal] : spanChanges) {
        result += "|" + std::to_string(off) + "," + std::to_string(len) + "," + oldVal;
    }
    return result;
}

std::string makeTED(const CtNodeContent& oldContent, const CtNodeContent& newContent)
{
    return "TED|" + Glib::Base64::encode(oldContent.toXml().raw())
         + "|" + Glib::Base64::encode(newContent.toXml().raw());
}

std::string makeWIns(int offset, const CtWidgetDesc& wd)
{
    return "WIns|" + std::to_string(offset) + "|" + Glib::Base64::encode(wd.toXml(offset).raw());
}

std::string makeWMod(int offset, const CtWidgetDesc& oldDesc, const CtWidgetDesc& newDesc)
{
    return "WMod|" + std::to_string(offset) + "|"
         + Glib::Base64::encode(oldDesc.toXml(offset).raw()) + "|"
         + Glib::Base64::encode(newDesc.toXml(offset).raw());
}

std::string makeTCel(int offset, int row, int col,
                      const Glib::ustring& oldText, const Glib::ustring& newText)
{
    return "TCel|" + std::to_string(offset) + "|" + std::to_string(row)
         + "|" + std::to_string(col) + "|" + Glib::Base64::encode(oldText)
         + "|" + Glib::Base64::encode(newText);
}

std::string makeCBed(int offset, const std::string& oldContent, const std::string& newContent)
{
    return "CBed|" + std::to_string(offset) + "|"
         + Glib::Base64::encode(oldContent) + "|" + Glib::Base64::encode(newContent);
}

std::string makeRCel(int offset, int row, int col,
                      const CtCellContent& oldCell, const CtCellContent& newCell)
{
    return "RCel|" + std::to_string(offset) + "|" + std::to_string(row)
         + "|" + std::to_string(col) + "|"
         + Glib::Base64::encode(oldCell.toXml().raw()) + "|"
         + Glib::Base64::encode(newCell.toXml().raw());
}

CtWidgetDesc makeCodeboxDesc(const std::string& code = "print(1)",
                              const std::string& syntax = "python3",
                              int width = 300, int height = 150)
{
    CtWidgetDesc wd(CtAnchWidgType::CodeBox);
    wd.setProperty("syntax_highlighting", syntax);
    wd.setProperty("frame_width", std::to_string(width));
    wd.setProperty("frame_height", std::to_string(height));
    wd.setProperty("width_in_pixels", "1");
    wd.setProperty("highlight_brackets", "0");
    wd.setProperty("show_line_numbers", "0");
    wd.setProperty("justification", "left");
    wd.contentData = code;
    return wd;
}

CtWidgetDesc makeTableDesc(const std::vector<std::vector<Glib::ustring>>& data,
                            int colMax = 60)
{
    CtWidgetDesc wd(CtAnchWidgType::TableLight);
    wd.setProperty("col_max", std::to_string(colMax));
    wd.setProperty("justification", "left");
    wd.tableData = data;
    return wd;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// B. Round-trip tests: applyForward then applyReverse restores original
// ═════════════════════════════════════════════════════════════════════════════

TEST(DeltaEngineRoundTrip, INS)
{
    CtNodeContent content;
    content.insertText(0, "hello", {});
    CtNodeContent original = content;

    std::string delta = makeINS(2, "XX");
    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    EXPECT_EQ(content.getText(), "heXXllo");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content.getText(), "hello");
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, INS_WithAttributes)
{
    CtNodeContent content;
    content.insertText(0, "hello", {});
    CtNodeContent original = content;

    std::map<std::string, std::string> attrs = {{"weight", "heavy"}};
    std::string delta = makeINS(2, "XX", attrs);

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, DEL_TextOnly)
{
    CtNodeContent content;
    content.insertText(0, "hello world", {});
    CtNodeContent original = content;

    std::string delta = makeDEL(5, {{{" world"}, {}}});

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    EXPECT_EQ(content.getText(), "hello");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content.getText(), "hello world");
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, DEL_WithWidget)
{
    CtNodeContent content;
    content.insertText(0, "abc", {});
    CtWidgetDesc cb = makeCodeboxDesc("x = 1");
    content.insertWidget(2, cb);
    content.insertText(4, "def", {});
    CtNodeContent original = content;

    // Delete range [1, 4) which includes text "b", the widget, and "d"
    // To construct the DEL delta, we need the deleted elements
    auto deleted = content.extractRange(1, 3);
    std::string result = "DEL|1";
    for (const auto& elem : deleted.elements) {
        if (elem.isTextSpan()) {
            result += "|T" + Glib::Base64::encode(elem.textSpan.text)
                    + "|" + serializeAttrs(elem.textSpan.attributes);
        } else if (elem.isWidget()) {
            std::string xml = elem.widget.toXml().raw();
            result += "|W" + Glib::Base64::encode(xml);
        }
    }

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, result));
    EXPECT_EQ(content.length(), original.length() - 3);

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, result));
    EXPECT_EQ(content.length(), original.length());
    EXPECT_EQ(content.getText(), original.getText());
}

TEST(DeltaEngineRoundTrip, FMT)
{
    CtNodeContent content;
    content.insertText(0, "hello", {});
    CtNodeContent original = content;

    // Apply bold — no existing weight, so oldValue is ""
    std::string delta = makeFMT(0, 5, "weight", "heavy", {{0, 5, ""}});

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    EXPECT_TRUE(content.hasAttributeInRange(0, 5, "weight"));

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_FALSE(content.hasAttributeInRange(0, 5, "weight"));
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, FMT_Overwrite)
{
    CtNodeContent content;
    content.insertText(0, "hello", {{"weight", "heavy"}});
    CtNodeContent original = content;

    // Change weight from "heavy" to "ultrabold" on substring [1,4)
    std::string delta = makeFMT(1, 3, "weight", "ultrabold", {{1, 3, "heavy"}});

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, RFM)
{
    CtNodeContent content;
    content.insertText(0, "hello", {{"weight", "heavy"}});
    CtNodeContent original = content;

    std::string delta = makeRFM(0, 5, "weight", {{0, 5, "heavy"}});

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    EXPECT_FALSE(content.hasAttributeInRange(0, 5, "weight"));

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_TRUE(content.hasAttributeInRange(0, 5, "weight"));
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, TED)
{
    CtNodeContent oldContent;
    oldContent.insertText(0, "before text", {});

    CtNodeContent newContent;
    newContent.insertText(0, "after paste", {{"weight", "heavy"}});

    CtNodeContent content = oldContent;
    std::string delta = makeTED(oldContent, newContent);

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    EXPECT_EQ(content.getText(), "after paste");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content.getText(), "before text");
    EXPECT_EQ(content, oldContent);
}

TEST(DeltaEngineRoundTrip, WIns)
{
    CtNodeContent content;
    content.insertText(0, "hello", {});
    CtNodeContent original = content;

    CtWidgetDesc cb = makeCodeboxDesc("print('hi')");
    std::string delta = makeWIns(3, cb);

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    EXPECT_EQ(content.length(), 6u);

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content.length(), 5u);
    EXPECT_EQ(content.getText(), "hello");
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, WMod)
{
    CtNodeContent content;
    content.insertText(0, "abc", {});
    CtWidgetDesc cbOld = makeCodeboxDesc("old code", "python3");
    cbOld.setProperty("char_offset", "3");
    content.insertWidget(3, cbOld);

    CtWidgetDesc cbNew = makeCodeboxDesc("new code", "javascript");
    cbNew.setProperty("char_offset", "3");
    std::string delta = makeWMod(3, cbOld, cbNew);

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    auto wdAfter = content.getWidgetDescAt(3);
    EXPECT_EQ(wdAfter.contentData, "new code");
    EXPECT_EQ(wdAfter.getSyntaxHighlighting(), "javascript");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    auto wdRestored = content.getWidgetDescAt(3);
    EXPECT_EQ(wdRestored.contentData, "old code");
    EXPECT_EQ(wdRestored.getSyntaxHighlighting(), "python3");
    EXPECT_EQ(wdRestored.type, CtAnchWidgType::CodeBox);
    EXPECT_EQ(content.length(), 4u);
}

TEST(DeltaEngineRoundTrip, TCel)
{
    CtNodeContent content;
    content.insertText(0, "abc", {});
    CtWidgetDesc tbl = makeTableDesc({{"cell00", "cell01"}, {"cell10", "cell11"}});
    tbl.setProperty("char_offset", "3");
    content.insertWidget(3, tbl);
    CtNodeContent original = content;

    std::string delta = makeTCel(3, 0, 0, "cell00", "edited");

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    auto wdAfter = content.getWidgetDescAt(3);
    EXPECT_EQ(wdAfter.tableData[0][0], "edited");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    auto wdRestored = content.getWidgetDescAt(3);
    EXPECT_EQ(wdRestored.tableData[0][0], "cell00");
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, CBed)
{
    CtNodeContent content;
    content.insertText(0, "abc", {});
    CtWidgetDesc cb = makeCodeboxDesc("original");
    cb.setProperty("char_offset", "3");
    content.insertWidget(3, cb);
    CtNodeContent original = content;

    std::string delta = makeCBed(3, "original", "modified");

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    auto wdAfter = content.getWidgetDescAt(3);
    EXPECT_EQ(wdAfter.contentData, "modified");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    auto wdRestored = content.getWidgetDescAt(3);
    EXPECT_EQ(wdRestored.contentData, "original");
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, RCel)
{
    CtNodeContent content;
    content.insertText(0, "abc", {});

    CtWidgetDesc rtbl(CtAnchWidgType::TableRich);
    rtbl.setProperty("col_max", "60");
    rtbl.setProperty("justification", "left");
    rtbl.setProperty("char_offset", "3");
    CtCellContent oldCell;
    oldCell.textSpans.push_back(CtTextSpan("old text"));
    CtCellContent emptyCell;
    emptyCell.textSpans.push_back(CtTextSpan(""));
    rtbl.richTableData = {{oldCell, emptyCell}};
    content.insertWidget(3, rtbl);
    CtNodeContent original = content;

    CtCellContent newCell;
    newCell.textSpans.push_back(CtTextSpan("new text", {{"weight", "heavy"}}));

    std::string delta = makeRCel(3, 0, 0, oldCell, newCell);

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta));
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta));
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineRoundTrip, Compound)
{
    CtNodeContent content;
    content.insertText(0, "hello", {});
    CtNodeContent original = content;

    // Compound: INS then DEL (two sub-deltas separated by newline)
    std::string ins = makeINS(5, " world");
    std::string del = makeDEL(0, {{{"h"}, {}}});
    std::string compound = ins + "\n" + del;

    ASSERT_TRUE(CtDeltaEngine::applyForward(content, compound));
    EXPECT_EQ(content.getText(), "ello world");

    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, compound));
    EXPECT_EQ(content.getText(), "hello");
    EXPECT_EQ(content, original);
}

// ═════════════════════════════════════════════════════════════════════════════
// C. Chain reversal tests
// ═════════════════════════════════════════════════════════════════════════════

TEST(DeltaEngineChain, ThreeOps)
{
    CtNodeContent content;
    content.insertText(0, "hello", {});
    CtNodeContent original = content;

    // Op 1: INS "XX" at offset 2 → "heXXllo"
    std::string delta1 = makeINS(2, "XX");
    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta1));
    EXPECT_EQ(content.getText(), "heXXllo");

    // Op 2: DEL char at offset 0 → "eXXllo"
    std::string delta2 = makeDEL(0, {{{"h"}, {}}});
    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta2));
    EXPECT_EQ(content.getText(), "eXXllo");

    // Op 3: FMT bold on [1,4) → "eXXllo" with bold on "XXl"
    std::string delta3 = makeFMT(1, 3, "weight", "heavy", {{1, 3, ""}});
    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta3));

    // Reverse the chain: delta3, delta2, delta1
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta3));
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta2));
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta1));

    EXPECT_EQ(content.getText(), "hello");
    EXPECT_EQ(content, original);
}

TEST(DeltaEngineChain, MixedTypes)
{
    CtNodeContent content;
    content.insertText(0, "abc", {});
    CtWidgetDesc cb = makeCodeboxDesc("code");
    cb.setProperty("char_offset", "3");
    content.insertWidget(3, cb);
    CtNodeContent original = content;

    // Op 1: Modify widget
    CtWidgetDesc cbNew = makeCodeboxDesc("new code");
    cbNew.setProperty("char_offset", "3");
    std::string delta1 = makeWMod(3, cb, cbNew);
    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta1));

    // Op 2: Insert text
    std::string delta2 = makeINS(0, "XY");
    ASSERT_TRUE(CtDeltaEngine::applyForward(content, delta2));

    // Reverse all
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta2));
    ASSERT_TRUE(CtDeltaEngine::applyReverse(content, delta1));

    EXPECT_EQ(content.length(), 4u);
    auto restoredWidget = content.getWidgetDescAt(3);
    EXPECT_EQ(restoredWidget.contentData, "code");
    EXPECT_EQ(restoredWidget.type, CtAnchWidgType::CodeBox);
}

TEST(DeltaEngineChain, BrokenChain)
{
    EXPECT_FALSE(CtDeltaEngine::isReplayable(""));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("INS|0|dGVzdA==|"));
    EXPECT_FALSE(CtDeltaEngine::isReplayable("UNKNOWN|foo"));
}

TEST(DeltaEngineChain, MultiStepRewind)
{
    CtNodeContent content;
    content.insertText(0, "start", {});
    CtNodeContent original = content;

    // Build a chain of 5 insertions
    std::vector<std::string> deltas;
    Glib::ustring text = "start";
    for (int i = 0; i < 5; ++i) {
        Glib::ustring ch(1, static_cast<gunichar>('A' + i));
        std::string d = makeINS(static_cast<int>(text.size()), ch);
        ASSERT_TRUE(CtDeltaEngine::applyForward(content, d));
        text += ch;
        deltas.push_back(d);
    }
    EXPECT_EQ(content.getText(), "startABCDE");

    // Rewind newest-first back to original
    for (auto it = deltas.rbegin(); it != deltas.rend(); ++it) {
        ASSERT_TRUE(CtDeltaEngine::applyReverse(content, *it));
    }
    EXPECT_EQ(content, original);
}

// ═════════════════════════════════════════════════════════════════════════════
// Edge cases
// ═════════════════════════════════════════════════════════════════════════════

TEST(DeltaEngineEdge, EmptyDelta)
{
    CtNodeContent content;
    content.insertText(0, "test", {});
    EXPECT_FALSE(CtDeltaEngine::applyForward(content, ""));
    EXPECT_FALSE(CtDeltaEngine::applyReverse(content, ""));
}

TEST(DeltaEngineEdge, MalformedDelta)
{
    CtNodeContent content;
    content.insertText(0, "test", {});
    EXPECT_FALSE(CtDeltaEngine::applyForward(content, "GARBAGE"));
    EXPECT_FALSE(CtDeltaEngine::applyReverse(content, "GARBAGE"));
}

TEST(DeltaEngineEdge, IsReplayable_AllTypes)
{
    EXPECT_TRUE(CtDeltaEngine::isReplayable("INS|0|dGVzdA==|"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("DEL|0|Tdata|"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("FMT|0|5|weight|heavy|0,5,"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("RFM|0|5|weight||0,5,heavy"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("TED|b2xk|bmV3"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("WIns|0|eG1s"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("WMod|0|b2xk|bmV3"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("TCel|0|0|0|b2xk|bmV3"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("CBed|0|b2xk|bmV3"));
    EXPECT_TRUE(CtDeltaEngine::isReplayable("RCel|0|0|0|b2xk|bmV3"));
}

TEST(DeltaEngineEdge, CompoundIsReplayable)
{
    std::string compound = "INS|0|dGVzdA==|\nDEL|0|Tdata|";
    EXPECT_TRUE(CtDeltaEngine::isReplayable(compound));

    std::string broken = "INS|0|dGVzdA==|\nGARBAGE";
    EXPECT_FALSE(CtDeltaEngine::isReplayable(broken));
}
