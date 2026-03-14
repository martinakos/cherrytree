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
#include "ct_const.h"
#include "ct_storage_xml.h"
#include "ct_widgets.h"
#include "ct_codebox.h"
#include "ct_table.h"
#include "ct_image.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"

// Extract a CtWidgetDesc from a live anchored widget.
// Calls to_xml() and parses the result — same logic as buildContentFromBuffer's widget extraction.
CtWidgetDesc extractWidgetDesc(CtAnchoredWidget* widget, int charOffset)
{
    if (!widget) return CtWidgetDesc();

    xmlpp::Document tmp_doc;
    xmlpp::Element* root = tmp_doc.create_root_node("node");
    widget->to_xml(root, 0, nullptr, "");

    for (xmlpp::Node* child : root->get_children()) {
        const xmlpp::Element* elem = dynamic_cast<const xmlpp::Element*>(child);
        if (!elem) continue;

        const Glib::ustring elem_name = elem->get_name();
        CtAnchWidgType wtype = CtAnchWidgType::None;

        if (elem_name == "encoded_png") {
            if (!elem->get_attribute_value("anchor").empty()) {
                wtype = CtAnchWidgType::ImageAnchor;
            } else if (elem->get_attribute_value("filename") == "latex") {
                wtype = CtAnchWidgType::ImageLatex;
            } else if (!elem->get_attribute_value("filename").empty()) {
                wtype = CtAnchWidgType::ImageEmbFile;
            } else {
                wtype = CtAnchWidgType::ImagePng;
            }
        } else if (elem_name == "codebox") {
            wtype = CtAnchWidgType::CodeBox;
        } else if (elem_name == "table") {
            wtype = widget->get_type(); // preserves TableLight vs TableHeavy
        }

        if (wtype == CtAnchWidgType::None) break;

        CtWidgetDesc desc(wtype);
        for (const auto& attr : elem->get_attributes()) {
            desc.setProperty(attr->get_name(), attr->get_value());
        }

        if (elem_name == "table") {
            for (xmlpp::Node* pNodeRow : elem->get_children("row")) {
                std::vector<Glib::ustring> row;
                for (xmlpp::Node* pNodeCell : pNodeRow->get_children("cell")) {
                    const xmlpp::Element* cellElem = dynamic_cast<const xmlpp::Element*>(pNodeCell);
                    if (cellElem) {
                        const xmlpp::TextNode* pTextNode = cellElem->get_child_text();
                        row.push_back(pTextNode ? pTextNode->get_content() : "");
                    }
                }
                if (!row.empty()) desc.tableData.push_back(row);
            }
            // Header row is written last in XML — move it to front
            if (!desc.tableData.empty()) {
                desc.tableData.insert(desc.tableData.begin(), desc.tableData.back());
                desc.tableData.pop_back();
            }
        } else {
            const xmlpp::TextNode* textNode = elem->get_child_text();
            if (textNode) {
                const Glib::ustring tc = textNode->get_content();
                if (!tc.empty()) {
                    desc.contentData = tc.raw();
                    desc.setProperty("_content", tc.raw());
                }
            }
        }

        desc.setProperty("char_offset", std::to_string(charOffset));
        return desc;
    }

    return CtWidgetDesc();
}

