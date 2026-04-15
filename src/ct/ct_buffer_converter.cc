/*
 * ct_buffer_converter.cc
 *
 * Buffer <-> Content conversion functions
 * Handles the circular dependency between CtNodeContent and CtMainWin
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
#include "ct_main_win.h"
#include <type_traits>
#include "ct_const.h"
#include "ct_storage_xml.h"
#include "ct_widgets.h"
#include "ct_codebox.h"
#include "ct_table.h"
#include "ct_image.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"

// Extract a CtWidgetDesc from a live anchored widget.
// Delegates directly to the widget's to_widget_desc() virtual method.
CtWidgetDesc extractWidgetDesc(CtAnchoredWidget* widget, int charOffset)
{
    if (!widget) return CtWidgetDesc();
    return widget->to_widget_desc(charOffset);
}

// Build a GTK TextBuffer from CtNodeContent
// Returns the list of created widget objects so caller can register them with the tree store
std::list<CtAnchoredWidget*> buildBufferFromContent(
    const CtNodeContent& content,
    const Glib::RefPtr<Gtk::TextBuffer>& buffer,
    CtMainWin* pCtMainWin)
{
    std::list<CtAnchoredWidget*> createdWidgets;

    if (!buffer) {
        return createdWidgets;
    }

    // Clear buffer
    buffer->erase(buffer->begin(), buffer->end());

    if (content.isEmpty()) {
        return createdWidgets;
    }

    // If no CtMainWin provided, fall back to plain text insertion
    if (!pCtMainWin) {
        for (const auto& elem : content.getElements()) {
            if (elem.isTextSpan()) {
                buffer->insert(buffer->end(), elem.textSpan.text);
            }
            else if (elem.isWidget()) {
                buffer->insert(buffer->end(), "\uFFFC");  // Object replacement character
            }
        }
        return createdWidgets;
    }

    // Full implementation with formatting and widget reconstruction
    for (const auto& elem : content.getElements()) {
        if (elem.isTextSpan()) {
            const auto& span = elem.textSpan;
            auto insert_iter = buffer->end();

            if (span.attributes.empty()) {
                buffer->insert(insert_iter, span.text);
            }
            else {
                std::vector<Glib::ustring> tag_names;
                for (const auto& tag_prop : CtConst::TAG_PROPERTIES) {
                    auto it = span.attributes.find(std::string(tag_prop));
                    if (it != span.attributes.end() && !it->second.empty()) {
                        tag_names.push_back(pCtMainWin->get_text_tag_name_exist_or_create(
                            it->first, it->second));
                    }
                }
                if (!tag_names.empty()) {
                    buffer->insert_with_tags_by_name(insert_iter, span.text, tag_names);
                }
                else {
                    buffer->insert(insert_iter, span.text);
                }
            }
        }
        else if (elem.isWidget()) {
            const auto& widgetDesc = elem.widget;

            // Create widget directly from CtWidgetDesc without going through XML
            CtAnchoredWidget* widget = nullptr;
            // Use current buffer end position as the correct offset, NOT the stored
            // char_offset from the descriptor. After model mutations (deleteRange,
            // reinsertContent) the stored char_offset may be stale.
            const int charOffset = buffer->end().get_offset();
            const std::string justification = widgetDesc.getJustification();

            try {
                if (widgetDesc.type == CtAnchWidgType::CodeBox) {
                    // Create codebox from properties
                    const std::string textContent = widgetDesc.getContent();
                    const std::string syntaxHighlighting = widgetDesc.getSyntaxHighlighting();
                    const int frameWidth = widgetDesc.getFrameWidth();
                    const int frameHeight = widgetDesc.getFrameHeight();
                    const bool widthInPixels = widgetDesc.isWidthInPixels();
                    const bool highlightBrackets = widgetDesc.isHighlightBrackets();
                    const bool showLineNumbers = widgetDesc.isShowLineNumbers();

                    widget = new CtCodebox{pCtMainWin, textContent, syntaxHighlighting,
                                          frameWidth, frameHeight, charOffset, justification,
                                          widthInPixels, highlightBrackets, showLineNumbers};
                }
                else if (widgetDesc.type == CtAnchWidgType::TableLight ||
                         widgetDesc.type == CtAnchWidgType::TableHeavy ||
                         widgetDesc.type == CtAnchWidgType::TableRich) {
                    const int colWidthDefault = widgetDesc.getColWidthDefault();
                    const std::string colWidthsStr = widgetDesc.getColWidthsStr();
                    CtTableColWidths colWidths;
                    if (!colWidthsStr.empty()) {
                        colWidths = CtStrUtil::gstring_split_to_int(colWidthsStr.c_str(), ",");
                    }

                    CtTableMatrix tableMatrix;

                    if (widgetDesc.type == CtAnchWidgType::TableRich) {
                        widget = new CtTableRich{pCtMainWin, widgetDesc.richTableData,
                                                 colWidthDefault, charOffset, justification, colWidths};
                    } else {
                        const bool is_light = (widgetDesc.type == CtAnchWidgType::TableLight);
                        for (const auto& row : widgetDesc.tableData) {
                            tableMatrix.push_back(CtTableRow{});
                            for (const auto& cell : row) {
                                if (is_light) {
                                    tableMatrix.back().push_back(new Glib::ustring{cell});
                                } else {
                                    tableMatrix.back().push_back(new CtTextCell{pCtMainWin, cell, CtConst::TABLE_CELL_TEXT_ID});
                                }
                            }
                        }
                        if (is_light) {
                            widget = new CtTableLight{pCtMainWin, tableMatrix, colWidthDefault, charOffset, justification, colWidths};
                        } else {
                            widget = new CtTableHeavy{pCtMainWin, tableMatrix, colWidthDefault, charOffset, justification, colWidths};
                        }
                    }
                    // Restore table style (border, colors) from widget descriptor properties
                    if (widget) {
                        CtTableStyle style;
                        const std::string bw = widgetDesc.getProperty("border_width");
                        if (!bw.empty()) style.borderWidth = std::stoi(bw);
                        const std::string bc = widgetDesc.getProperty("border_color");
                        if (!bc.empty()) style.borderColor = bc;
                        const std::string tbg = widgetDesc.getProperty("table_bg_color");
                        if (!tbg.empty()) style.tableBgColor = tbg;
                        // Parse "row,col:value;..." format for per-cell maps
                        auto parseCellMapStr = [](const std::string& s, auto& targetMap) {
                            if (s.empty()) return;
                            for (const auto& entry : str::split(s, ";")) {
                                const auto colon = entry.find(':');
                                if (colon == std::string::npos) continue;
                                const std::string coords = entry.substr(0, colon);
                                const std::string value = entry.substr(colon + 1);
                                const auto comma = coords.find(',');
                                if (comma == std::string::npos) continue;
                                size_t row = static_cast<size_t>(std::stoul(coords.substr(0, comma)));
                                size_t col = static_cast<size_t>(std::stoul(coords.substr(comma + 1)));
                                if constexpr (std::is_same_v<typename std::decay_t<decltype(targetMap)>::mapped_type, int>) {
                                    targetMap[{row, col}] = std::stoi(value);
                                } else {
                                    targetMap[{row, col}] = value;
                                }
                            }
                        };
                        parseCellMapStr(widgetDesc.getProperty("cell_bg_colors"), style.cellBgColors);
                        parseCellMapStr(widgetDesc.getProperty("cell_border_widths"), style.cellBorderWidths);
                        parseCellMapStr(widgetDesc.getProperty("cell_border_colors"), style.cellBorderColors);
                        static_cast<CtTableCommon*>(widget)->setTableStyle(style);
                    }
                }
                else if (widgetDesc.type == CtAnchWidgType::ImageAnchor) {
                    // Create anchor image
                    const std::string anchorName = widgetDesc.getAnchorName();
                    CtAnchorExpCollState expCollState{CtAnchorExpCollState::None};
                    if (0 != CtStrUtil::is_header_anchor_name(anchorName)) {
                        const std::string state = widgetDesc.getAnchorState();
                        if (state == "coll") expCollState = CtAnchorExpCollState::Collapsed;
                        else expCollState = CtAnchorExpCollState::Expanded;
                    }
                    widget = new CtImageAnchor{pCtMainWin, anchorName, expCollState, charOffset, justification};
                }
                else if (widgetDesc.type == CtAnchWidgType::ImageLatex) {
                    // Create latex image
                    const std::string encodedBlob = widgetDesc.getContent();
                    widget = new CtImageLatex{pCtMainWin, encodedBlob, charOffset, justification, CtImageEmbFile::get_next_unique_id()};
                }
                else if (widgetDesc.type == CtAnchWidgType::ImageEmbFile) {
                    // Create embedded file image
                    const std::string fileName = widgetDesc.getFileName();
                    const std::string encodedBlob = widgetDesc.getContent();
                    std::string rawBlob;
                    if (!encodedBlob.empty()) {
                        rawBlob = Glib::Base64::decode(encodedBlob);
                    }
                    const time_t timeInt = static_cast<time_t>(widgetDesc.getFileTime());
                    widget = new CtImageEmbFile{pCtMainWin, fileName, rawBlob, timeInt, charOffset, justification,
                                               CtImageEmbFile::get_next_unique_id(), fs::path{fileName}};
                }
                else if (widgetDesc.type == CtAnchWidgType::ImagePng) {
                    // Create PNG image
                    const std::string encodedBlob = widgetDesc.getContent();
                    std::string rawBlob;
                    if (!encodedBlob.empty()) {
                        rawBlob = Glib::Base64::decode(encodedBlob);
                    }
                    const std::string link = widgetDesc.getLink();
                    widget = new CtImagePng{pCtMainWin, rawBlob, link, charOffset, justification};
                }
                else {
                    spdlog::warn("buildBufferFromContent: unknown widget type {}, skipping",
                                static_cast<int>(widgetDesc.type));
                }

                if (widget) {
                    // Insert widget anchor into buffer
                    widget->insertInTextBuffer(buffer);
                    createdWidgets.push_back(widget);
                } else {
                    buffer->insert(buffer->end(), "\uFFFC");
                }
            }
            catch (const std::exception& e) {
                spdlog::error("buildBufferFromContent: exception creating widget: {}", e.what());
                buffer->insert(buffer->end(), "\uFFFC");
            }
        }
    }

    return createdWidgets;
}

// Convert a GTK TextBuffer to CtNodeContent
// Extracts all text spans with their formatting attributes and widgets
CtNodeContent buildContentFromBuffer(
    const Glib::RefPtr<Gtk::TextBuffer>& buffer,
    const std::list<CtAnchoredWidget*>& widgets)
{
    CtNodeContent content;

    if (!buffer) {
        return content;
    }

    // Build a map of widget offsets → widget pointer for quick lookup
    std::map<int, CtAnchoredWidget*> widgetMap;
    for (CtAnchoredWidget* w : widgets) {
        widgetMap[w->getOffset()] = w;
    }

    Gtk::TextIter startIter = buffer->begin();
    Gtk::TextIter endIter = buffer->end();

    if (startIter == endIter) {
        return content;
    }

    std::map<std::string, std::string> currentAttrs = extractAttributesFromIter(startIter);
    Glib::ustring currentText;

    auto finalizeSpan = [&]() {
        if (!currentText.empty()) {
            content.insertText(content.length(), currentText, currentAttrs);
            currentText.clear();
        }
    };

    Gtk::TextIter iter = startIter;
    while (iter != endIter) {
        int currentOffset = iter.get_offset();

        // Check if there's a widget anchor at this position
        auto anchor = iter.get_child_anchor();
        if (anchor) {
            finalizeSpan();

            // Find widget in our map and serialize its properties into CtWidgetDesc
            auto mapIt = widgetMap.find(currentOffset);
            if (mapIt != widgetMap.end()) {
                CtAnchoredWidget* widget = mapIt->second;

                // Get widget descriptor directly via virtual method (no XML round-trip)
                CtWidgetDesc widgetDesc = widget->to_widget_desc(currentOffset);
                // Overwrite char_offset with actual structured-model position
                widgetDesc.setProperty("char_offset", std::to_string(content.length()));
                content.insertWidget(content.length(), widgetDesc);
            }
            else {
                // Widget not in map (shouldn't happen), insert placeholder
                CtWidgetDesc placeholder(CtAnchWidgType::ImagePng);
                placeholder.setProperty("char_offset", std::to_string(content.length()));
                content.insertWidget(content.length(), placeholder);
            }

            iter.forward_char();
            if (iter != endIter) {
                currentAttrs = extractAttributesFromIter(iter);
            }
            continue;
        }

        // Regular character — check if attributes changed
        auto iterAttrs = extractAttributesFromIter(iter);
        if (currentAttrs != iterAttrs) {
            finalizeSpan();
            currentAttrs = iterAttrs;
        }

        currentText += iter.get_char();
        iter.forward_char();
    }

    finalizeSpan();
    return content;
}
