/*
 * ct_node_content.cc
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
#include <algorithm>
#include <stdexcept>
#include <gtkmm.h>
#include <libxml++/libxml++.h>
#include "ct_logging.h"

// Forward declarations to avoid including heavy headers
class CtMainWin;
class CtAnchoredWidget;

namespace str {
    // Forward declare str::xml_escape to avoid including ct_misc_utils.h which pulls in ct_types.h
    // This avoids fs::path template instantiation issues
    std::string xml_escape(const Glib::ustring& text);
    bool startswith(const std::string& str, const std::string& prefix);
}

namespace CtConst {
    // Forward declare TAG_PROPERTIES and tag prefixes
    extern const std::array<std::string_view, 12> TAG_PROPERTIES;
    extern const char* TAG_WEIGHT;
    extern const char* TAG_FOREGROUND;
    extern const char* TAG_BACKGROUND;
    extern const char* TAG_STYLE;
    extern const char* TAG_UNDERLINE;
    extern const char* TAG_STRIKETHROUGH;
    extern const char* TAG_SCALE;
    extern const char* TAG_INVISIBLE;
    extern const char* TAG_FAMILY;
    extern const char* TAG_JUSTIFICATION;
    extern const char* TAG_LINK;
    extern const char* TAG_INDENT;
    extern const char* TAG_WEIGHT_PREFIX;
    extern const char* TAG_FOREGROUND_PREFIX;
    extern const char* TAG_BACKGROUND_PREFIX;
    extern const char* TAG_STYLE_PREFIX;
    extern const char* TAG_UNDERLINE_PREFIX;
    extern const char* TAG_STRIKETHROUGH_PREFIX;
    extern const char* TAG_SCALE_PREFIX;
    extern const char* TAG_INVISIBLE_PREFIX;
    extern const char* TAG_FAMILY_PREFIX;
    extern const char* TAG_JUSTIFICATION_PREFIX;
    extern const char* TAG_LINK_PREFIX;
    extern const char* TAG_INDENT_PREFIX;
    extern const char* GTKSPELLCHECK_TAG_NAME;
}

// Get total character count
size_t CtNodeContent::length() const
{
    size_t total = 0;
    for (const auto& elem : _elements) {
        total += elem.length();
    }
    return total;
}

// Get plain text without formatting
Glib::ustring CtNodeContent::getText() const
{
    Glib::ustring result;
    for (const auto& elem : _elements) {
        if (elem.isTextSpan()) {
            result += elem.textSpan.text;
        } else {
            // Widgets are represented as special character (object replacement character)
            result += Glib::ustring(1, static_cast<gunichar>(0xFFFC));
        }
    }
    return result;
}

// Find element at given offset
CtNodeContent::ElementAtOffset CtNodeContent::getElementAtOffset(int offset) const
{
    if (offset < 0 || _elements.empty()) {
        return ElementAtOffset();
    }

    int currentOffset = 0;
    for (size_t i = 0; i < _elements.size(); ++i) {
        const auto& elem = _elements[i];
        int elemLen = static_cast<int>(elem.length());

        if (offset < currentOffset + elemLen) {
            return ElementAtOffset(i, offset - currentOffset);
        }
        currentOffset += elemLen;
    }

    // Offset is at or past the end
    if (offset == currentOffset) {
        // Valid position at end - can insert here
        return ElementAtOffset(_elements.size(), 0);
    }

    return ElementAtOffset();
}

// Insert text at offset
int CtNodeContent::insertText(int offset, const Glib::ustring& text, const std::map<std::string, std::string>& attributes)
{
    if (text.empty()) {
        return offset;
    }

    auto location = getElementAtOffset(offset);

    if (!location.valid) {
        // Offset out of bounds
        if (_elements.empty() && offset == 0) {
            // Empty content, insert at start
            _elements.push_back(CtContentElement(CtTextSpan(text, attributes)));
            return static_cast<int>(text.length());
        }
        spdlog::error("insertText: offset {} out of bounds (elements={}, length={})",
                      offset, _elements.size(), length());
        throw std::out_of_range("Insert offset out of bounds");
    }

    if (location.elementIndex >= _elements.size()) {
        // Inserting at end
        if (!_elements.empty()) {
            auto& lastElem = _elements.back();
            if (lastElem.isTextSpan() && _spansHaveSameAttributes(lastElem.textSpan, CtTextSpan("", attributes))) {
                // Append to last span
                lastElem.textSpan.text += text;
                return offset + static_cast<int>(text.length());
            }
        }
        // Add new span at end
        _elements.push_back(CtContentElement(CtTextSpan(text, attributes)));
        return offset + static_cast<int>(text.length());
    }

    auto& elem = _elements[location.elementIndex];

    if (elem.isWidget()) {
        // Inserting adjacent to widget - insert new span
        if (location.offsetInElement == 0) {
            // Insert before widget
            _elements.insert(_elements.begin() + location.elementIndex, CtContentElement(CtTextSpan(text, attributes)));
        } else {
            // Insert after widget
            _elements.insert(_elements.begin() + location.elementIndex + 1, CtContentElement(CtTextSpan(text, attributes)));
        }
        _mergeAdjacentSpans(location.elementIndex);
        return offset + static_cast<int>(text.length());
    }

    // Inserting into text span
    auto& span = elem.textSpan;

    if (_spansHaveSameAttributes(span, CtTextSpan("", attributes))) {
        // Same attributes - insert directly
        span.text.insert(location.offsetInElement, text);
        return offset + static_cast<int>(text.length());
    }

    // Different attributes - split the span
    if (location.offsetInElement == 0) {
        // Insert at start of span
        _elements.insert(_elements.begin() + location.elementIndex, CtContentElement(CtTextSpan(text, attributes)));
        _mergeAdjacentSpans(location.elementIndex);
    } else if (location.offsetInElement == span.text.length()) {
        // Insert at end of span
        _elements.insert(_elements.begin() + location.elementIndex + 1, CtContentElement(CtTextSpan(text, attributes)));
        _mergeAdjacentSpans(location.elementIndex);
    } else {
        // Split in middle
        Glib::ustring afterText = span.text.substr(location.offsetInElement);
        span.text = span.text.substr(0, location.offsetInElement);

        _elements.insert(_elements.begin() + location.elementIndex + 1, CtContentElement(CtTextSpan(text, attributes)));
        _elements.insert(_elements.begin() + location.elementIndex + 2, CtContentElement(CtTextSpan(afterText, span.attributes)));

        _mergeAdjacentSpans(location.elementIndex);
    }

    return offset + static_cast<int>(text.length());
}

// Delete range
CtDeletedContent CtNodeContent::deleteRange(int start, int length)
{
    CtDeletedContent deleted(start);

    if (length <= 0 || _elements.empty()) {
        return deleted;
    }

    auto startLoc = getElementAtOffset(start);
    if (!startLoc.valid) {
        return deleted;
    }

    int remaining = length;
    size_t currentIndex = startLoc.elementIndex;

    // Handle partial deletion at start if we're in the middle of a text span
    if (startLoc.elementIndex < _elements.size() && startLoc.offsetInElement > 0) {
        auto& elem = _elements[currentIndex];

        if (elem.isTextSpan()) {
            // Partial deletion within a text span
            auto& span = elem.textSpan;
            int elemLen = static_cast<int>(span.length());
            int availableInElem = elemLen - static_cast<int>(startLoc.offsetInElement);
            int toDelete = std::min(remaining, availableInElem);

            Glib::ustring deletedText = span.text.substr(startLoc.offsetInElement, toDelete);
            deleted.elements.push_back(CtContentElement(CtTextSpan(deletedText, span.attributes)));

            span.text.erase(startLoc.offsetInElement, toDelete);
            remaining -= toDelete;

            if (span.text.empty()) {
                _elements.erase(_elements.begin() + currentIndex);
                // currentIndex stays the same - next element is now at this index
            } else {
                ++currentIndex;  // Move to next element
            }
        } else {
            // Widget at offset 0 - will be handled in main loop
        }
    }

    // Delete whole elements or parts from their start
    while (remaining > 0 && currentIndex < _elements.size()) {
        auto& elem = _elements[currentIndex];
        int elemLen = static_cast<int>(elem.length());

        if (remaining >= elemLen) {
            // Delete entire element
            deleted.elements.push_back(elem);
            _elements.erase(_elements.begin() + currentIndex);
            remaining -= elemLen;
            // currentIndex stays the same - next element is now at this index
        } else {
            // Partial deletion at end (only possible for text spans)
            if (elem.isTextSpan()) {
                auto& span = elem.textSpan;
                Glib::ustring deletedText = span.text.substr(0, remaining);
                deleted.elements.push_back(CtContentElement(CtTextSpan(deletedText, span.attributes)));
                span.text.erase(0, remaining);
                remaining = 0;
            } else {
                // Widget - can't partially delete, shouldn't reach here
                break;
            }
        }
    }

    // Merge adjacent spans with same attributes at deletion point
    if (startLoc.elementIndex > 0) {
        size_t mergeIndex = std::min(startLoc.elementIndex - 1, _elements.size() - 1);
        if (mergeIndex < _elements.size()) {
            _mergeAdjacentSpans(mergeIndex);
        }
    } else if (!_elements.empty()) {
        _mergeAdjacentSpans(0);
    }

    return deleted;
}

// Extract range without modifying content (for capturing deleted content before buffer deletion)
CtDeletedContent CtNodeContent::extractRange(int start, int length) const
{
    CtDeletedContent extracted(start);

    if (length <= 0 || _elements.empty()) {
        return extracted;
    }

    auto startLoc = getElementAtOffset(start);
    if (!startLoc.valid) {
        return extracted;
    }

    int remaining = length;
    size_t currentIndex = startLoc.elementIndex;

    // Handle partial extraction at start if we're in the middle of a text span
    if (startLoc.elementIndex < _elements.size() && startLoc.offsetInElement > 0) {
        const auto& elem = _elements[currentIndex];

        if (elem.isTextSpan()) {
            const auto& span = elem.textSpan;
            int elemLen = static_cast<int>(span.length());
            int availableInElem = elemLen - static_cast<int>(startLoc.offsetInElement);
            int toExtract = std::min(remaining, availableInElem);

            Glib::ustring extractedText = span.text.substr(startLoc.offsetInElement, toExtract);
            extracted.elements.push_back(CtContentElement(CtTextSpan(extractedText, span.attributes)));

            remaining -= toExtract;
            ++currentIndex;
        }
    }

    // Extract whole elements or parts from their start
    while (remaining > 0 && currentIndex < _elements.size()) {
        const auto& elem = _elements[currentIndex];
        int elemLen = static_cast<int>(elem.length());

        if (remaining >= elemLen) {
            // Extract entire element
            extracted.elements.push_back(elem);
            remaining -= elemLen;
            ++currentIndex;
        } else {
            // Partial extraction at end (only possible for text spans)
            if (elem.isTextSpan()) {
                const auto& span = elem.textSpan;
                Glib::ustring extractedText = span.text.substr(0, remaining);
                extracted.elements.push_back(CtContentElement(CtTextSpan(extractedText, span.attributes)));
                remaining = 0;
            } else {
                // Widget - can't partially extract
                break;
            }
        }
    }

    return extracted;
}

// Apply formatting to range
CtFormatChange CtNodeContent::applyFormat(int start, int length, const std::string& attribute, const std::string& value)
{
    CtFormatChange change;

    if (length <= 0 || _elements.empty()) {
        return change;
    }

    auto startLoc = getElementAtOffset(start);
    if (!startLoc.valid) {
        return change;
    }

    int remaining = length;
    size_t currentIndex = startLoc.elementIndex;

    // Split at start if needed
    if (startLoc.elementIndex < _elements.size() && startLoc.offsetInElement > 0) {
        auto& elem = _elements[currentIndex];
        if (elem.isTextSpan()) {
            _splitSpanAtOffset(currentIndex, startLoc.offsetInElement);
            ++currentIndex;
        }
    }

    // Split at end if needed
    auto endLoc = getElementAtOffset(start + length);
    if (endLoc.valid && endLoc.elementIndex < _elements.size() && endLoc.offsetInElement > 0) {
        auto& elem = _elements[endLoc.elementIndex];
        if (elem.isTextSpan()) {
            _splitSpanAtOffset(endLoc.elementIndex, endLoc.offsetInElement);
        }
    }

    // Apply format to spans in range
    currentIndex = getElementAtOffset(start).elementIndex;

    while (remaining > 0 && currentIndex < _elements.size()) {
        auto& elem = _elements[currentIndex];
        int elemLen = static_cast<int>(elem.length());

        if (elem.isTextSpan()) {
            auto& span = elem.textSpan;
            bool hadAttribute = span.hasAttribute(attribute);
            std::string oldValue = span.getAttribute(attribute);

            change.changes.emplace_back(currentIndex, oldValue, hadAttribute);
            span.setAttribute(attribute, value);
        }

        remaining -= elemLen;
        ++currentIndex;
    }

    // Merge adjacent spans with same attributes
    if (startLoc.elementIndex > 0) {
        _mergeAdjacentSpans(startLoc.elementIndex - 1);
    }

    return change;
}

// Remove formatting from range
CtFormatChange CtNodeContent::removeFormat(int start, int length, const std::string& attribute)
{
    CtFormatChange change;

    if (length <= 0 || _elements.empty()) {
        return change;
    }

    auto startLoc = getElementAtOffset(start);
    if (!startLoc.valid) {
        return change;
    }

    int remaining = length;
    size_t currentIndex = startLoc.elementIndex;

    // Split at start if needed
    if (startLoc.elementIndex < _elements.size() && startLoc.offsetInElement > 0) {
        auto& elem = _elements[currentIndex];
        if (elem.isTextSpan()) {
            _splitSpanAtOffset(currentIndex, startLoc.offsetInElement);
            ++currentIndex;
        }
    }

    // Split at end if needed
    auto endLoc = getElementAtOffset(start + length);
    if (endLoc.valid && endLoc.elementIndex < _elements.size() && endLoc.offsetInElement > 0) {
        auto& elem = _elements[endLoc.elementIndex];
        if (elem.isTextSpan()) {
            _splitSpanAtOffset(endLoc.elementIndex, endLoc.offsetInElement);
        }
    }

    // Remove format from spans in range
    currentIndex = getElementAtOffset(start).elementIndex;

    while (remaining > 0 && currentIndex < _elements.size()) {
        auto& elem = _elements[currentIndex];
        int elemLen = static_cast<int>(elem.length());

        if (elem.isTextSpan()) {
            auto& span = elem.textSpan;
            bool hadAttribute = span.hasAttribute(attribute);
            std::string oldValue = span.getAttribute(attribute);

            change.changes.emplace_back(currentIndex, oldValue, hadAttribute);
            span.removeAttribute(attribute);
        }

        remaining -= elemLen;
        ++currentIndex;
    }

    // Merge adjacent spans with same attributes
    if (startLoc.elementIndex > 0) {
        _mergeAdjacentSpans(startLoc.elementIndex - 1);
    }

    return change;
}

// Returns true if any text span in [start, start+length) has the given attribute set
bool CtNodeContent::hasAttributeInRange(int start, int length, const std::string& attribute) const
{
    if (length <= 0 || _elements.empty()) return false;

    int pos = 0;
    for (const auto& elem : _elements) {
        int elemLen = static_cast<int>(elem.length());
        if (pos + elemLen > start && pos < start + length) {
            if (elem.isTextSpan() && elem.textSpan.hasAttribute(attribute)) {
                return true;
            }
        }
        pos += elemLen;
        if (pos >= start + length) break;
    }
    return false;
}

// Returns true if any text span in [start, start+length) has the attribute set to value
bool CtNodeContent::hasAttributeValueInRange(int start, int length, const std::string& attribute, const std::string& value) const
{
    if (length <= 0 || _elements.empty()) return false;

    int pos = 0;
    for (const auto& elem : _elements) {
        int elemLen = static_cast<int>(elem.length());
        if (pos + elemLen > start && pos < start + length) {
            if (elem.isTextSpan() && elem.textSpan.getAttribute(attribute) == value) {
                return true;
            }
        }
        pos += elemLen;
        if (pos >= start + length) break;
    }
    return false;
}

bool CtNodeContent::setWidgetContentData(int charOffset, const std::string& newContent)
{
    for (auto& elem : _elements) {
        if (elem.isWidget() && elem.widget.getCharOffset() == charOffset) {
            elem.widget.contentData = newContent;
            return true;
        }
    }
    return false;
}

bool CtNodeContent::setWidgetTableCell(int charOffset, size_t row, size_t col, const Glib::ustring& newText)
{
    for (auto& elem : _elements) {
        if (elem.isWidget() && elem.widget.getCharOffset() == charOffset) {
            if (elem.widget.hasTableData() &&
                row < elem.widget.tableData.size() &&
                col < elem.widget.tableData[row].size()) {
                elem.widget.tableData[row][col] = newText;
                return true;
            }
            return false;
        }
    }
    return false;
}

CtWidgetDesc CtNodeContent::replaceWidget(int charOffset, const CtWidgetDesc& newWidget)
{
    for (auto& elem : _elements) {
        if (elem.isWidget() && elem.widget.getCharOffset() == charOffset) {
            CtWidgetDesc old = elem.widget;
            elem.widget = newWidget;
            return old;
        }
    }
    return CtWidgetDesc(); // not found — type==None
}

CtWidgetDesc CtNodeContent::getWidgetDescAt(int charOffset) const
{
    for (const auto& elem : _elements) {
        if (elem.isWidget() && elem.widget.getCharOffset() == charOffset) {
            return elem.widget;
        }
    }
    return CtWidgetDesc(); // not found — type==None
}

// Insert widget
int CtNodeContent::insertWidget(int offset, const CtWidgetDesc& widget)
{
    auto location = getElementAtOffset(offset);

    if (!location.valid) {
        if (_elements.empty() && offset == 0) {
            _elements.push_back(CtContentElement(widget));
            return 0;
        }
        throw std::out_of_range("Insert widget offset out of bounds");
    }

    if (location.elementIndex >= _elements.size()) {
        _elements.push_back(CtContentElement(widget));
        return offset;
    }

    auto& elem = _elements[location.elementIndex];

    if (elem.isWidget() || location.offsetInElement == 0) {
        _elements.insert(_elements.begin() + location.elementIndex, CtContentElement(widget));
    } else if (elem.isTextSpan() && location.offsetInElement == elem.textSpan.text.length()) {
        _elements.insert(_elements.begin() + location.elementIndex + 1, CtContentElement(widget));
    } else {
        // Split text span
        _splitSpanAtOffset(location.elementIndex, location.offsetInElement);
        _elements.insert(_elements.begin() + location.elementIndex + 1, CtContentElement(widget));
    }

    return offset;
}

// Remove widget
CtWidgetDesc CtNodeContent::removeWidget(int offset)
{
    auto location = getElementAtOffset(offset);

    if (!location.valid || location.elementIndex >= _elements.size()) {
        return CtWidgetDesc();
    }

    auto& elem = _elements[location.elementIndex];
    if (!elem.isWidget()) {
        return CtWidgetDesc();
    }

    CtWidgetDesc widget = elem.widget;
    _elements.erase(_elements.begin() + location.elementIndex);

    // Merge adjacent text spans
    if (location.elementIndex > 0 && location.elementIndex < _elements.size()) {
        _mergeAdjacentSpans(location.elementIndex - 1);
    }

    return widget;
}

// Re-insert deleted content
void CtNodeContent::reinsertContent(const CtDeletedContent& deleted)
{
    if (deleted.isEmpty()) {
        return;
    }

    int offset = deleted.startOffset;
    for (const auto& elem : deleted.elements) {
        auto location = getElementAtOffset(offset);
        if (!location.valid) {
            if (offset == 0 && _elements.empty()) {
                _elements.push_back(elem);
            } else {
                _elements.push_back(elem);
            }
        } else if (location.elementIndex >= _elements.size()) {
            _elements.push_back(elem);
        } else {
            _elements.insert(_elements.begin() + location.elementIndex, elem);
        }
        offset += static_cast<int>(elem.length());
    }

    // Merge adjacent spans
    if (deleted.startOffset > 0) {
        auto loc = getElementAtOffset(deleted.startOffset - 1);
        if (loc.valid) {
            _mergeAdjacentSpans(loc.elementIndex);
        }
    }
}

// Restore previous format state
void CtNodeContent::restoreFormat(int start, int /*length*/, const std::string& attribute, const CtFormatChange& change)
{
    for (const auto& spanChange : change.changes) {
        if (static_cast<size_t>(spanChange.spanIndex) >= _elements.size()) {
            continue;
        }

        auto& elem = _elements[spanChange.spanIndex];
        if (!elem.isTextSpan()) {
            continue;
        }

        if (spanChange.hadAttribute) {
            elem.textSpan.setAttribute(attribute, spanChange.oldValue);
        } else {
            elem.textSpan.removeAttribute(attribute);
        }
    }

    // Merge adjacent spans
    auto loc = getElementAtOffset(start);
    if (loc.valid && loc.elementIndex > 0) {
        _mergeAdjacentSpans(loc.elementIndex - 1);
    }
}