// Returns the XML tag name for a given widget type
static const char* widget_type_to_tag_name(CtAnchWidgType type)
{
    switch (type) {
        case CtAnchWidgType::ImagePng:
        case CtAnchWidgType::ImageAnchor:
        case CtAnchWidgType::ImageLatex:
        case CtAnchWidgType::ImageEmbFile:
            return "encoded_png";
        case CtAnchWidgType::CodeBox:
            return "codebox";
        case CtAnchWidgType::TableHeavy:
        case CtAnchWidgType::TableLight:
            return "table";
        default:
            return nullptr;
    }
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
                else if (widgetDesc.type == CtAnchWidgType::TableLight || widgetDesc.type == CtAnchWidgType::TableHeavy) {
                    // Create table from tableData
                    const int colWidthDefault = widgetDesc.getColWidthDefault();
                    const std::string colWidthsStr = widgetDesc.getColWidthsStr();
                    CtTableColWidths colWidths;
                    if (!colWidthsStr.empty()) {
                        colWidths = CtStrUtil::gstring_split_to_int(colWidthsStr.c_str(), ",");
                    }

                    // Build CtTableMatrix from tableData
                    CtTableMatrix tableMatrix;
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

                // Serialize to XML to extract all properties
                xmlpp::Document tmp_doc;
                xmlpp::Element* root = tmp_doc.create_root_node("node");
                widget->to_xml(root, 0, nullptr, "");

                // Find the widget element (first non-text child)
                for (xmlpp::Node* child : root->get_children()) {
                    const xmlpp::Element* elem = dynamic_cast<const xmlpp::Element*>(child);
                    if (!elem) continue;

                    Glib::ustring elem_name = elem->get_name();
                    CtAnchWidgType wtype = CtAnchWidgType::None;
                    if (elem_name == "encoded_png") {
                        // Determine more specific image type from attributes
                        if (!elem->get_attribute_value("anchor").empty()) {
                            wtype = CtAnchWidgType::ImageAnchor;
                        } else if (elem->get_attribute_value("filename") == "latex") {
                            wtype = CtAnchWidgType::ImageLatex;
                        } else if (!elem->get_attribute_value("filename").empty()) {
                            wtype = CtAnchWidgType::ImageEmbFile;
                        } else {
                            wtype = CtAnchWidgType::ImagePng;
                        }
                    } else if (elem_name == "codebox") {
                        wtype = CtAnchWidgType::CodeBox;
                    } else if (elem_name == "table") {
                        // Determine light vs heavy from tableMatrix size
                        wtype = widget->get_type();
                    }

                    if (wtype != CtAnchWidgType::None) {
                        CtWidgetDesc widgetDesc(wtype);
                        for (const auto& attr : elem->get_attributes()) {
                            widgetDesc.setProperty(attr->get_name(), attr->get_value());
                        }

                        // Extract content based on widget type
                        if (elem_name == "table") {
                            // Parse table cell data from <row><cell> structure
                            for (xmlpp::Node* pNodeRow : elem->get_children("row")) {
                                std::vector<Glib::ustring> row;
                                for (xmlpp::Node* pNodeCell : pNodeRow->get_children("cell")) {
                                    xmlpp::Element* cellElem = dynamic_cast<xmlpp::Element*>(pNodeCell);
                                    if (cellElem) {
                                        const xmlpp::TextNode* pTextNode = cellElem->get_child_text();
                                        const Glib::ustring cellContent = pTextNode ? pTextNode->get_content() : "";
                                        row.push_back(cellContent);
                                    }
                                }
                                if (!row.empty()) {
                                    widgetDesc.tableData.push_back(row);
                                }
                            }
                            // Move header row from last to first (same as storage XML helper does)
                            if (!widgetDesc.tableData.empty()) {
                                widgetDesc.tableData.insert(widgetDesc.tableData.begin(), widgetDesc.tableData.back());
                                widgetDesc.tableData.pop_back();
                            }
                            // Also keep in _content for backward compatibility during transition
                            const xmlpp::TextNode* textNode = elem->get_child_text();
                            if (textNode) {
                                Glib::ustring tc = textNode->get_content();
                                if (!tc.empty()) {
                                    widgetDesc.setProperty("_content", tc.raw());
                                }
                            }
                        } else {
                            // For other widgets, extract text content
                            const xmlpp::TextNode* textNode = elem->get_child_text();
                            if (textNode) {
                                Glib::ustring tc = textNode->get_content();
                                if (!tc.empty()) {
                                    widgetDesc.contentData = tc.raw();
                                    // Also keep in properties for backward compatibility
                                    widgetDesc.setProperty("_content", tc.raw());
                                }
                            }
                        }

                        // Overwrite char_offset with actual buffer position
                        widgetDesc.setProperty("char_offset", std::to_string(content.length()));
                        content.insertWidget(content.length(), widgetDesc);
                    }
                    break;  // Only handle first child element
                }
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
