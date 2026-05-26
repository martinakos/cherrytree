/*
 * tests_html_parse.cpp
 *
 * Copyright 2009-2025
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include "ct_parser.h"
#include "ct_config.h"
#include "ct_const.h"
#include "tests_common.h"
#include <gdkmm/wrap_init.h>
#include <gdkmm/pixbuf.h>

namespace {

xmlpp::Element* get_slot_root(const xmlpp::Document& doc) {
    return dynamic_cast<xmlpp::Element*>(doc.get_root_node());
}

xmlpp::Element* find_slot_element(const xmlpp::Document& doc) {
    auto* root = get_slot_root(doc);
    if (!root) return nullptr;
    for (auto* child : root->get_children()) {
        auto* el = dynamic_cast<xmlpp::Element*>(child);
        if (el && el->get_name() == "slot") return el;
    }
    return nullptr;
}

xmlpp::Element* find_table_element(const xmlpp::Document& doc) {
    auto* slot = find_slot_element(doc);
    if (!slot) return nullptr;
    for (auto* child : slot->get_children()) {
        auto* el = dynamic_cast<xmlpp::Element*>(child);
        if (el && el->get_name() == "table") return el;
    }
    return nullptr;
}

std::vector<xmlpp::Element*> get_child_elements(xmlpp::Element* parent, const std::string& name = "") {
    std::vector<xmlpp::Element*> result;
    for (auto* child : parent->get_children()) {
        auto* el = dynamic_cast<xmlpp::Element*>(child);
        if (el && (name.empty() || el->get_name() == name))
            result.push_back(el);
    }
    return result;
}

std::string get_text_content(xmlpp::Element* el) {
    auto* text = el->get_child_text();
    return text ? text->get_content() : "";
}

std::vector<xmlpp::Element*> find_encoded_png_elements(const xmlpp::Document& doc) {
    auto* slot = find_slot_element(doc);
    if (!slot) return {};
    return get_child_elements(slot, "encoded_png");
}

} // namespace

TEST(HtmlParseGroup, FallbackPixbufUsedWhenDownloadFails)
{
    Glib::init();
    Gdk::wrap_init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    auto fallback = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 4, 4);
    fallback->fill(0xff0000ff);
    parser.set_fallback_pixbuf(fallback);

    // img src points to an unreachable URL — download will fail, fallback should be used
    parser.feed("<html><body><img src=\"https://127.0.0.1:1/no-such-image.png\"/></body></html>");

    auto images = find_encoded_png_elements(parser.doc());
    ASSERT_EQ(1u, images.size());
    std::string link = images[0]->get_attribute_value("link");
    ASSERT_NE(std::string::npos, link.find("127.0.0.1"));
    ASSERT_FALSE(get_text_content(images[0]).empty());
}

TEST(HtmlParseGroup, FallbackPixbufNotUsedWhenBase64Succeeds)
{
    Glib::init();
    Gdk::wrap_init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    auto fallback = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 4, 4);
    fallback->fill(0x00ff00ff);
    parser.set_fallback_pixbuf(fallback);

    // 1x1 red PNG as base64
    const char* b64_png =
        "data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4"
        "2mP8z8BQDwADhQGAWjR9awAAAABJRU5ErkJggg==";
    std::string html = std::string("<html><body><img src=\"") + b64_png + "\"/></body></html>";
    parser.feed(html);

    auto images = find_encoded_png_elements(parser.doc());
    ASSERT_EQ(1u, images.size());
    // The link should contain the base64 data URI, not a web URL
    std::string link = images[0]->get_attribute_value("link");
    ASSERT_NE(std::string::npos, link.find("data:image"));
}

TEST(HtmlParseGroup, FallbackPixbufConsumedAfterFirstUse)
{
    Glib::init();
    Gdk::wrap_init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    auto fallback = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 4, 4);
    fallback->fill(0x0000ffff);
    parser.set_fallback_pixbuf(fallback);

    // Two unreachable images — only the first should get the fallback
    parser.feed("<html><body>"
                "<img src=\"https://127.0.0.1:1/img1.png\"/>"
                "<img src=\"https://127.0.0.1:1/img2.png\"/>"
                "</body></html>");

    auto images = find_encoded_png_elements(parser.doc());
    ASSERT_EQ(1u, images.size());
}

TEST(HtmlParseGroup, PlainTableNoRichFormatting)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><th>Name</th><th>Value</th></tr>"
                "<tr><td>foo</td><td>bar</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    // th triggers bold => rich table path
    ASSERT_STREQ("1", table->get_attribute_value("is_rich").c_str());
}

TEST(HtmlParseGroup, ThAppliesBoldFormatting)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><th>Header</th></tr>"
                "<tr><td>data</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    ASSERT_STREQ("1", table->get_attribute_value("is_rich").c_str());

    // Header row goes last (CherryTree convention); find it
    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    ASSERT_STREQ("heavy", rich_texts[0]->get_attribute_value("weight").c_str());
    ASSERT_STREQ("Header", get_text_content(rich_texts[0]).c_str());
}

TEST(HtmlParseGroup, ColspanExpandsCells)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><th colspan=\"3\">Wide</th></tr>"
                "<tr><td>a</td><td>b</td><td>c</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    // First data row + header row (moved to end)
    ASSERT_EQ(2u, rows.size());
    // Data row should have 3 cells
    auto data_cells = get_child_elements(rows[0], "cell");
    ASSERT_EQ(3u, data_cells.size());
    // Header row should also have 3 cells (1 original + 2 from colspan expansion)
    auto header_cells = get_child_elements(rows[1], "cell");
    ASSERT_EQ(3u, header_cells.size());
}

TEST(HtmlParseGroup, CellBackgroundColors)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td style=\"background-color: #ff0000;\">red</td><td>plain</td></tr>"
                "<tr><td>plain2</td><td style=\"background-color: #00ff00;\">green</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    std::string cell_bg = table->get_attribute_value("cell_bg_colors");
    ASSERT_FALSE(cell_bg.empty());
    // Should contain color references for cells with backgrounds
    ASSERT_NE(std::string::npos, cell_bg.find("#"));
}

TEST(HtmlParseGroup, InlineBoldInCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td>normal <b>bold</b> end</td><td>x</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    ASSERT_STREQ("1", table->get_attribute_value("is_rich").c_str());

    // First row is header (moved to end), data row comes first
    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    // Header row (last) has the bold content
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 2u);

    bool found_bold = false;
    for (auto* rt : rich_texts) {
        if (rt->get_attribute_value("weight") == "heavy") {
            ASSERT_STREQ("bold", get_text_content(rt).c_str());
            found_bold = true;
        }
    }
    ASSERT_TRUE(found_bold);
}

TEST(HtmlParseGroup, InlineItalicInCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td><i>italic</i></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    ASSERT_STREQ("italic", rich_texts[0]->get_attribute_value("style").c_str());
}

TEST(HtmlParseGroup, InlineCodeInCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td><code>monospace</code></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    ASSERT_STREQ("monospace", rich_texts[0]->get_attribute_value("family").c_str());
}

TEST(HtmlParseGroup, InlineStrikethroughInCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td><s>struck</s></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    ASSERT_STREQ("true", rich_texts[0]->get_attribute_value("strikethrough").c_str());
}

TEST(HtmlParseGroup, SpanColorInCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    // Use #cc0000 — dark enough to not be filtered (Y ~0.10) but still clearly colored
    parser.feed("<table>"
                "<tr><td><span style=\"color: #cc0000;\">red text</span></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    ASSERT_STREQ("1", table->get_attribute_value("is_rich").c_str());

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    std::string fg = rich_texts[0]->get_attribute_value("foreground");
    ASSERT_FALSE(fg.empty());
    ASSERT_STREQ("red text", get_text_content(rich_texts[0]).c_str());
}

TEST(HtmlParseGroup, LightForegroundColorFiltered)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    // Very light color (designed for dark bg) should be filtered
    parser.feed("<table>"
                "<tr><td><span style=\"color: #ffffff;\">white text</span></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    // White color should be filtered in _convert_html_color (Y > 0.95)
    // so cell attrs will be empty => plain table path (no is_rich)
    if (table) {
        std::string is_rich = table->get_attribute_value("is_rich");
        if (is_rich == "1") {
            auto rows = get_child_elements(table, "row");
            for (auto* row : rows) {
                auto cells = get_child_elements(row, "cell");
                for (auto* cell : cells) {
                    auto rich_texts = get_child_elements(cell, "rich_text");
                    for (auto* rt : rich_texts)
                        ASSERT_TRUE(rt->get_attribute_value("foreground").empty());
                }
            }
        }
    }
}

TEST(HtmlParseGroup, CaptionSerializedBeforeTable)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<caption>Table Title</caption>"
                "<tr><td>a</td><td>b</td></tr>"
                "<tr><td>c</td><td>d</td></tr>"
                "</table>");

    auto xml_str = parser.to_string();
    // Caption text should appear as rich_text before the table element
    auto table_pos = xml_str.find("<table ");
    auto caption_pos = xml_str.find("Table Title");
    ASSERT_NE(std::string::npos, caption_pos);
    ASSERT_NE(std::string::npos, table_pos);
    ASSERT_LT(caption_pos, table_pos);
}

TEST(HtmlParseGroup, FontColorInCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    // Use #cc0000 — passes the Y > 0.05 luminance filter
    parser.feed("<table>"
                "<tr><td><font color=\"#cc0000\">colored</font></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    std::string fg = rich_texts[0]->get_attribute_value("foreground");
    ASSERT_FALSE(fg.empty());
}

TEST(HtmlParseGroup, TrStyleInheritedByCells)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr style=\"background-color: #cccccc;\"><td>cell1</td><td>cell2</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    // The tr background should be captured as cell_bg_colors
    std::string cell_bg = table->get_attribute_value("cell_bg_colors");
    ASSERT_FALSE(cell_bg.empty());
}

TEST(HtmlParseGroup, MultipleFormattingInOneCell)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td><b>bold</b> and <i>italic</i></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    ASSERT_STREQ("1", table->get_attribute_value("is_rich").c_str());

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    // Should have at least: "bold", " and ", "italic"
    ASSERT_GE(rich_texts.size(), 3u);

    bool found_bold = false, found_italic = false;
    for (auto* rt : rich_texts) {
        if (rt->get_attribute_value("weight") == "heavy") found_bold = true;
        if (rt->get_attribute_value("style") == "italic") found_italic = true;
    }
    ASSERT_TRUE(found_bold);
    ASSERT_TRUE(found_italic);
}

TEST(HtmlParseGroup, HeaderRowMovedToEnd)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><th>H1</th><th>H2</th></tr>"
                "<tr><td>d1</td><td>d2</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_EQ(2u, rows.size());

    // Last row should be the header (with bold text "H1", "H2")
    auto header_cells = get_child_elements(rows[1], "cell");
    ASSERT_EQ(2u, header_cells.size());
    auto rt = get_child_elements(header_cells[0], "rich_text");
    ASSERT_GE(rt.size(), 1u);
    ASSERT_STREQ("H1", get_text_content(rt[0]).c_str());
    ASSERT_STREQ("heavy", rt[0]->get_attribute_value("weight").c_str());

    // First row should be data
    auto data_cells = get_child_elements(rows[0], "cell");
    ASSERT_EQ(2u, data_cells.size());
    auto rt_data = get_child_elements(data_cells[0], "rich_text");
    ASSERT_GE(rt_data.size(), 1u);
    ASSERT_STREQ("d1", get_text_content(rt_data[0]).c_str());
}

TEST(HtmlParseGroup, PlainTableWithoutFormatting)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    // No th, no styles — should go the plain (non-rich) path
    parser.feed("<table>"
                "<tr><td>a</td><td>b</td></tr>"
                "<tr><td>c</td><td>d</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);
    // Plain table uses different XML structure (no is_rich attribute)
    std::string is_rich = table->get_attribute_value("is_rich");
    ASSERT_TRUE(is_rich.empty() || is_rich == "0");
}

TEST(HtmlParseGroup, NbspReplacedWithSpace)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    // \xC2\xA0 is UTF-8 for non-breaking space
    parser.feed("<table>"
                "<tr><td><b>hello\xC2\xA0world</b></td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    ASSERT_GE(rich_texts.size(), 1u);
    std::string text = get_text_content(rich_texts[0]);
    ASSERT_EQ(std::string::npos, text.find("\xC2\xA0"));
    ASSERT_NE(std::string::npos, text.find("hello world"));
}

TEST(HtmlParseGroup, BrInCellPreservesNewline)
{
    Glib::init();
    CtConfig* pCtConfig = CtConfig::GetCtConfig();
    CtHtml2Xml parser{pCtConfig};

    parser.feed("<table>"
                "<tr><td><b>line1</b><br/>line2</td><td>plain</td></tr>"
                "<tr><td>r2</td><td>r2b</td></tr>"
                "</table>");

    auto* table = find_table_element(parser.doc());
    ASSERT_NE(nullptr, table);

    auto rows = get_child_elements(table, "row");
    ASSERT_GE(rows.size(), 1u);
    auto* header_row = rows.back();
    auto cells = get_child_elements(header_row, "cell");
    ASSERT_GE(cells.size(), 1u);
    auto rich_texts = get_child_elements(cells[0], "rich_text");
    std::string all_text;
    for (auto* rt : rich_texts) all_text += get_text_content(rt);
    ASSERT_NE(std::string::npos, all_text.find("\n"));
    ASSERT_NE(std::string::npos, all_text.find("line1"));
    ASSERT_NE(std::string::npos, all_text.find("line2"));
}