// Helper: Merge adjacent text spans with same attributes
void CtNodeContent::_mergeAdjacentSpans(size_t startIndex)
{
    if (startIndex >= _elements.size()) {
        return;
    }

    while (startIndex < _elements.size() - 1) {
        auto& current = _elements[startIndex];
        auto& next = _elements[startIndex + 1];

        if (current.isTextSpan() && next.isTextSpan() &&
            _spansHaveSameAttributes(current.textSpan, next.textSpan)) {
            current.textSpan.text += next.textSpan.text;
            _elements.erase(_elements.begin() + startIndex + 1);
        } else {
            ++startIndex;
        }
    }
}

// Helper: Split text span at offset
void CtNodeContent::_splitSpanAtOffset(size_t elementIndex, size_t offsetInElement)
{
    if (elementIndex >= _elements.size()) {
        return;
    }

    auto& elem = _elements[elementIndex];
    if (!elem.isTextSpan()) {
        return;
    }

    auto& span = elem.textSpan;
    if (offsetInElement == 0 || offsetInElement >= span.text.length()) {
        return;
    }

    Glib::ustring afterText = span.text.substr(offsetInElement);
    span.text = span.text.substr(0, offsetInElement);

    _elements.insert(_elements.begin() + elementIndex + 1,
                     CtContentElement(CtTextSpan(afterText, span.attributes)));
}

