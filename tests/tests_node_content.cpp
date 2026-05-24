/*
 * tests_node_content.cpp
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

#include "ct_node_content.h"
#include "gtest/gtest.h"

// Test basic construction and empty state
TEST(CtNodeContentTest, EmptyContent)
{
    CtNodeContent content;
    EXPECT_TRUE(content.isEmpty());
    EXPECT_EQ(0, content.length());
    EXPECT_EQ("", content.getText());
}

// Test inserting plain text
TEST(CtNodeContentTest, InsertPlainText)
{
    CtNodeContent content;

    std::map<std::string, std::string> attrs;
    int end = content.insertText(0, "Hello", attrs);

    EXPECT_EQ(5, end);
    EXPECT_EQ(5, content.length());
    EXPECT_EQ("Hello", content.getText());
    EXPECT_FALSE(content.isEmpty());
}

// Test inserting text with attributes
TEST(CtNodeContentTest, InsertTextWithAttributes)
{
    CtNodeContent content;

    std::map<std::string, std::string> attrs;
    attrs["weight"] = "bold";
    attrs["foreground"] = "#ff0000";

    content.insertText(0, "Bold", attrs);

    EXPECT_EQ(4, content.length());
    EXPECT_EQ("Bold", content.getText());

    auto elem = content.getElementAtOffset(0);
    ASSERT_TRUE(elem.valid);
    const auto& elements = content.getElements();
    ASSERT_GT(elements.size(), 0);
    EXPECT_TRUE(elements[elem.elementIndex].isTextSpan());
    EXPECT_EQ("bold", elements[elem.elementIndex].textSpan.getAttribute("weight"));
    EXPECT_EQ("#ff0000", elements[elem.elementIndex].textSpan.getAttribute("foreground"));
}

// Test inserting text at different positions
TEST(CtNodeContentTest, InsertAtDifferentPositions)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello", attrs);
    content.insertText(5, " World", attrs);  // Append
    EXPECT_EQ("Hello World", content.getText());

    content.insertText(5, ",", attrs);  // Insert in middle
    EXPECT_EQ("Hello, World", content.getText());

    content.insertText(0, "Say: ", attrs);  // Insert at start
    EXPECT_EQ("Say: Hello, World", content.getText());
}

// Test merging adjacent spans with same attributes
TEST(CtNodeContentTest, MergeAdjacentSpans)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;
    attrs["weight"] = "bold";

    content.insertText(0, "Hello", attrs);
    content.insertText(5, " World", attrs);  // Same attributes - should merge

    const auto& elements = content.getElements();
    // Should be merged into single span
    EXPECT_EQ(1, elements.size());
    EXPECT_EQ("Hello World", elements[0].textSpan.text);
}

// Test splitting spans with different attributes
TEST(CtNodeContentTest, SplitSpansDifferentAttributes)
{
    CtNodeContent content;

    std::map<std::string, std::string> boldAttrs;
    boldAttrs["weight"] = "bold";

    std::map<std::string, std::string> italicAttrs;
    italicAttrs["style"] = "italic";

    content.insertText(0, "Bold", boldAttrs);
    content.insertText(4, "Italic", italicAttrs);

    const auto& elements = content.getElements();
    EXPECT_EQ(2, elements.size());
    EXPECT_EQ("Bold", elements[0].textSpan.text);
    EXPECT_EQ("bold", elements[0].textSpan.getAttribute("weight"));
    EXPECT_EQ("Italic", elements[1].textSpan.text);
    EXPECT_EQ("italic", elements[1].textSpan.getAttribute("style"));
}

// Test deleting text range
TEST(CtNodeContentTest, DeleteRange)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello World", attrs);
    EXPECT_EQ("Hello World", content.getText());

    auto deleted = content.deleteRange(5, 6);  // Delete " World"
    EXPECT_EQ("Hello", content.getText());
    EXPECT_EQ(5, content.length());

    ASSERT_FALSE(deleted.isEmpty());
    EXPECT_EQ(5, deleted.startOffset);
}

// Test undo delete operation
TEST(CtNodeContentTest, UndoDelete)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello World", attrs);
    auto deleted = content.deleteRange(5, 6);

    EXPECT_EQ("Hello", content.getText());

    content.reinsertContent(deleted);
    EXPECT_EQ("Hello World", content.getText());
}

// Test deleting across multiple spans
TEST(CtNodeContentTest, DeleteAcrossSpans)
{
    CtNodeContent content;

    std::map<std::string, std::string> attrs1;
    attrs1["weight"] = "bold";
    std::map<std::string, std::string> attrs2;

    content.insertText(0, "Bold", attrs1);
    content.insertText(4, "Normal", attrs2);

    EXPECT_EQ("BoldNormal", content.getText());

    auto deleted = content.deleteRange(2, 4);  // Delete "ldNo" (4 chars)
    EXPECT_EQ("Bormal", content.getText());
}

// Test applying format to range
TEST(CtNodeContentTest, ApplyFormat)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello World", attrs);

    auto change = content.applyFormat(0, 5, "weight", "bold");

    const auto& elements = content.getElements();
    EXPECT_GE(elements.size(), 1);

    // First span should be bold
    EXPECT_TRUE(elements[0].isTextSpan());
    EXPECT_EQ("bold", elements[0].textSpan.getAttribute("weight"));

    // Change should track what was modified
    EXPECT_FALSE(change.isEmpty());
}

// Test undo format operation
TEST(CtNodeContentTest, UndoFormat)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello World", attrs);
    auto change = content.applyFormat(0, 5, "weight", "bold");

    content.restoreFormat(0, 5, "weight", change);

    const auto& elements = content.getElements();
    EXPECT_GE(elements.size(), 1);
    EXPECT_FALSE(elements[0].textSpan.hasAttribute("weight"));
}

// Test removing format from range
TEST(CtNodeContentTest, RemoveFormat)
{
    CtNodeContent content;

    std::map<std::string, std::string> attrs;
    attrs["weight"] = "bold";
    attrs["style"] = "italic";

    content.insertText(0, "Hello", attrs);

    auto change = content.removeFormat(0, 5, "weight");

    const auto& elements = content.getElements();
    ASSERT_GE(elements.size(), 1);
    EXPECT_FALSE(elements[0].textSpan.hasAttribute("weight"));
    EXPECT_TRUE(elements[0].textSpan.hasAttribute("style"));  // Other attrs preserved
}

// Test inserting widget
TEST(CtNodeContentTest, InsertWidget)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello World", attrs);

    CtWidgetDesc widget(CtAnchWidgType::ImagePng);
    widget.setProperty("filename", "test.png");

    int pos = content.insertWidget(5, widget);  // Insert after "Hello"
    EXPECT_EQ(5, pos);
    EXPECT_EQ(12, content.length());  // 11 chars + 1 for widget

    // Text should have special character for widget
    Glib::ustring text = content.getText();
    EXPECT_EQ(12, text.length());
}

// Test removing widget
TEST(CtNodeContentTest, RemoveWidget)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello", attrs);

    CtWidgetDesc widget(CtAnchWidgType::ImagePng);
    widget.setProperty("filename", "test.png");
    content.insertWidget(5, widget);

    EXPECT_EQ(6, content.length());

    auto removed = content.removeWidget(5);
    EXPECT_EQ(CtAnchWidgType::ImagePng, removed.type);
    EXPECT_EQ("test.png", removed.getProperty("filename"));
    EXPECT_EQ(5, content.length());
}

// Test element access by offset
TEST(CtNodeContentTest, GetElementAtOffset)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello", attrs);
    content.insertText(5, " World", attrs);

    auto elem0 = content.getElementAtOffset(0);
    EXPECT_TRUE(elem0.valid);
    EXPECT_EQ(0, elem0.elementIndex);
    EXPECT_EQ(0, elem0.offsetInElement);

    auto elem5 = content.getElementAtOffset(5);
    EXPECT_TRUE(elem5.valid);

    auto elem11 = content.getElementAtOffset(11);  // At end
    EXPECT_TRUE(elem11.valid);

    auto elem100 = content.getElementAtOffset(100);  // Out of bounds
    EXPECT_FALSE(elem100.valid);
}

// Test complex scenario: multiple operations
TEST(CtNodeContentTest, ComplexScenario)
{
    CtNodeContent content;

    // Insert some text
    std::map<std::string, std::string> normalAttrs;
    content.insertText(0, "Hello World", normalAttrs);
    EXPECT_EQ(11, content.length());

    // Insert a widget at position 5 (after "Hello")
    CtWidgetDesc image(CtAnchWidgType::ImagePng);
    content.insertWidget(5, image);
    EXPECT_EQ(12, content.length());  // 11 chars + 1 widget

    // Make "World" bold
    std::map<std::string, std::string> boldAttrs;
    boldAttrs["weight"] = "bold";
    // "World" is now at offset 6 (after widget)
    auto change = content.applyFormat(6, 5, "weight", "bold");

    // Length should still be 12
    EXPECT_EQ(12, content.length());

    // Delete "Hello"
    auto deleted = content.deleteRange(0, 5);
    EXPECT_EQ(7, content.length());  // widget + "World"

    // Verify we can undo
    content.reinsertContent(deleted);
    EXPECT_EQ(12, content.length());
}

// Test clearing content
TEST(CtNodeContentTest, ClearContent)
{
    CtNodeContent content;
    std::map<std::string, std::string> attrs;

    content.insertText(0, "Hello World", attrs);
    EXPECT_FALSE(content.isEmpty());

    content.clear();
    EXPECT_TRUE(content.isEmpty());
    EXPECT_EQ(0, content.length());
}

// ============================================================================
// Phase RT-1: CtCellContent and CtWidgetDesc::richTableData
// ============================================================================

TEST(CtNodeContentTest, RichTable_CtCellContent_PlainText)
{
    CtCellContent cell;
    cell.textSpans.emplace_back("hello");
    EXPECT_EQ("hello", cell.getPlainText());
    EXPECT_TRUE(cell.isPlainText());
}

TEST(CtNodeContentTest, RichTable_CtCellContent_Formatted)
{
    CtCellContent cell;
    std::map<std::string, std::string> boldAttr = {{"weight", "heavy"}};
    cell.textSpans.emplace_back("bold", boldAttr);
    cell.textSpans.emplace_back(" normal");
    EXPECT_EQ("bold normal", cell.getPlainText());
    EXPECT_FALSE(cell.isPlainText());  // has formatting
}

TEST(CtNodeContentTest, RichTable_WidgetDesc_HasRichTableData)
{
    CtWidgetDesc desc(CtAnchWidgType::TableRich);
    EXPECT_FALSE(desc.hasRichTableData());

    CtCellContent cell;
    cell.textSpans.emplace_back("A1");
    desc.richTableData.push_back({cell});
    EXPECT_TRUE(desc.hasRichTableData());
}

TEST(CtNodeContentTest, RichTable_ToXml_FromXml_RoundTrip)
{
    // Build a 2×2 rich table widget descriptor
    CtWidgetDesc desc(CtAnchWidgType::TableRich);
    desc.setProperty("char_offset", "5");
    desc.setProperty("justification", "left");
    desc.setProperty("col_max", "60");
    desc.setProperty("is_rich", "1");  // stored in properties like is_light for plain tables

    // Row 0 (header): plain cells
    {
        CtCellContent h0, h1;
        h0.textSpans.emplace_back("Name");
        h1.textSpans.emplace_back("Value");
        desc.richTableData.push_back({h0, h1});
    }
    // Row 1 (data): one plain, one bold
    {
        CtCellContent d0, d1;
        d0.textSpans.emplace_back("foo");
        std::map<std::string, std::string> bold = {{"weight", "heavy"}};
        d1.textSpans.emplace_back("bar", bold);
        desc.richTableData.push_back({d0, d1});
    }

    // Serialize to XML via CtNodeContent: "Hello" (5 chars) then the widget at offset 5
    std::map<std::string, std::string> noAttrs;
    CtNodeContent ncFull;
    ncFull.insertText(0, "Hello", noAttrs);
    ncFull.insertWidget(5, desc);

    Glib::ustring xml = ncFull.toXml();
    EXPECT_NE(std::string::npos, xml.raw().find("is_rich=\"1\""));
    EXPECT_NE(std::string::npos, xml.raw().find("<rich_text"));
    EXPECT_NE(std::string::npos, xml.raw().find("weight=\"heavy\""));

    // Round-trip: parse back
    CtNodeContent ncParsed = CtNodeContent::fromXml(xml, nullptr);

    // Find the widget in the parsed content
    CtWidgetDesc parsedDesc;
    for (const auto& elem : ncParsed.getElements()) {
        if (elem.isWidget() && elem.widget.type == CtAnchWidgType::TableRich) {
            parsedDesc = elem.widget;
            break;
        }
    }
    ASSERT_EQ(CtAnchWidgType::TableRich, parsedDesc.type);
    ASSERT_TRUE(parsedDesc.hasRichTableData());
    ASSERT_EQ(2u, parsedDesc.richTableData.size());     // 2 rows
    ASSERT_EQ(2u, parsedDesc.richTableData[0].size());  // 2 cols

    // Header row
    EXPECT_EQ("Name",  parsedDesc.richTableData[0][0].getPlainText());
    EXPECT_EQ("Value", parsedDesc.richTableData[0][1].getPlainText());
    // Data row
    EXPECT_EQ("foo", parsedDesc.richTableData[1][0].getPlainText());
    EXPECT_EQ("bar", parsedDesc.richTableData[1][1].getPlainText());
    // Bold attribute on bar
    ASSERT_FALSE(parsedDesc.richTableData[1][1].textSpans.empty());
    EXPECT_EQ("heavy", parsedDesc.richTableData[1][1].textSpans[0].getAttribute("weight"));

    // Equality operator
    EXPECT_EQ(desc, parsedDesc);
}

TEST(CtNodeContentTest, RichTable_PlainTableUnchanged)
{
    // Verify that existing plain table serialization is unaffected
    CtWidgetDesc plainDesc(CtAnchWidgType::TableHeavy);
    plainDesc.setProperty("char_offset", "0");
    plainDesc.setProperty("justification", "left");
    plainDesc.setProperty("col_max", "60");
    plainDesc.tableData = {{"H1", "H2"}, {"A", "B"}};

    CtNodeContent nc;
    nc.insertWidget(0, plainDesc);
    Glib::ustring xml = nc.toXml();

    // Must NOT have is_rich
    EXPECT_EQ(std::string::npos, xml.raw().find("is_rich"));

    CtNodeContent ncParsed = CtNodeContent::fromXml(xml, nullptr);
    CtWidgetDesc parsedDesc;
    for (const auto& elem : ncParsed.getElements()) {
        if (elem.isWidget()) { parsedDesc = elem.widget; break; }
    }
    EXPECT_EQ(CtAnchWidgType::TableHeavy, parsedDesc.type);
    EXPECT_FALSE(parsedDesc.hasRichTableData());
    EXPECT_TRUE(parsedDesc.hasTableData());
}