// Helper: Check if two spans have same attributes
bool CtNodeContent::_spansHaveSameAttributes(const CtTextSpan& a, const CtTextSpan& b) const
{
    return a.attributes == b.attributes;
}

// Parse XML to create content model
CtNodeContent CtNodeContent::fromXml(const Glib::ustring& xml, CtMainWin* /*pCtMainWin*/)
{
    CtNodeContent content;

    if (xml.empty()) {
        return content;
    }

    try {
        // Remove XML declaration if present (causes issues when wrapping)
        Glib::ustring cleanXml = xml;
        size_t declPos = cleanXml.find("<?xml");
        if (declPos != Glib::ustring::npos) {
            size_t declEnd = cleanXml.find("?>", declPos);
            if (declEnd != Glib::ustring::npos) {
                cleanXml.erase(declPos, declEnd - declPos + 2);
            }
        }

        // Trim whitespace
        size_t startPos = cleanXml.find_first_not_of(" \t\n\r");
        if (startPos != Glib::ustring::npos) {
            cleanXml = cleanXml.substr(startPos);
        }

        // Check if XML already has <node> root - if so, use it directly
        // Otherwise wrap in <node> for parsing
        Glib::ustring wrappedXml;
        if (cleanXml.find("<node>") == 0 || cleanXml.find("<node ") == 0) {
            // Already has <node> root, use as-is
            wrappedXml = cleanXml;
        } else {
            // Wrap in <node> root
            wrappedXml = "<node>" + cleanXml + "</node>";
        }

        // Parse XML
        xmlpp::DomParser parser;
        parser.parse_memory(wrappedXml);

        if (!parser) {
            spdlog::error("CtNodeContent::fromXml: XML parsing failed");
            return content;
        }

        const xmlpp::Element* root = parser.get_document()->get_root_node();
        if (!root) {
            spdlog::error("CtNodeContent::fromXml: no root element");
            return content;
        }

        // Iterate through child nodes
        for (const xmlpp::Node* node : root->get_children()) {
            const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(node);
            if (!element) {
                continue;
            }

            Glib::ustring nodeName = element->get_name();

            if (nodeName == "rich_text") {
                // Parse text span
                std::map<std::string, std::string> attributes;

                // Extract attributes
                for (const auto& attr : element->get_attributes()) {
                    attributes[attr->get_name()] = attr->get_value();
                }

                // Extract text content (unescaping is handled by libxml++)
                Glib::ustring text;
                const xmlpp::TextNode* textNode = element->get_child_text();
                if (textNode) {
                    text = textNode->get_content();
                }

                // Insert text span into content
                content.insertText(content.length(), text, attributes);
            }
            else if (nodeName == "encoded_png" || nodeName == "codebox" ||
                     nodeName == "table") {
                // Parse widget
                CtWidgetDesc widget;

                // Determine widget type
                if (nodeName == "encoded_png") {
                    widget.type = CtAnchWidgType::ImagePng;
                } else if (nodeName == "codebox") {
                    widget.type = CtAnchWidgType::CodeBox;
                } else if (nodeName == "table") {
                    // Determine table type from attributes (default to heavy)
                    const Glib::ustring isLightStr = element->get_attribute_value("is_light");
                    widget.type = (!isLightStr.empty() && isLightStr == "1")
                                  ? CtAnchWidgType::TableLight
                                  : CtAnchWidgType::TableHeavy;
                }

                // Extract properties from XML attributes
                for (const auto& attr : element->get_attributes()) {
                    widget.setProperty(attr->get_name(), attr->get_value());
                }

                // Extract content based on widget type
                if (nodeName == "table") {
                    // Parse table cell data from <row><cell> structure
                    for (xmlpp::Node* pNodeRow : element->get_children("row")) {
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
                            widget.tableData.push_back(row);
                        }
                    }
                    // Move header row from last to first (same as storage XML helper does)
                    if (!widget.tableData.empty()) {
                        widget.tableData.insert(widget.tableData.begin(), widget.tableData.back());
                        widget.tableData.pop_back();
                    }
                } else {
                    // Extract text content for other widgets (images, codeboxes)
                    const xmlpp::TextNode* textNode = element->get_child_text();
                    if (textNode) {
                        Glib::ustring textContent = textNode->get_content();
                        if (!textContent.empty()) {
                            widget.contentData = textContent.raw();
                            // Keep in properties for backward compatibility during transition
                            widget.setProperty("_content", textContent.raw());
                        }
                    }
                }

                // Insert widget at its char_offset position so the model
                // is correct regardless of whether the XML has text and
                // widgets interleaved or all text first (as getBufferContentAsXml generates).
                {
                    const int charOff = widget.getCharOffset();
                    content.insertWidget(charOff, widget);
                }
            }
        }

        spdlog::debug("CtNodeContent::fromXml: parsed {} elements", content.getElements().size());
        return content;
    }
    catch (const std::exception& e) {
        spdlog::error("CtNodeContent::fromXml: exception: {}", e.what());
        return content;
    }
}

// Serialize content model to XML
Glib::ustring CtNodeContent::toXml() const
{
    if (_elements.empty()) {
        return "";
    }

    Glib::ustring xml;

    for (const auto& elem : _elements) {
        if (elem.isTextSpan()) {
            const auto& span = elem.textSpan;

            // Build <rich_text> element
            xml += "<rich_text";

            // Add attributes in TAG_PROPERTIES order for consistent output
            for (const auto& tag_prop : CtConst::TAG_PROPERTIES) {
                auto it = span.attributes.find(std::string(tag_prop));
                if (it != span.attributes.end() && !it->second.empty()) {
                    xml += " " + it->first + "=\"" + it->second + "\"";
                }
            }

            xml += ">";

            // Add text content (XML-escaped)
            xml += str::xml_escape(span.text);
            xml += "</rich_text>";
        }
        else {
            // Widget serialization
            const auto& widget = elem.widget;

            if (widget.type == CtAnchWidgType::ImagePng ||
                widget.type == CtAnchWidgType::ImageAnchor ||
                widget.type == CtAnchWidgType::ImageLatex ||
                widget.type == CtAnchWidgType::ImageEmbFile) {
                xml += "<encoded_png";
                // Output attributes except _content (which is the element's text content)
                for (const auto& prop : widget.properties) {
                    if (prop.first != "_content") {
                        xml += " " + prop.first + "=\"" + prop.second + "\"";
                    }
                }
                // Output text content if present (base64 encoded image data)
                const std::string content = widget.getContent();
                if (!content.empty()) {
                    xml += ">";
                    xml += content;
                    xml += "</encoded_png>";
                } else {
                    xml += "/>";
                }
            }
            else if (widget.type == CtAnchWidgType::CodeBox) {
                xml += "<codebox";
                for (const auto& prop : widget.properties) {
                    if (prop.first != "_content") {
                        xml += " " + prop.first + "=\"" + prop.second + "\"";
                    }
                }
                // Output text content if present (codebox source code)
                const std::string content = widget.getContent();
                if (!content.empty()) {
                    xml += ">";
                    xml += str::xml_escape(content);
                    xml += "</codebox>";
                } else {
                    xml += "/>";
                }
            }
            else if (widget.type == CtAnchWidgType::TableLight || widget.type == CtAnchWidgType::TableHeavy) {
                xml += "<table";
                for (const auto& prop : widget.properties) {
                    if (prop.first != "_content") {
                        xml += " " + prop.first + "=\"" + prop.second + "\"";
                    }
                }

                // Output table data from tableData vector if present, otherwise fallback to _content
                if (!widget.tableData.empty()) {
                    xml += ">";

                    // Write rows, but put header (first row) at the end
                    // This matches the format used by CtTableHeavy::_populate_xml_rows_cells
                    for (size_t rowIdx = 1; rowIdx < widget.tableData.size(); ++rowIdx) {
                        xml += "<row>";
                        for (const auto& cell : widget.tableData[rowIdx]) {
                            xml += "<cell>";
                            xml += str::xml_escape(cell);
                            xml += "</cell>";
                        }
                        xml += "</row>";
                    }

                    // Write header row last
                    if (!widget.tableData[0].empty()) {
                        xml += "<row>";
                        for (const auto& cell : widget.tableData[0]) {
                            xml += "<cell>";
                            xml += str::xml_escape(cell);
                            xml += "</cell>";
                        }
                        xml += "</row>";
                    }

                    xml += "</table>";
                } else {
                    // Fallback to _content property for compatibility
                    auto contentIt = widget.properties.find("_content");
                    if (contentIt != widget.properties.end() && !contentIt->second.empty()) {
                        xml += ">";
                        xml += contentIt->second;
                        xml += "</table>";
                    } else {
                        xml += "/>";
                    }
                }
            }
        }
    }

    return xml;
}

// ============================================================================
// Buffer Conversion Functions (Phase 3)
// ============================================================================

// Extract formatting attributes from a GTK TextIter
// Uses the same logic as CtTextIterUtil::rich_text_attributes_update
// Made non-static so it can be used from ct_text_commands.cc for signal capture
std::map<std::string, std::string> extractAttributesFromIter(const Gtk::TextIter& iter)
{
    std::map<std::string, std::string> attributes;

    // Get all tags at this position
    auto tags = iter.get_tags();

    for (const auto& tag : tags) {
        Glib::ustring tag_name = tag->property_name();

        if (tag_name.empty() || tag_name == CtConst::GTKSPELLCHECK_TAG_NAME) {
            continue;
        }

        // Extract attribute name and value based on tag name prefix
        // Pattern: "prefix_value", extract the value after the prefix
        if (str::startswith(tag_name, CtConst::TAG_WEIGHT_PREFIX)) {
            attributes[CtConst::TAG_WEIGHT] = tag_name.substr(7); // "weight_" = 7 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_FOREGROUND_PREFIX)) {
            attributes[CtConst::TAG_FOREGROUND] = tag_name.substr(11); // "foreground_" = 11 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_BACKGROUND_PREFIX)) {
            attributes[CtConst::TAG_BACKGROUND] = tag_name.substr(11); // "background_" = 11 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_SCALE_PREFIX)) {
            attributes[CtConst::TAG_SCALE] = tag_name.substr(6); // "scale_" = 6 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_INVISIBLE_PREFIX)) {
            attributes[CtConst::TAG_INVISIBLE] = tag_name.substr(10); // "invisible_" = 10 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_JUSTIFICATION_PREFIX)) {
            attributes[CtConst::TAG_JUSTIFICATION] = tag_name.substr(14); // "justification_" = 14 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_STYLE_PREFIX)) {
            attributes[CtConst::TAG_STYLE] = tag_name.substr(6); // "style_" = 6 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_UNDERLINE_PREFIX)) {
            attributes[CtConst::TAG_UNDERLINE] = tag_name.substr(10); // "underline_" = 10 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_STRIKETHROUGH_PREFIX)) {
            attributes[CtConst::TAG_STRIKETHROUGH] = tag_name.substr(14); // "strikethrough_" = 14 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_INDENT_PREFIX)) {
            attributes[CtConst::TAG_INDENT] = tag_name.substr(7); // "indent_" = 7 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_LINK_PREFIX)) {
            attributes[CtConst::TAG_LINK] = tag_name.substr(5); // "link_" = 5 chars
        }
        else if (str::startswith(tag_name, CtConst::TAG_FAMILY_PREFIX)) {
            attributes[CtConst::TAG_FAMILY] = tag_name.substr(7); // "family_" = 7 chars
        }
    }

    return attributes;
}

// Helper function to check if two attribute maps are the same
static bool attributesEqual(const std::map<std::string, std::string>& a,
                           const std::map<std::string, std::string>& b)
{
    return a == b;
}

// buildContentFromBuffer() is implemented in ct_buffer_converter.cc
// (needs access to CtAnchoredWidget methods, solved via ct_main_win.h inclusion there)

