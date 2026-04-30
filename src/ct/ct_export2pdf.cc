/*
 * ct_export2pdf.cc
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

#include "ct_export2pdf.h"
#include "ct_dialogs.h"
#include <utility>
#include <pango/pango-attributes.h>
#include <pango/pango-layout.h>

namespace {

Glib::ustring generate_tag(gint64 node_id, const Glib::ustring& anchor_name)
{
    if (anchor_name.empty()) {
        return fmt::format("n.{}", node_id);
    }
    return fmt::format("n.{}.{}", node_id, std::hash<std::string>{}(anchor_name.raw()));
}

} // namespace (anonymous)

CtExport2Pango::CtExport2Pango(CtMainWin* pCtMainWin)
 : _pCtMainWin{pCtMainWin}
 , _pCtConfig{pCtMainWin->get_ct_config()}
{
}

// Given a treestore iter returns the Pango rich text
void CtExport2Pango::pango_get_from_treestore_node(CtTreeIter node_iter, int sel_start, int sel_end, std::vector<CtPangoObjectPtr>& out_slots)
{
    Glib::RefPtr<Gtk::TextBuffer> curr_buffer = node_iter.get_node_text_buffer();

    std::list<CtAnchoredWidget*> out_widgets = node_iter.get_anchored_widgets(sel_start, sel_end);
    int start_text_offset = sel_start < 1 ? 0 : sel_start;
    for (CtAnchoredWidget* widget : out_widgets) {
        const int widgetOffset = widget->getOffset();
        _pango_process_slot(start_text_offset, widgetOffset, curr_buffer, out_slots);

        int widget_indent{0};
        CtCurrAttributesMap emtpy_attributes, iter_attributes;
        const Gtk::TextIter widgetTextIter = curr_buffer->get_iter_at_offset(widgetOffset);
        if (CtTextIterUtil::rich_text_attributes_update(widgetTextIter, emtpy_attributes, iter_attributes)) {
            auto attr_iter = iter_attributes.find(CtConst::TAG_INDENT);
            if (attr_iter != iter_attributes.end()) {
                widget_indent = not attr_iter->second.empty() ? CtConst::INDENT_MARGIN * std::stoi(attr_iter->second) : 0;
            }
        }
        const PangoDirection pango_dir = CtTextIterUtil::get_pango_direction(widgetTextIter);

        if (auto anchor = dynamic_cast<CtImageAnchor*>(widget)) {
            out_slots.emplace_back(std::make_shared<CtPangoDest>(
                "<sup> </sup>", // ⚓
                CtConst::RICH_TEXT_ID,
                widget_indent,
                "name='" + generate_tag(node_iter.get_node_id(), anchor->get_anchor_name()) + "'",
                pango_dir));
        }
        else {
            out_slots.emplace_back(std::make_shared<CtPangoWidget>(widget, widget_indent, pango_dir));
        }
        start_text_offset = widgetOffset;
    }

    int end_offset = sel_end < 0 ? curr_buffer->end().get_offset() : sel_end;
    _pango_process_slot(start_text_offset, end_offset, curr_buffer, out_slots);
}

// Get rich text from syntax highlighted code node
Glib::ustring CtExport2Pango::pango_get_from_code_buffer(Glib::RefPtr<Gtk::TextBuffer> code_buffer,
                                                         int sel_start,
                                                         int sel_end,
                                                         const std::string& syntax_highlighting)
{
    Gtk::TextIter curr_iter = sel_start < 0 ? code_buffer->begin() : code_buffer->get_iter_at_offset(sel_start);
    Gtk::TextIter end_iter = sel_start < 0 ? code_buffer->end() : code_buffer->get_iter_at_offset(sel_end);
    Glib::ustring indentation;
    bool indentation_force_spaces{false};
    if (syntax_highlighting != CtConst::PLAIN_TEXT_ID) {
        _pCtMainWin->apply_syntax_highlighting(code_buffer, syntax_highlighting, false/*forceReApply*/);
        gtk_source_buffer_ensure_highlight(GTK_SOURCE_BUFFER(code_buffer->gobj()), curr_iter.gobj(), end_iter.gobj());
        indentation = str::repeat(CtConst::CHAR_SPACE, _pCtConfig->tabsWidth);
        indentation_force_spaces = true;
    }
    Glib::ustring pango_text;
    Glib::ustring former_tag_str = CtConst::COLOR_48_BLACK;
    bool span_opened{false};
    bool is_indentation{true};
    for (;;) {
        std::vector<Glib::RefPtr<Gtk::TextTag>> curr_tags = curr_iter.get_tags();
        if (curr_tags.size() > 0) {
            Glib::ustring curr_tag_str{CtConst::COLOR_48_BLACK};
            int font_weight = curr_tags[0]->property_weight().get_value();
            for (Glib::RefPtr<Gtk::TextTag>& curr_tag : curr_tags) {
                if (curr_tag->property_foreground_set()) {
                    Glib::ustring tmpTagStr = curr_tag->property_foreground_rgba().get_value().to_string();
                    if (tmpTagStr != curr_tag_str) {
                        curr_tag_str = tmpTagStr;
                        font_weight = curr_tag->property_weight().get_value();
                        break;
                    }
#if 0
                    spdlog::debug("{} TAG FG={} BG={} WEIGHT={}", curr_iter.get_offset(),
                        curr_tag->property_foreground_rgba().get_value().to_string().c_str(),
                        curr_tag->property_background_rgba().get_value().to_string().c_str(),
                        curr_tag->property_weight().get_value());
#endif
                }
            }
            if (curr_tag_str == CtConst::COLOR_48_BLACK) {
                if (former_tag_str != curr_tag_str) {
                    former_tag_str = curr_tag_str;
                    // end of tag
                    pango_text += "</span>";
                    span_opened = false;
                }
            }
            else {
                if (former_tag_str != curr_tag_str) {
                    former_tag_str = curr_tag_str;
                    if (span_opened) pango_text += "</span>";
                    // start of tag
                    Glib::ustring color = CtRgbUtil::rgb_to_no_white(curr_tag_str);
                    color = CtRgbUtil::get_rgb24str_from_str_any(color);
                    pango_text += "<span foreground=\"" + color + "\" font_weight=\"" + std::to_string(font_weight) + "\">";
                    span_opened = true;
                }
            }
        }
        else if (span_opened) {
            span_opened = false;
            former_tag_str = CtConst::COLOR_48_BLACK;
            pango_text += "</span>";
        }
        const auto curr_char = curr_iter.get_char();
        if (indentation_force_spaces) {
            if (is_indentation) {
                if ('\t' == curr_char) {
                    pango_text += indentation;
                }
                else {
                    is_indentation = false;
                }
            }
            if (not is_indentation) {
                pango_text += str::xml_escape(Glib::ustring{1, curr_char});
                if ('\n' == curr_char) {
                    is_indentation = true;
                }
            }
        }
        else {
            pango_text += str::xml_escape(Glib::ustring{1, curr_char});
        }
        if (not curr_iter.forward_char() or (sel_end >= 0 && curr_iter.get_offset() > sel_end)) {
            if (span_opened) pango_text += "</span>";
            break;
        }
    }
    //if (pango_text.empty() || pango_text[pango_text.size()-1] != g_utf8_get_char(CtConst::CHAR_NEWLINE))
    //    pango_text += CtConst::CHAR_NEWLINE;
    return pango_text;
}

// Process a Single Pango Slot
Glib::ustring CtExport2Pango::pango_get_from_rich_buffer(Glib::RefPtr<Gtk::TextBuffer> curr_buffer,
                                                         int start_offset,
                                                         int end_offset)
{
    if (not curr_buffer) return {};

    // Walk the buffer; process text slots between child anchors and inject a literal
    // U+FFFC at each anchor position. Callers (rich-table cells) use the U+FFFC as
    // an inline placeholder where Pango shape attributes reserve space for images.
    const int begin_off = start_offset;
    const int end_off = end_offset == -1 ? curr_buffer->end().get_offset() : end_offset;

    std::vector<int> anchorOffsets;
    {
        Gtk::TextIter it = curr_buffer->get_iter_at_offset(begin_off);
        while (it.get_offset() < end_off) {
            if (it.get_child_anchor()) {
                anchorOffsets.push_back(it.get_offset());
            }
            if (not it.forward_char()) break;
        }
    }

    Glib::ustring result;
    int slot_start = begin_off;
    for (int anchor_off : anchorOffsets) {
        if (anchor_off > slot_start) {
            std::vector<CtPangoObjectPtr> slots;
            _pango_process_slot(slot_start, anchor_off, curr_buffer, slots);
            for (const auto& slot : slots) {
                if (auto pText = std::dynamic_pointer_cast<CtPangoText>(slot)) {
                    result += pText->text;
                }
            }
        }
        result += "\xEF\xBF\xBC"; // U+FFFC OBJECT REPLACEMENT CHARACTER
        slot_start = anchor_off + 1; // skip the anchor itself
    }
    if (slot_start < end_off) {
        std::vector<CtPangoObjectPtr> slots;
        _pango_process_slot(slot_start, end_off, curr_buffer, slots);
        for (const auto& slot : slots) {
            if (auto pText = std::dynamic_pointer_cast<CtPangoText>(slot)) {
                result += pText->text;
            }
        }
    }
    return result;
}

void CtExport2Pango::_pango_process_slot(int start_offset,
                                         int end_offset,
                                         Glib::RefPtr<Gtk::TextBuffer> curr_buffer,
                                         std::vector<CtPangoObjectPtr>& out_slots)
{
    CtTextIterUtil::SerializeFunc f_pango_serialize = [&](Gtk::TextIter& start_iter,
                                                          Gtk::TextIter& end_iter,
                                                          CtCurrAttributesMap& curr_attributes,
                                                          CtListInfo*/*pCurrListInfo*/)
    {
        _pango_text_serialize(start_iter, end_iter, curr_attributes, out_slots);
    };
    CtTextIterUtil::generic_process_slot(_pCtConfig,
                                         start_offset,
                                         end_offset,
                                         curr_buffer,
                                         f_pango_serialize);
}

// Adds a slice to the Pango Text
void CtExport2Pango::_pango_text_serialize(const Gtk::TextIter& start_iter,
                                           Gtk::TextIter end_iter,
                                           const CtCurrAttributesMap& curr_attributes,
                                           std::vector<CtPangoObjectPtr>& out_slots)
{
    Glib::ustring pango_attrs;
    int indent{0};
    std::string link_url;
    for (auto tag_property : CtConst::TAG_PROPERTIES) {
        if (tag_property != CtConst::TAG_JUSTIFICATION and
            tag_property != CtConst::TAG_LINK and
            not curr_attributes.at(tag_property).empty())
        {
            auto property_value = curr_attributes.at(tag_property);
            // tag names fix
            if (tag_property == CtConst::TAG_SCALE) {
                if (property_value == CtConst::TAG_PROP_VAL_SUP) {
                    pango_attrs += " size=\"xx-small\" rise=\"5000\"";
                    continue;
                }
                if (property_value == CtConst::TAG_PROP_VAL_SUB) {
                    pango_attrs += " size=\"xx-small\" rise=\"-5000\"";
                    continue;
                }
                size_t sIdx;
                if (property_value == CtConst::TAG_PROP_VAL_SMALL) sIdx = 6;
                else if (property_value == CtConst::TAG_PROP_VAL_H1) sIdx = 0;
                else if (property_value == CtConst::TAG_PROP_VAL_H2) sIdx = 1;
                else if (property_value == CtConst::TAG_PROP_VAL_H3) sIdx = 2;
                else if (property_value == CtConst::TAG_PROP_VAL_H4) sIdx = 3;
                else if (property_value == CtConst::TAG_PROP_VAL_H5) sIdx = 4;
                else if (property_value == CtConst::TAG_PROP_VAL_H6) sIdx = 5;
                else {
                    spdlog::debug("!! unexp property_value {}", property_value);
                    continue;
                }
                tag_property = "font_size";
                const auto fontSize = Pango::FontDescription{_pCtConfig->rtFont}.get_size();
                const auto pCtScalableTag = _pCtConfig->scalablesTags.at(sIdx);
                property_value = std::to_string(static_cast<unsigned>(fontSize * pCtScalableTag->scale));
                // check if other optional configuration is applied to this scalable tag
                if (not pCtScalableTag->foreground.empty()) {
                    pango_attrs += std::string{" "} + CtConst::TAG_FOREGROUND + "=\"" + pCtScalableTag->foreground + "\"";
                }
                if (not pCtScalableTag->background.empty()) {
                    pango_attrs += std::string{" "} + CtConst::TAG_BACKGROUND + "=\"" + pCtScalableTag->background + "\"";
                }
                if (pCtScalableTag->bold) {
                    pango_attrs += std::string{" "} + CtConst::TAG_WEIGHT + "=\"" + CtConst::TAG_PROP_VAL_HEAVY + "\"";
                }
                if (pCtScalableTag->italic) {
                    pango_attrs += std::string{" "} + CtConst::TAG_STYLE + "=\"" + CtConst::TAG_PROP_VAL_ITALIC + "\"";
                }
                if (pCtScalableTag->underline) {
                    pango_attrs += std::string{" "} + CtConst::TAG_UNDERLINE + "=\"" + CtConst::TAG_PROP_VAL_SINGLE + "\"";
                }
            }
            else if (tag_property == CtConst::TAG_FAMILY) {
                tag_property = "font_family";
                if (not _pCtConfig->monospaceFg.empty()) {
                    pango_attrs += std::string{" "} + CtConst::TAG_FOREGROUND + "=\"" + _pCtConfig->monospaceFg + "\"";
                }
                if (not _pCtConfig->monospaceBg.empty()) {
                    pango_attrs += std::string{" "} + CtConst::TAG_BACKGROUND + "=\"" + _pCtConfig->monospaceBg + "\"";
                }
                if (_pCtConfig->msDedicatedFont and not _pCtConfig->monospaceFont.empty()) {
                    auto fontDesc = Pango::FontDescription{_pCtConfig->monospaceFont};
                    property_value = fontDesc.get_family();
                    pango_attrs += std::string{" font_size=\""} + std::to_string(fontDesc.get_size()) + "\"";
                }
                else {
                    property_value = CtConst::TAG_PROP_VAL_MONOSPACE;
                }
            }
            else if (tag_property == CtConst::TAG_INDENT) {
                indent = not property_value.empty() ? CtConst::INDENT_MARGIN * std::stoi(property_value) : 0;
                continue;
            }
            /* comment it, but Giuseppe may want to return code with additional background color checks
            else if (tag_property == CtConst::TAG_FOREGROUND)
            {
                Glib::ustring color_no_white = CtRgbUtil::rgb_to_no_white(property_value);
                property_value = CtRgbUtil::get_rgb24str_from_str_any(color_no_white);
            }
            */
            pango_attrs += std::string{" "} + tag_property.data() + "=\"" + property_value + "\"";
        }
        if (tag_property == CtConst::TAG_LINK) {
            link_url = curr_attributes.at(tag_property);
        }
    }

    // split by \n to use Layout::set_indent properly
    std::vector<Glib::ustring> lines = str::split(start_iter.get_text(end_iter), "\n");
    for (size_t i = 0; i < lines.size(); ++i) {

        PangoDirection pango_dir = CtStrUtil::gtk_pango_find_base_dir(lines[i].c_str(), -1);

        if (not lines[i].empty()) {

            size_t j{0};
            const char* pStart = lines[i].c_str();
            do {
                const int delta_inversion = PANGO_DIRECTION_NEUTRAL != pango_dir ?
                    CtStrUtil::gtk_pango_find_start_of_dir(pStart, PANGO_DIRECTION_RTL == pango_dir ? PANGO_DIRECTION_LTR : PANGO_DIRECTION_RTL) : -1;

                auto untagged_slot = -1 == delta_inversion ? lines[i].raw().substr(j) : lines[i].raw().substr(j, static_cast<size_t>(delta_inversion));
                Glib::ustring tagged_text = str::xml_escape(untagged_slot);

                if (not pango_attrs.empty())
                    tagged_text = "<span" + pango_attrs + ">" + tagged_text + "</span>";

                if (not link_url.empty()) {
                    out_slots.emplace_back(_pango_link_url(tagged_text, link_url, indent, pango_dir));
                    //spdlog::debug("PANGO link={} indent={} pango_dir={}", tagged_text, indent, pango_dir);
                }
                else {
                    out_slots.emplace_back(std::make_shared<CtPangoText>(tagged_text, CtConst::RICH_TEXT_ID, indent, pango_dir));
                    //spdlog::debug("PANGO txt={} indent={} pango_dir={}", tagged_text, indent, pango_dir);
                }

                if (-1 == delta_inversion) {
                    j = lines[i].size();
                }
                else {
                    j += delta_inversion;
                    pango_dir = PANGO_DIRECTION_RTL == pango_dir ? PANGO_DIRECTION_LTR : PANGO_DIRECTION_RTL;
                    pStart = lines[i].c_str() + j;
                }
            }
            while (j < lines[i].size());
        }

        // add '\n' between lines
        if (lines.size() > 1 && i < lines.size() - 1) {
            out_slots.emplace_back(std::make_shared<CtPangoText>(CtConst::CHAR_NEWLINE, CtConst::RICH_TEXT_ID, indent, pango_dir));
            //spdlog::debug("PANGO nl indent={} pango_dir={}", indent, pango_dir);
        }
    }
}

std::shared_ptr<CtPangoText> CtExport2Pango::_pango_link_url(const Glib::ustring& tagged_text, const Glib::ustring& link, const int indent, const PangoDirection pango_dir)
{
    CtLinkEntry link_entry = CtMiscUtil::get_link_entry_from_property(link);
    Glib::ustring uri;
    if (CtLinkType::Node == link_entry.type) {
        uri = "dest='" + generate_tag(link_entry.node_id, link_entry.anch) + "'";
    }
    else if (CtLinkType::Webs == link_entry.type) {
        uri = "uri='" + str::xml_escape(link_entry.webs) + "'";
    }
    else if (CtLinkType::File == link_entry.type or
             CtLinkType::Fold == link_entry.type)
    {
        std::string fileOrFold = CtLinkType::File == link_entry.type ? link_entry.file.raw() : link_entry.fold.raw();
#if defined(_WIN32) || defined(__APPLE__)
        const std::string encoding = CtStrUtil::get_encoding(fileOrFold.c_str(), fileOrFold.size());
        if (encoding == "ASCII") {
            uri = (Glib::path_is_absolute(fileOrFold) ? "uri='file://":"uri='") + str::xml_escape(fs::path{fileOrFold}.string_unix()) + "'";
        }
        else {
            uri = "uri='file://non_supported_encoding_" + encoding + "'";
        }
#else /* !_WIN32 && !__APPLE__ */
        uri = (Glib::path_is_absolute(fileOrFold) ? "uri='file://":"uri='") + str::xml_escape(fileOrFold) + "'";
#endif /* !_WIN32 && !__APPLE__ */
    }
    else {
        spdlog::debug("invalid link entry {}, text {}", link.raw(), tagged_text.raw());
        return std::make_shared<CtPangoText>(tagged_text, CtConst::RICH_TEXT_ID, indent, pango_dir);
    }

    Glib::ustring blue_text = "<span fgcolor='blue'><u>" + tagged_text + "</u></span>";
    return std::make_shared<CtPangoLink>(blue_text, indent, uri, pango_dir);
}

void CtExport2Pdf::node_export_print(const fs::path& pdf_filepath, CtTreeIter tree_iter, const CtExportOptions& options, int sel_start, int sel_end)
{
    Glib::RefPtr<Gtk::TextBuffer> pTextBuffer = tree_iter.get_node_text_buffer();
    if (not pTextBuffer) {
        throw std::runtime_error(str::format(_("Failed to retrieve the content of the node '%s'"), tree_iter.get_node_name().raw()));
    }
    std::vector<CtPangoObjectPtr> pango_slots;
    if (tree_iter.get_node_is_text()) {
        CtExport2Pango{_pCtMainWin}.pango_get_from_treestore_node(tree_iter, sel_start, sel_end, pango_slots);
    }
    else {
        Glib::ustring text = CtExport2Pango{_pCtMainWin}.pango_get_from_code_buffer(
            pTextBuffer, sel_start, sel_end, tree_iter.get_node_syntax_highlighting());
        pango_slots.push_back(std::make_shared<CtPangoText>(text, tree_iter.get_node_syntax_highlighting(), 0/*indent*/, PANGO_DIRECTION_LTR));
    }

    if (options.include_node_name) {
        pango_slots.emplace(pango_slots.begin(), _generate_pango_node_name(tree_iter));
    }
    _pCtMainWin->get_ct_print().print_text(pdf_filepath, pango_slots);
}

void CtExport2Pdf::node_and_subnodes_export_print(const fs::path& pdf_filepath, CtTreeIter tree_iter, const CtExportOptions& options)
{
    std::vector<CtPangoObjectPtr> tree_pango_slots;
    _nodes_all_export_print_iter(tree_iter, options, tree_pango_slots);

    _pCtMainWin->get_ct_print().print_text(pdf_filepath, tree_pango_slots);
}

void CtExport2Pdf::tree_export_print(const fs::path& pdf_filepath, CtTreeIter tree_iter, const CtExportOptions& options)
{
    std::vector<CtPangoObjectPtr> tree_pango_slots;
    while (tree_iter) {
        _nodes_all_export_print_iter(tree_iter, options, tree_pango_slots);
        ++tree_iter;
    }
    _pCtMainWin->get_ct_print().print_text(pdf_filepath, tree_pango_slots);
}

void CtExport2Pdf::_nodes_all_export_print_iter(CtTreeIter tree_iter,
                                                const CtExportOptions& options,
                                                std::vector<CtPangoObjectPtr>& tree_pango_slots)
{
    Glib::RefPtr<Gtk::TextBuffer> pTextBuffer = tree_iter.get_node_text_buffer();
    if (not pTextBuffer) {
        throw std::runtime_error(str::format(_("Failed to retrieve the content of the node '%s'"), tree_iter.get_node_name().raw()));
    }
    std::vector<CtPangoObjectPtr> node_pango_slots;
    if (tree_iter.get_node_is_text()) {
        CtExport2Pango{_pCtMainWin}.pango_get_from_treestore_node(tree_iter, -1, -1, node_pango_slots);
    }
    else {
        Glib::ustring text = CtExport2Pango{_pCtMainWin}.pango_get_from_code_buffer(
            pTextBuffer, -1, -1, tree_iter.get_node_syntax_highlighting());
        node_pango_slots.push_back(std::make_shared<CtPangoText>(text, tree_iter.get_node_syntax_highlighting(), 0/*indent*/, PANGO_DIRECTION_LTR));
    }

    if (options.include_node_name) {
        node_pango_slots.emplace(node_pango_slots.begin(), _generate_pango_node_name(tree_iter));
    }
    if (tree_pango_slots.empty()) {
        tree_pango_slots = node_pango_slots;
    }
    else {
        if (options.new_node_page)
            tree_pango_slots.push_back(std::make_shared<CtPangoNewPage>());
        else
            tree_pango_slots.push_back(std::make_shared<CtPangoText>(str::repeat(CtConst::CHAR_NEWLINE, 3), tree_iter.get_node_syntax_highlighting(), 0/*indent*/, PANGO_DIRECTION_NEUTRAL));
        vec::vector_extend(tree_pango_slots, node_pango_slots);
    }

    for (auto child_iter = tree_iter->children().begin(); child_iter != tree_iter->children().end(); ++child_iter) {
        _nodes_all_export_print_iter(_pCtMainWin->get_tree_store().to_ct_tree_iter(child_iter), options, tree_pango_slots);
    }
}

CtPangoObjectPtr CtExport2Pdf::_generate_pango_node_name(CtTreeIter tree_iter)
{
    const auto node_name = tree_iter.get_node_name();
    Glib::ustring text = "<b><i><span size=\"xx-large\">" + str::xml_escape(node_name) + "</span></i></b>" +
                         CtConst::CHAR_NEWLINE + CtConst::CHAR_NEWLINE;
    const PangoDirection pango_dir = CtStrUtil::gtk_pango_find_base_dir(node_name.c_str(), -1);
    auto slot = std::make_shared<CtPangoDest>(
        text,
        tree_iter.get_node_syntax_highlighting(),
        0,
        "name='" + generate_tag(tree_iter.get_node_id(), "") + "'",
        pango_dir);
    return slot;
}

CtPrint::CtPrint(CtMainWin* pCtMainWin)
 : _pCtMainWin{pCtMainWin}
 , _pCtConfig{pCtMainWin->get_ct_config()}
{
    _pPrintSettings = Gtk::PrintSettings::create();
    _pPageSetup = Gtk::PageSetup::create();
    const fs::path printPageSetupFilepath = fs::get_cherrytree_print_page_setup_cfg_filepath();
    bool pageSetupLoadFromFile{false};
    if (fs::is_regular_file(printPageSetupFilepath)) {
#if GTKMM_MAJOR_VERSION >= 4
        auto keyFile = Glib::KeyFile::create();
        keyFile->load_from_file(printPageSetupFilepath.string());
        try {
            _pPageSetup->load_from_key_file(keyFile);
            pageSetupLoadFromFile = true;
        }
        catch (Glib::KeyFileError& kferror) {}
        try {
            _pPrintSettings->load_from_key_file(keyFile);
        }
        catch (Glib::KeyFileError& kferror) {}
#else
        Glib::KeyFile keyFile;
        keyFile.load_from_file(printPageSetupFilepath.string());
        try {
            _pPageSetup->load_from_key_file(keyFile);
            pageSetupLoadFromFile = true;
        }
        catch (Glib::KeyFileError& kferror) {}
        try {
            _pPrintSettings->load_from_key_file(keyFile);
        }
        catch (Glib::KeyFileError& kferror) {}
#endif
    }
    if (not pageSetupLoadFromFile) {
        _pPageSetup->set_paper_size(Gtk::PaperSize("iso_a4"));
    }
}

void CtPrint::run_page_setup_dialog(Gtk::Window* pWin)
{
    _pPageSetup = Gtk::run_page_setup_dialog(*pWin, _pPageSetup, _pPrintSettings);
#if GTKMM_MAJOR_VERSION >= 4
    auto keyFile = Glib::KeyFile::create();
    _pPageSetup->save_to_key_file(keyFile);
    _pPrintSettings->save_to_key_file(keyFile);
    keyFile->save_to_file(fs::get_cherrytree_print_page_setup_cfg_filepath().string());
#else
    Glib::KeyFile keyFile;
    _pPageSetup->save_to_key_file(keyFile);
    _pPrintSettings->save_to_key_file(keyFile);
    keyFile.save_to_file(fs::get_cherrytree_print_page_setup_cfg_filepath().string());
#endif
}

// Start the Print Operations for Text
void CtPrint::print_text(const fs::path& pdf_filepath, const std::vector<CtPangoObjectPtr>& slots)
{
    CtPrintData print_data;
    print_data.slots = slots;

#if GTKMM_MAJOR_VERSION >= 4
    // GTK4: use lambdas directly to avoid incomplete sigc::slot type issues
    auto f_begin_print_text = [this, &print_data](const Glib::RefPtr<Gtk::PrintContext>& context) {
        _on_begin_print_text(context, &print_data);
    };
    auto f_draw_page_text = [this, &print_data](const Glib::RefPtr<Gtk::PrintContext>& context, int page_nr) {
        _on_draw_page_text(context, page_nr, &print_data);
    };
#else
    sigc::slot<void, const Glib::RefPtr<Gtk::PrintContext>&, CtPrintData*> f_begin_print_text = sigc::mem_fun(*this, &CtPrint::_on_begin_print_text);
    sigc::slot<void,const Glib::RefPtr<Gtk::PrintContext>&, int, CtPrintData*> f_draw_page_text = sigc::mem_fun(*this, &CtPrint::_on_draw_page_text);
#endif

    print_data.operation = Gtk::PrintOperation::create();
    print_data.operation->set_show_progress(true);
    print_data.operation->set_default_page_setup(_pPageSetup);
    print_data.operation->set_print_settings(_pPrintSettings);
#if GTKMM_MAJOR_VERSION >= 4
    print_data.operation->signal_begin_print().connect(f_begin_print_text);
    print_data.operation->signal_draw_page().connect(f_draw_page_text);
#else
    print_data.operation->signal_begin_print().connect(sigc::bind(f_begin_print_text, &print_data));
    print_data.operation->signal_draw_page().connect(sigc::bind(f_draw_page_text, &print_data));
#endif
    print_data.operation->set_export_filename(pdf_filepath.string());
    try {
#if GTKMM_MAJOR_VERSION >= 4
        auto res = print_data.operation->run(!pdf_filepath.empty() ? Gtk::PrintOperation::Action::EXPORT : Gtk::PrintOperation::Action::PRINT_DIALOG);
        if (res == Gtk::PrintOperation::Result::ERROR)
            CtDialogs::error_dialog("Error printing file: (bad res)", *_pCtMainWin);
        else if (res == Gtk::PrintOperation::Result::APPLY)
#else
        auto res = print_data.operation->run(!pdf_filepath.empty() ? Gtk::PRINT_OPERATION_ACTION_EXPORT : Gtk::PRINT_OPERATION_ACTION_PRINT_DIALOG);
        if (res == Gtk::PRINT_OPERATION_RESULT_ERROR)
            CtDialogs::error_dialog("Error printing file: (bad res)", *_pCtMainWin);
        else if (res == Gtk::PRINT_OPERATION_RESULT_APPLY)
#endif
            _pPrintSettings = print_data.operation->get_print_settings();
    }
    catch (Glib::Error& ex) {
        CtDialogs::error_dialog("Error printing file:\n" + str::xml_escape(ex.what()) + " (exception caught)", *_pCtMainWin);
    }
    if (!print_data.warning.empty())
        _pCtMainWin->get_status_bar().update_status(print_data.warning);
}

// Here we Compute the Lines Positions, the Number of Pages Needed and the Page Breaks
void CtPrint::_on_begin_print_text(const Glib::RefPtr<Gtk::PrintContext>& context, CtPrintData* print_data)
{
    auto get_font_with_fallback_ = [](Pango::FontDescription font, const std::string& fallbackFont) {
#ifdef _WIN32
        // explicit fallback is needed on Win32, linux works OK without it
        font.set_family(font.get_family() + "," + fallbackFont);
#else
        (void)fallbackFont; // to silence the warning
#endif
        return font;
    };

    print_data->context = context;
    _rich_font = get_font_with_fallback_(Pango::FontDescription(_pCtConfig->rtFont), _pCtConfig->fallbackFontFamily);
    _plain_font = get_font_with_fallback_(Pango::FontDescription(_pCtConfig->ptFont), _pCtConfig->fallbackFontFamily);
    _code_font = get_font_with_fallback_(Pango::FontDescription(_pCtConfig->codeFont), "monospace");
    _text_window_width = _pCtMainWin->get_text_view().mm().get_allocation().get_width();
    _table_line_thickness = 6; // will be scaled by _doc_scale where it's used
    // standard - 72, but MS print to pdf - 600
    // it helps to fix window pixels, otherwise images, etc will be too small
    _page_dpi_scale = context->get_dpi_x() / 72.0;
    _page_width = context->get_width();
    _page_height = context->get_height() * 1.02; // tolerance at bottom of the page

    // Pre-scan slots to find the most restrictive table fit_scale, then use it as
    // a uniform document-wide content scale so text, images, codeboxes, and table
    // cells all share the same visual proportions. We do a natural-width pango
    // pass per table so wrap-off cells (which expand their column to the text's
    // natural width in the editor) are accounted for here, not just stored
    // col_widths.
    {
        const double TABLE_SIDE_MARGIN_PRESCAN = 36.0; // matches _process_pango_table
        _doc_scale = 1.0;
        for (auto slot : print_data->slots) {
            auto pango_widget = std::dynamic_pointer_cast<CtPangoWidget>(slot);
            if (!pango_widget) continue;
            auto* pTable = dynamic_cast<const CtTableCommon*>(pango_widget->widget);
            if (!pTable) continue;
            // Lay out all cells at fit_scale=1 (untouched font) to discover the
            // widest natural line of any wrap-off cell per column.
            const auto natural_layouts = _table_get_layouts(pTable, 1, -1, context, 1.0);
            const CtTableStyle& tStyle = pTable->getTableStyle();
            auto isWrapOff = [&](size_t r, size_t c) -> bool {
                const auto it = tStyle.cellWrap.find({r, c});
                if (it != tStyle.cellWrap.end()) return !it->second;
                if (tStyle.tableWrapDefaultSet) return !tStyle.tableWrapDefault;
                return false;
            };
            const auto* pRichTable = dynamic_cast<const CtTableRich*>(pTable);
            double natural_w = 0.0;
            for (size_t c = 0; c < pTable->get_num_columns(); ++c) {
                double w_pts = pTable->get_col_width(c) * _page_dpi_scale;
                for (size_t lr = 0; lr < natural_layouts.size(); ++lr) {
                    size_t srcR = lr;
                    if (pRichTable && lr > 0) srcR = lr; // first_row was 1 in our call
                    if (!isWrapOff(srcR, c)) continue;
                    if (c >= natural_layouts[lr].size()) continue;
                    auto& cl = natural_layouts[lr][c];
                    double mx = 0;
                    for (int li = 0; li < cl->get_line_count(); ++li) {
                        const double lw = _get_width_height_from_layout_line(cl->get_line(li)).width;
                        if (lw > mx) mx = lw;
                    }
                    if (mx > w_pts) w_pts = mx;
                }
                natural_w += w_pts;
            }
            const double avail = std::max(0.0, _page_width - pango_widget->indent - 2 * TABLE_SIDE_MARGIN_PRESCAN);
            if (natural_w > avail and natural_w > 0) {
                const double s = avail / natural_w;
                if (s < _doc_scale) _doc_scale = s;
            }
        }
        // We don't pre-scale font descriptions — that would only shrink the layout
        // base font, and explicit font_size attrs in the markup (H1, H2, monospace,
        // etc.) would keep their full size, throwing off the H/body ratio. Instead
        // every text layout gets a pango_attr_scale applied uniformly at the end of
        // its construction; see the helper below.
    }
    _table_text_row_height = _rich_font.get_size()/Pango::SCALE;
    _layout_newline_height = [&](){
        Glib::RefPtr<Pango::Layout> layout_newline = context->create_pango_layout();
        layout_newline->set_font_description(_rich_font);
        layout_newline->set_width(int(_page_width * Pango::SCALE));
        layout_newline->set_markup(CtConst::CHAR_NEWLINE);
        return _get_width_height_from_layout_line(layout_newline->get_line(0)).height;
    }();

    bool any_image_resized{false};
    for (auto slot : print_data->slots) {
        if (dynamic_cast<CtPangoNewPage*>(slot.get())) {
            print_data->pages.new_page();
        }
        else if (auto pango_text = dynamic_cast<CtPangoText*>(slot.get())) {
            _process_pango_text(print_data, pango_text);
        }
        else if (auto pango_widget = dynamic_cast<CtPangoWidget*>(slot.get())) {
            if (auto image = dynamic_cast<const CtImage*>(pango_widget->widget)) {
                _process_pango_image(print_data, image, pango_widget, any_image_resized);
            }
            else if (auto codebox = dynamic_cast<const CtCodebox*>(pango_widget->widget)) {
                _process_pango_codebox(print_data, codebox, pango_widget);
            }
            else if (auto table = dynamic_cast<const CtTableCommon*>(pango_widget->widget)) {
                _process_pango_table(print_data, table, pango_widget);
            }
        }
    }

    print_data->operation->set_n_pages(print_data->pages.size());
    if (any_image_resized) {
        print_data->warning = Glib::ustring(_("Warning: One or More Images Were Reduced to Enter the Page!")) + " ("
                                       + std::to_string(static_cast<int>(_page_width))+ "x" + std::to_string(static_cast<int>(_page_height)) + ")";
    }
}

bool CtPrint::_cairo_tag_can_apply(const Glib::ustring& tag_name, const Glib::ustring& tag_attr, const CtPrintData* print_data)
{
    if (CAIRO_TAG_DEST == tag_name or not str::startswith(tag_attr, "dest=")) {
        return true;
    }
    Glib::ustring tag_attr_dest = tag_attr.substr(5);
    for (const Glib::ustring& curr_name : print_data->cairo_names) {
        if (curr_name == tag_attr_dest) {
            return true;
        }
    }
    spdlog::debug("{} dropped", tag_attr.raw());
    return false;
}

void CtPrint::_on_draw_page_text(const Glib::RefPtr<Gtk::PrintContext>& context, int page_nr, CtPrintData* print_data)
{
    auto operation = print_data->operation;
    auto cairo_context = context->get_cairo_context();

    // draw page number
    cairo_context->set_source_rgb(0.5, 0.5, 0.5);
    Glib::ustring page_num_str = std::to_string(page_nr+1) + "/" + std::to_string(operation->property_n_pages());
    Glib::RefPtr<Pango::Layout> layout = context->create_pango_layout();
    layout->set_font_description(_rich_font);
    layout->set_markup(page_num_str);
    auto layout_line = layout->get_line(0);
    auto size = _get_width_height_from_layout_line(layout_line);
    cairo_context->move_to(_page_width/2. - size.width/2, _page_height+17);
    layout_line->show_in_cairo_context(cairo_context);

    //cairo_context->set_source_rgba(0.3, 0, 0, 0.3);
    //cairo_context->rectangle(0, 0, _page_width, _page_height);
    //cairo_context->stroke();

    auto& page = print_data->pages.get_page(page_nr);
    for (auto& line : page.lines) {
        for (CtPageElementPtr element : line.elements) {
            if (auto page_text = dynamic_cast<CtPageText*>(element.get())) {
                cairo_context->set_source_rgb(0, 0, 0);
                cairo_context->move_to(page_text->x, line.y);
                page_text->layout_line->show_in_cairo_context(cairo_context);

                //auto size = _get_width_height_from_layout_line(page_text->layout_line);
                //cairo_context->set_source_rgba(0.3, 0, 0, 0.3);
                //cairo_context->rectangle(page_text->x, page_text->y - size.height, size.width, size.height);
                //cairo_context->stroke();
            }
            else if (auto page_tag = dynamic_cast<CtPageTag*>(element.get())) {
                cairo_context->set_source_rgb(0, 0, 0);

                const bool can_cairo_tag = _cairo_tag_can_apply(page_tag->tag_name, page_tag->tag_attr, print_data);

                if (can_cairo_tag) cairo_tag_begin(cairo_context->cobj(), page_tag->tag_name.c_str(), page_tag->tag_attr.c_str());
                cairo_context->move_to(page_tag->x, line.y);
                page_tag->layout_line->show_in_cairo_context(cairo_context);
                if (can_cairo_tag) cairo_tag_end(cairo_context->cobj(), page_tag->tag_name.c_str());
            }
            else if (auto page_image = dynamic_cast<const CtPageImage*>(element.get())) {
                auto scale = page_image->scale; // it also contains _page_dpi_scale
                Glib::RefPtr<Gdk::Pixbuf> pPixbuf;
                if (auto pLatexImage = dynamic_cast<const CtImageLatex*>(page_image->image)) {
                    pPixbuf = pLatexImage->get_image_for_print();
                    scale /= CtImageLatex::PrintZoom;
                }
                else {
                    pPixbuf = page_image->image->get_pixbuf();;
                }
                double pixbuf_height = pPixbuf->get_height() * scale;
                cairo_context->save();
                cairo_context->scale(scale, scale);
                Gdk::Cairo::set_source_pixbuf(cairo_context, pPixbuf, page_image->x / scale, (line.y - pixbuf_height) / scale);
                cairo_context->paint();
                cairo_context->restore();
            }
            else if (auto page_codebox = dynamic_cast<const CtPageCodebox*>(element.get())) {
                double codebox_height = _get_height_from_layout(page_codebox->layout);
                // Use the explicit box_width if set so split codeboxes have a
                // consistent width across pages (otherwise a single short line
                // would shrink the rendered frame to that line's pixel width).
                double codebox_width = page_codebox->box_width > 0
                    ? page_codebox->box_width
                    : _get_width_from_layout(page_codebox->layout);
                _draw_codebox_box(cairo_context, page_codebox->x, line.y - codebox_height, codebox_width, codebox_height);
                _draw_codebox_code(cairo_context, page_codebox->layout, page_codebox->x, line.y - codebox_height);
            }
            else if (auto page_table = dynamic_cast<const CtPageTable*>(element.get())) {
                std::vector<double> rows_h, cols_w;
                _table_get_grid(page_table->layouts, page_table->colWidths, rows_h, cols_w, page_table->pTable, page_table->first_row, page_table->fit_scale);
                double table_width = _table_get_width_height(cols_w);
                double table_height = _table_get_width_height(rows_h);
                _draw_table_grid(cairo_context, rows_h, cols_w, page_table->x, line.y - table_height, table_width, table_height, page_table);
                _draw_table_text(cairo_context, rows_h, cols_w, page_table->layouts, page_table->x, line.y - table_height, page_table);
            }
        }
    }
}

void CtPrint::_process_pango_text(CtPrintData* print_data, CtPangoText* text_slot)
{
    auto context = print_data->context;
    CtPrintPages& pages = print_data->pages;
    Pango::FontDescription* font = [&]() {
        if (text_slot->synt_highl == CtConst::RICH_TEXT_ID) return &_rich_font;
        if (text_slot->synt_highl == CtConst::PLAIN_TEXT_ID) return &_plain_font;
        return &_code_font;
    }();

    Glib::ustring tag_name, tag_attr;
    if (auto pango_link = dynamic_cast<CtPangoLink*>(text_slot)) {
        tag_name = CAIRO_TAG_LINK;
        tag_attr = pango_link->link;
    }
    else if (auto pango_dest = dynamic_cast<CtPangoDest*>(text_slot)) {
        tag_name = CAIRO_TAG_DEST;
        tag_attr = pango_dest->dest;
        print_data->cairo_names.push_back(tag_attr.substr(5)); // name='...'
    }

    if (not pages.last_line().evaluated_pango_dir) {
        pages.last_line().evaluated_pango_dir = true;
        pages.last_line().pango_dir = text_slot->pango_dir;
    }
    else if (text_slot->pango_dir != pages.last_line().pango_dir and
             PANGO_DIRECTION_NEUTRAL != text_slot->pango_dir and
             PANGO_DIRECTION_NEUTRAL != pages.last_line().pango_dir and
             -1 != pages.last_line().cur_x)
    {
        if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
            pages.last_line().changed_rtl_in_line_prev_x = pages.last_line().cur_x;
            pages.last_line().cur_x = -1;
        }
        else {
            //spdlog::debug("second change of rtl in same line, slot_text={}, was {}", text_slot->text, pages.last_line().changed_rtl_in_line_prev_x);
            pages.new_line();
            _process_pango_text(print_data, text_slot);
            return;
        }
    }

    Glib::RefPtr<Pango::Layout> layout = context->create_pango_layout();
    layout->set_font_description(*font);
    const int max_layout_line_width = _page_width - text_slot->indent;
    layout->set_width(max_layout_line_width * Pango::SCALE);
#if GTKMM_MAJOR_VERSION >= 4
    layout->set_wrap(Pango::WrapMode::WORD_CHAR);
#else
    layout->set_wrap(Pango::WRAP_WORD_CHAR);
#endif
    // the next line fixes the link issue, allowing to start paragraphs from where a link ends
    // don't apply paragraph indent because set_indent will work only for the first line
    // also avoid `\n` because new lines also got indent
    if (PANGO_DIRECTION_RTL != text_slot->pango_dir and Glib::ustring(CtConst::CHAR_NEWLINE) != text_slot->text and -1 != pages.last_line().cur_x) {
        layout->set_indent(int(pages.last_line().cur_x * Pango::SCALE));
    }
    layout->set_markup(text_slot->text);
    _apply_doc_scale_to_layout(layout);
    //spdlog::debug("{}", text_slot->text.c_str());

    int layout_count = layout->get_line_count();
    for (int i = 0; i < layout_count; ++i) {
        auto layout_line = layout->get_line(i);
        auto size = _get_width_height_from_layout_line(layout_line);
        if (size.width > max_layout_line_width) {
            size.width = max_layout_line_width;
        }

        if (not pages.last_line().test_element_height(size.height, _page_height)) {
            pages.line_on_new_page();
        }

        if (-1 == pages.last_line().cur_x) {
            // initialise x for a new line (or after change of RTL in line)
            if (PANGO_DIRECTION_RTL == text_slot->pango_dir) {
                pages.last_line().cur_x = max_layout_line_width;
            }
            else {
                pages.last_line().cur_x = text_slot->indent;
            }
        }
        if (text_slot->text != Glib::ustring(CtConst::CHAR_NEWLINE)) {
            // situation when a bit of space is left but pango cannot wrap the first word
            // make it on a new line
            bool need_new_line{false};
            if (PANGO_DIRECTION_RTL == text_slot->pango_dir) {
                if ( (pages.last_line().cur_x - size.width) <
                     (-1 == pages.last_line().changed_rtl_in_line_prev_x ? 0 : pages.last_line().changed_rtl_in_line_prev_x) )
                {
                    need_new_line = true;
                }
            }
            else {
                if ( (pages.last_line().cur_x + size.width) >
                     (-1 == pages.last_line().changed_rtl_in_line_prev_x ? _page_width : pages.last_line().changed_rtl_in_line_prev_x) )
                {
                    need_new_line = true;
                }
            }
            if (need_new_line) {
                pages.new_line();
                _process_pango_text(print_data, text_slot);
                return;
            }
        }

        if (PANGO_DIRECTION_RTL == text_slot->pango_dir) {
            // decrease x before if RTL
            pages.last_line().cur_x -= size.width;
        }
        pages.last_line().set_max_height(size.height);
        if (tag_name.empty()) {
            pages.last_line().elements.push_back(std::make_shared<CtPageText>(pages.last_line().cur_x, layout_line));
        }
        else {
            pages.last_line().elements.push_back(std::make_shared<CtPageTag>(pages.last_line().cur_x, layout_line, tag_name, tag_attr));
        }
        if (i < (layout_count - 1)) { // the paragragh was wrapped, so it's multiline
            pages.new_line();
        }
        else if (PANGO_DIRECTION_RTL != text_slot->pango_dir) {
            // increase x after if not RTL
            pages.last_line().cur_x += size.width;
        }
    }
}

void CtPrint::_process_pango_image(CtPrintData* print_data, const CtImage* image, const CtPangoWidget* pango_widget, bool& any_image_resized)
{
    auto context = print_data->context;
    CtPrintPages& pages = print_data->pages;
    auto pixbuf = image->get_pixbuf();

    for (int i = 0; i < 2; ++i) {
        // first loop we try and fit the image in line with existing text
        // second loop we move to a new line for maximum width

        if (not pages.last_line().evaluated_pango_dir) {
            pages.last_line().evaluated_pango_dir = true;
            pages.last_line().pango_dir = pango_widget->pango_dir;
        }

        if (-1 == pages.last_line().cur_x) {
            // initialise x for a new line
            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                pages.last_line().cur_x = _page_width - pango_widget->indent;
            }
            else {
                pages.last_line().cur_x = pango_widget->indent;
            }
        }

        int available_width{0};
        if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
            if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
                available_width = pages.last_line().cur_x;
            }
            else {
                available_width = pages.last_line().cur_x - pages.last_line().changed_rtl_in_line_prev_x;
            }
        }
        else {
            if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
                available_width = _page_width - pages.last_line().cur_x;
            }
            else {
                available_width = pages.last_line().changed_rtl_in_line_prev_x - pages.last_line().cur_x;
            }
        }
        // calculate image
        const double scale_w = available_width / (pixbuf->get_width() * _page_dpi_scale);
        if (0 == i and scale_w < 1.0 and available_width < (_page_width - pango_widget->indent)) {
            pages.new_line();
            continue; // restart loop from a new line
        }
        double scale_h = (_page_height - _layout_newline_height - (CtConst::WHITE_SPACE_BETW_PIXB_AND_TEXT * _page_dpi_scale)) / (pixbuf->get_height() * _page_dpi_scale);
        double scale = std::min(scale_w, scale_h);
        if (scale > 1.0) scale = 1.0;
        if (scale < 1.0) any_image_resized = true;

        scale *= _page_dpi_scale; // need to compensate high dpi
        // Apply the document-wide scale so non-table images shrink in step with
        // text, codeboxes, and table content.
        scale *= _doc_scale;

        double pixbuf_width = pixbuf->get_width() * scale;
        double pixbuf_height = pixbuf->get_height() * scale + (CtConst::WHITE_SPACE_BETW_PIXB_AND_TEXT * _page_dpi_scale);

        // calculate label if it exists
        Cairo::Rectangle label_size{0,0,0,0};
        Glib::RefPtr<Pango::Layout> label_layout = context->create_pango_layout();
        label_layout->set_font_description(_plain_font);
        if (auto emb_file = dynamic_cast<const CtImageEmbFile*>(image)) {
            label_layout->set_markup("<b><small>"+str::xml_escape(emb_file->get_file_name().string())+"</small></b>");
            _apply_doc_scale_to_layout(label_layout);
            label_size = _get_width_height_from_layout_line(label_layout->get_line(0));
        }

        if (not pages.last_line().test_element_height(pixbuf_height + label_size.height, _page_height)) {
            pages.line_on_new_page();
        }
        if (-1 == pages.last_line().cur_x) {
            // initialise x for a new line
            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                pages.last_line().cur_x = _page_width - pango_widget->indent;
            }
            else {
                pages.last_line().cur_x = pango_widget->indent;
            }
        }

        if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
            // decrease x before if RTL
            pages.last_line().cur_x -= std::max(pixbuf_width, label_size.width);
        }

        // insert label line
        if (label_size.height != 0) {
            int prev_x = pages.last_line().cur_x;
            int prev_y = pages.last_line().y;
            pages.last_line().set_max_height(label_size.height);
            pages.last_line().elements.push_back(std::make_shared<CtPageText>(pages.last_line().cur_x, label_layout->get_line(0)));
            pages.new_line();
            pages.last_line().cur_x = prev_x;
            pages.last_line().y = prev_y - (CtConst::WHITE_SPACE_BETW_PIXB_AND_TEXT * _page_dpi_scale); // this removes additional 2px;
        }

        // insert image
        pages.last_line().set_max_height(pixbuf_height);
        pages.last_line().elements.push_back(std::make_shared<CtPageImage>(pages.last_line().cur_x, image, scale));

        if (PANGO_DIRECTION_RTL != pango_widget->pango_dir) {
            // increase x after if not RTL
            pages.last_line().cur_x += std::max(pixbuf_width, label_size.width);
        }
        break; // if we reach the end of the first loop, we don't want to start another
    }
}

void CtPrint::_process_pango_codebox(CtPrintData* print_data, const CtCodebox* codebox, const CtPangoWidget* pango_widget)
{
    auto context = print_data->context;
    CtPrintPages& pages = print_data->pages;

    Glib::ustring original_content = CtExport2Pango{_pCtMainWin}.pango_get_from_code_buffer(
        codebox->get_buffer(), -1, -1, codebox->get_syntax_highlighting());

    for (int i = 0; i < 1000/*just a big number without meaning*/; ++i) {
        // first loop we try and fit the codebox in line with existing text
        // second loop we move to a new line for maximum width

        if (not pages.last_line().evaluated_pango_dir) {
            pages.last_line().evaluated_pango_dir = true;
            pages.last_line().pango_dir = pango_widget->pango_dir;
        }

        if (-1 == pages.last_line().cur_x) {
            // initialise x for a new line
            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                pages.last_line().cur_x = _page_width - pango_widget->indent;
            }
            else {
                pages.last_line().cur_x = pango_widget->indent;
            }
        }

        int available_width{0};
        if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
            if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
                available_width = pages.last_line().cur_x;
            }
            else {
                available_width = pages.last_line().cur_x - pages.last_line().changed_rtl_in_line_prev_x;
            }
        }
        else {
            if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
                available_width = _page_width - pages.last_line().cur_x;
            }
            else {
                available_width = pages.last_line().changed_rtl_in_line_prev_x - pages.last_line().cur_x;
            }
        }

        // The codebox's intended width is a per-codebox property: don't shrink it
        // based on the current line's available width, otherwise the second piece
        // (after a page break, with full-page-width available) would be wider than
        // the first piece. Cap only at the page width minus indent, computed once
        // and reused on every iteration of the split loop.
        double codebox_width = (codebox->get_width_in_pixels() ? codebox->get_frame_width() : _text_window_width * codebox->get_frame_width()/100.0)*_page_dpi_scale;
        codebox_width *= _doc_scale;
        const double max_codebox_width = std::max(0.0, _page_width - pango_widget->indent);
        if (codebox_width > max_codebox_width) {
            codebox_width = max_codebox_width;
        }
        if (0 == i and codebox_width > available_width and available_width < (_page_width - pango_widget->indent)) {
            pages.new_line();
            continue; // restart loop from a new line
        }

        // use content if it's ok
        auto codebox_layout = _codebox_get_layout(codebox, original_content, context, codebox_width);
        double codebox_height = _get_height_from_layout(codebox_layout);
        if (pages.last_line().test_element_height(codebox_height + (BOX_OFFSET * _page_dpi_scale), _page_height)) {

            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                // decrease x before if RTL
                pages.last_line().cur_x -= _get_width_from_layout(codebox_layout);
            }

            pages.last_line().set_max_height(codebox_height + (BOX_OFFSET * _page_dpi_scale));
            pages.last_line().elements.push_back(std::make_shared<CtPageCodebox>(pages.last_line().cur_x, codebox_layout, codebox_width));

            if (PANGO_DIRECTION_RTL != pango_widget->pango_dir) {
                // increase x after if not RTL
                pages.last_line().cur_x += _get_width_from_layout(codebox_layout);
            }
            return;
        }

        // if content is too long, split it
        Glib::ustring first_split, second_split;
        _codebox_split_content(codebox, original_content, _page_height - pages.last_line().y, context, first_split, second_split, codebox_width);
        if (first_split.empty()) {
            pages.new_page(); // need a new page
        }
        else {
            auto first_split_layout = _codebox_get_layout(codebox, first_split, context, codebox_width);
            double codebox_height = _get_height_from_layout(first_split_layout);

            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                // decrease x before if RTL
                pages.last_line().cur_x -= _get_width_from_layout(codebox_layout);
            }

            pages.last_line().set_max_height(codebox_height + (BOX_OFFSET * _page_dpi_scale));
            pages.last_line().elements.push_back(std::make_shared<CtPageCodebox>(pages.last_line().cur_x, first_split_layout, codebox_width));

            // no need to increase x after if not RTL as we will move to a new page
            pages.new_page();

            // go to to check the second part
            original_content = second_split;
        }
    }
}

Glib::RefPtr<Pango::Layout> CtPrint::_codebox_get_layout(const CtCodebox* codebox,
                                                         Glib::ustring content,
                                                         Glib::RefPtr<Gtk::PrintContext> context,
                                                         const int codebox_width)
{
    Glib::RefPtr<Pango::Layout> layout = context->create_pango_layout();
    layout->set_font_description(codebox->get_syntax_highlighting() != CtConst::PLAIN_TEXT_ID ? _code_font : _plain_font);
    layout->set_width(int(codebox_width * Pango::SCALE));
#if GTKMM_MAJOR_VERSION >= 4
    layout->set_wrap(Pango::WrapMode::WORD_CHAR);
#else
    layout->set_wrap(Pango::WRAP_WORD_CHAR);
#endif
    layout->set_markup(content);
    _apply_doc_scale_to_layout(layout);
    return layout;
}

// Split Long CodeBoxes
void CtPrint::_codebox_split_content(const CtCodebox* codebox,
                                     Glib::ustring original_content,
                                     const int check_height,
                                     const Glib::RefPtr<Gtk::PrintContext>& context,
                                     Glib::ustring& first_split,
                                     Glib::ustring& second_split,
                                     const int codebox_width)
{
    std::vector<Glib::ustring> original_splitted_pango = str::split(original_content, "\n");
    // fix for not-closed span
    const size_t original_splitted_pango_size = original_splitted_pango.size();
    for (size_t i = 0u; i < original_splitted_pango_size; ++i) {
        Glib::ustring& element = original_splitted_pango[i];
        //if (0u == i) spdlog::debug("{}", element.c_str());
        Glib::ustring::size_type last_close = element.rfind("</span");
        Glib::ustring::size_type last_open = element.rfind("<span");
        if (last_close < element.size() and last_open < element.size() and last_close < last_open) {
            Glib::ustring non_closed_span = element.substr(last_open);
            Glib::ustring::size_type end_non_closed_span_idx = non_closed_span.find(">");
            if (end_non_closed_span_idx < non_closed_span.size()) {
                non_closed_span = non_closed_span.substr(0, end_non_closed_span_idx+1);
                original_splitted_pango[i] += "</span>";
                original_splitted_pango[i+1] = non_closed_span + original_splitted_pango[i+1];
            }
        }
    }

    std::vector<Glib::ustring> splitted_pango;
    while (splitted_pango.size() < original_splitted_pango_size) {
        splitted_pango.push_back(original_splitted_pango[splitted_pango.size()]);
        Glib::ustring new_content = str::join(splitted_pango, CtConst::CHAR_NEWLINE);
        Glib::RefPtr<Pango::Layout> codebox_layout = _codebox_get_layout(codebox, new_content, context, codebox_width);
        const double codebox_height = _get_height_from_layout(codebox_layout);
        if ((codebox_height + BOX_OFFSET) > check_height) {
            if (1u == splitted_pango.size()) {
                // check_height is not enough, so we need a new page
                first_split.clear();
                second_split = original_content;
                return;
            }

            splitted_pango.pop_back();
            original_splitted_pango.erase(original_splitted_pango.begin(), original_splitted_pango.begin() + splitted_pango.size());
            first_split = str::join(splitted_pango, CtConst::CHAR_NEWLINE);
            second_split = str::join(original_splitted_pango, CtConst::CHAR_NEWLINE);
            return;
        }
    }
}

void CtPrint::_process_pango_table(CtPrintData *print_data,
                                   const CtTableCommon* table,
                                   const CtPangoWidget* pango_widget)
{
    auto context = print_data->context;
    CtPrintPages& pages = print_data->pages;

    int first_row = 1;

    for (int i = 0; i < 1000/*just a big number without meaning*/; ++i) {
        // first loop we try and fit the codebox in line with existing text
        // second loop we move to a new line for maximum width

        if (not pages.last_line().evaluated_pango_dir) {
            pages.last_line().evaluated_pango_dir = true;
            pages.last_line().pango_dir = pango_widget->pango_dir;
        }

        if (-1 == pages.last_line().cur_x) {
            // initialise x for a new line
            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                pages.last_line().cur_x = _page_width - pango_widget->indent;
            }
            else {
                pages.last_line().cur_x = pango_widget->indent;
            }
        }

        int available_width{0};
        if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
            if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
                available_width = pages.last_line().cur_x;
            }
            else {
                available_width = pages.last_line().cur_x - pages.last_line().changed_rtl_in_line_prev_x;
            }
        }
        else {
            if (-1 == pages.last_line().changed_rtl_in_line_prev_x) {
                available_width = _page_width - pages.last_line().cur_x;
            }
            else {
                available_width = pages.last_line().changed_rtl_in_line_prev_x - pages.last_line().cur_x;
            }
        }

        // The document-wide scale (_doc_scale) was computed from the most
        // restrictive table at print start. Use it directly as fit_scale so all
        // tables share the same visual scale as the surrounding text and images.
        // Per-table re-derivation is no longer needed; we still emit the natural
        // pass to detect wrap-off column expansion.
        const double TABLE_SIDE_MARGIN = 36.0; // points, ~0.5 inch

        // Each column's natural width is the larger of (the stored col_width
        // pre-shrunk by _doc_scale) and (the widest natural line of any wrap-off
        // cell in that column, measured at the already-scaled font). Lay out each
        // cell once at fit_scale=1 to measure — fonts are already scaled by
        // _doc_scale at print start, so the resulting layout widths are in the
        // final coordinate space.
        const auto natural_layouts = _table_get_layouts(table, first_row, -1, context, 1.0);
        const CtTableStyle& tStyle = table->getTableStyle();
        auto isCellWrapOff = [&](size_t srcR, size_t srcC) -> bool {
            const auto it = tStyle.cellWrap.find({srcR, srcC});
            if (it != tStyle.cellWrap.end()) return !it->second;
            if (tStyle.tableWrapDefaultSet) return !tStyle.tableWrapDefault;
            return false;
        };
        double natural_table_width{0.0};
        std::vector<double> natural_col_widths_pts(table->get_num_columns(), 0.0);
        auto* pRichTable_for_nat = dynamic_cast<const CtTableRich*>(table);
        for (size_t col = 0; col < table->get_num_columns(); ++col) {
            double w_pts = table->get_col_width(col) * _page_dpi_scale * _doc_scale;
            // Walk all source rows present in natural_layouts.
            const size_t numLayoutRows = natural_layouts.size();
            for (size_t lr = 0; lr < numLayoutRows; ++lr) {
                size_t srcR = lr;
                if (pRichTable_for_nat && lr > 0) srcR = static_cast<size_t>(first_row) + (lr - 1);
                if (!isCellWrapOff(srcR, col)) continue;
                if (col >= natural_layouts[lr].size()) continue;
                auto& cell_layout = natural_layouts[lr][col];
                double max_line_w = 0;
                for (int li = 0; li < cell_layout->get_line_count(); ++li) {
                    const double lw = _get_width_height_from_layout_line(cell_layout->get_line(li)).width;
                    if (lw > max_line_w) max_line_w = lw;
                }
                if (max_line_w > w_pts) w_pts = max_line_w;
            }
            // Add a small safety slack so a 1-2pt pango-rounding difference between
            // "natural at full font × fit_scale" and "natural at scaled font" doesn't
            // cause wrap-off text to be clipped by the column edge.
            natural_col_widths_pts[col] = w_pts + (w_pts > table->get_col_width(col) * _page_dpi_scale ? 4.0 : 0.0);
            natural_table_width += natural_col_widths_pts[col];
        }
        const double max_line_width = std::max(0.0, _page_width - pango_widget->indent - 2 * TABLE_SIDE_MARGIN);
        const double fit_avail = std::max(0.0, available_width - 2 * TABLE_SIDE_MARGIN);
        // _doc_scale is already baked into both fonts and effective col widths,
        // so the per-table fit_scale here is 1.0 — no further compression.
        double fit_scale = 1.0;
        if (0 == i) {
            if (natural_table_width > fit_avail and fit_avail < max_line_width) {
                pages.new_line();
                continue; // restart loop from a new line
            }
        }

        // effective_col_widths in pixels-equivalent. natural_col_widths_pts already
        // bakes in _doc_scale (via stored × _doc_scale baseline) and wrap-off
        // expansion at the scaled font, so dividing by dpi_scale gives the final
        // per-column width that _table_get_layouts will multiply back by dpi_scale.
        CtTableColWidths effective_col_widths;
        for (size_t col = 0; col < table->get_num_columns(); ++col) {
            const double w_px = natural_col_widths_pts[col] / std::max(1e-9, _page_dpi_scale);
            effective_col_widths.push_back(int(std::ceil(w_px)));
        }

        // use table is length is ok
        std::vector<double> rows_h, cols_w;
        auto table_layouts = _table_get_layouts(table, first_row, -1, context, fit_scale, &effective_col_widths);
        _table_get_grid(table_layouts, effective_col_widths, rows_h, cols_w, table, first_row, _doc_scale);
        double table_height = _table_get_width_height(rows_h);
        // Position the table with a soft margin on the left so it isn't flush against
        // the printable-area edge. We only add this when the table starts a fresh line
        // (cur_x at indent), to avoid disrupting in-line layout with surrounding text.
        const bool atLineStart = (pages.last_line().cur_x == pango_widget->indent);
        const double table_x_offset = (PANGO_DIRECTION_RTL == pango_widget->pango_dir || !atLineStart)
                                          ? 0.0 : TABLE_SIDE_MARGIN;
        if (pages.last_line().test_element_height(table_height + (BOX_OFFSET * _page_dpi_scale), _page_height)) {

            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                // decrease x before if RTL
                pages.last_line().cur_x -= _table_get_width_height(cols_w);
            }

            pages.last_line().set_max_height(table_height + (BOX_OFFSET * _page_dpi_scale));
            pages.last_line().elements.push_back(std::make_shared<CtPageTable>(pages.last_line().cur_x + table_x_offset, table_layouts, effective_col_widths, _page_dpi_scale, table, first_row, fit_scale));

            if (PANGO_DIRECTION_RTL != pango_widget->pango_dir) {
                // increase x after if not RTL
                pages.last_line().cur_x += _table_get_width_height(cols_w) + table_x_offset;
            }
            return;
        }

        // if table is too long, split it
        int split_row = _table_split_content(table, first_row, _page_height - pages.last_line().y - (BOX_OFFSET * _page_dpi_scale), context, fit_scale);
        if (split_row == -1) {
            pages.new_page(); // need a new page
        }
        else {
            auto split_layouts = _table_get_layouts(table, first_row, split_row, context, fit_scale, &effective_col_widths);
            _table_get_grid(split_layouts, effective_col_widths, rows_h, cols_w, table, first_row, _doc_scale);
            double table_height = _table_get_width_height(rows_h);

            if (PANGO_DIRECTION_RTL == pango_widget->pango_dir) {
                // decrease x before if RTL
                pages.last_line().cur_x -= _table_get_width_height(cols_w);
            }

            pages.last_line().set_max_height(table_height + (BOX_OFFSET * _page_dpi_scale));
            pages.last_line().elements.push_back(std::make_shared<CtPageTable>(pages.last_line().cur_x + table_x_offset, split_layouts, effective_col_widths, _page_dpi_scale, table, first_row, fit_scale));

            // no need to increase x after if not RTL as we will move to a new page
            pages.new_page();

            // go to to check the second part
            first_row = split_row + 1;
        }
    }
}

CtPageTable::TableLayouts CtPrint::_table_get_layouts(const CtTableCommon* table,
                                                      const int first_row,
                                                      const int last_row,
                                                      const Glib::RefPtr<Gtk::PrintContext>& context,
                                                      const double fit_scale,
                                                      const CtTableColWidths* effective_col_widths)
{
    auto colWidthOf = [&](size_t c) -> int {
        if (effective_col_widths && c < effective_col_widths->size()) {
            return effective_col_widths->at(c);
        }
        return table->get_col_width(c);
    };
    auto* pRichTable = dynamic_cast<const CtTableRich*>(table);
    std::vector<std::vector<Glib::ustring>> rows;
    if (not pRichTable) {
        table->write_strings_matrix(rows);
    }
    const size_t numRows = pRichTable ? pRichTable->get_num_rows() : rows.size();
    const size_t numCols = pRichTable ? pRichTable->get_num_columns()
                                      : (rows.empty() ? 0u : rows.front().size());

    CtExport2Pango pango_helper{_pCtMainWin};

    const CtTableStyle& style = table->getTableStyle();
    auto resolveCellHAlign = [&](size_t r, size_t c) -> std::string {
        const auto it = style.cellHAlign.find({r, c});
        if (it != style.cellHAlign.end()) return it->second;
        return style.tableHAlignDefault;
    };
    auto resolveCellWrap = [&](size_t r, size_t c) -> bool {
        const auto it = style.cellWrap.find({r, c});
        if (it != style.cellWrap.end()) return it->second;
        if (style.tableWrapDefaultSet) return style.tableWrapDefault;
        return true;
    };

    CtPageTable::TableLayouts table_layouts;
    for (size_t r = 0u; r < numRows; ++r) {
        if (first_row != -1 && r > 0 && (int)r < first_row) continue; // skip row out of range except header
        if (last_row != -1 && (int)r > last_row) break;

        std::vector<Glib::RefPtr<Pango::Layout>> layouts;
        for (size_t c = 0u; c < numCols; ++c) {
            Glib::ustring text;
            if (pRichTable) {
                CtRichCell* pCell = pRichTable->getRichCell(r, c);
                if (pCell) {
                    auto rBuffer = pCell->get_buffer();
                    if (rBuffer) {
                        text = pango_helper.pango_get_from_rich_buffer(rBuffer);
                    }
                }
                if (r == 0) text = "<b>" + text + "</b>";
            }
            else {
                text = str::xml_escape(rows.at(r).at(c));
                if (r == 0) text = "<b>" + text + "</b>";
            }
            Glib::RefPtr<Pango::Layout> cell_layout = context->create_pango_layout();
            // _rich_font is pre-scaled at print start by the document-wide scale,
            // so we don't need to scale per-cell anymore.
            cell_layout->set_font_description(_rich_font);
            const bool wrap_on = resolveCellWrap(r, c);
            if (wrap_on) {
                cell_layout->set_width(int((colWidthOf(c) * _page_dpi_scale * fit_scale) * Pango::SCALE));
#if GTKMM_MAJOR_VERSION >= 4
                cell_layout->set_wrap(Pango::WrapMode::WORD_CHAR);
#else
                cell_layout->set_wrap(Pango::WRAP_WORD_CHAR);
#endif
            }
            else {
                // Wrap off: don't constrain layout width so the text can extend naturally.
                cell_layout->set_width(-1);
            }
            // Horizontal alignment within the cell. Pango defaults to LEFT which matches
            // the editor for rich tables; only override when a non-default halign is set.
            const std::string halign = resolveCellHAlign(r, c);
            if (halign == "center") {
#if GTKMM_MAJOR_VERSION >= 4
                cell_layout->set_alignment(Pango::Alignment::CENTER);
#else
                cell_layout->set_alignment(Pango::ALIGN_CENTER);
#endif
            }
            else if (halign == "right") {
#if GTKMM_MAJOR_VERSION >= 4
                cell_layout->set_alignment(Pango::Alignment::RIGHT);
#else
                cell_layout->set_alignment(Pango::ALIGN_RIGHT);
#endif
            }

            // For rich cells with embedded images, inject Pango shape attributes at
            // each U+FFFC position so the layout reserves inline space for them —
            // image + text flow on the same line, just like the editor.
            std::vector<const CtImage*> ordered_images;
            if (pRichTable) {
                CtRichCell* pCell = pRichTable->getRichCell(r, c);
                if (pCell) {
                    auto rBuffer = pCell->get_buffer();
                    if (rBuffer) {
                        for (Gtk::TextIter it = rBuffer->begin(); ; ) {
                            Glib::RefPtr<Gtk::TextChildAnchor> rA = it.get_child_anchor();
                            if (rA) {
                                for (CtAnchoredWidget* pW : pCell->getEmbeddedWidgets()) {
                                    if (pW->getTextChildAnchor() == rA) {
                                        if (auto* pImg = dynamic_cast<CtImage*>(pW)) {
                                            ordered_images.push_back(pImg);
                                        }
                                        break;
                                    }
                                }
                            }
                            if (not it.forward_char()) break;
                        }
                    }
                }
            }

            bool layout_set = false;
            if (not ordered_images.empty()) {
                GError* err = nullptr;
                PangoAttrList* attrs = nullptr;
                char* plain_text = nullptr;
                if (pango_parse_markup(text.c_str(), -1, 0, &attrs, &plain_text, NULL, &err)) {
                    if (!attrs) attrs = pango_attr_list_new();
                    // Find U+FFFC byte offsets in the parsed plain text.
                    std::vector<int> ufffc_offsets;
                    for (size_t bi = 0; plain_text[bi] != '\0'; ) {
                        if ((unsigned char)plain_text[bi] == 0xEF &&
                            (unsigned char)plain_text[bi+1] == 0xBF &&
                            (unsigned char)plain_text[bi+2] == 0xBC) {
                            ufffc_offsets.push_back(static_cast<int>(bi));
                            bi += 3;
                        } else {
                            ++bi;
                        }
                    }
                    const size_t common_n = std::min(ufffc_offsets.size(), ordered_images.size());
                    for (size_t k = 0; k < common_n; ++k) {
                        const CtImage* pImg = ordered_images[k];
                        auto pixbuf = pImg->get_pixbuf();
                        if (!pixbuf) continue;
                        // Image dimensions in points, scaled by _doc_scale, but
                        // additionally capped to fit the cell's inner width/height
                        // so it doesn't overflow into the borders. Inner box =
                        // col_w − 2·cell_pad and (row_h − 2·cell_pad) when the row
                        // has a fixed height in the table style.
                        const double cell_pad_pts = 4.0 * _doc_scale;
                        const double col_w_pts = std::max(0.0,
                            colWidthOf(c) * _page_dpi_scale * fit_scale);
                        const double max_img_w = std::max(1.0,
                            col_w_pts - 2 * cell_pad_pts);
                        // Determine row inner height from style (if a fixed row
                        // height is set) so vertical-overflowing images get capped.
                        double max_img_h = 0; // 0 means no height cap
                        {
                            int rowFixed = style.rowMinHeightDefault;
                            const auto rh_it = style.rowMinHeights.find(r);
                            if (rh_it != style.rowMinHeights.end()) rowFixed = rh_it->second;
                            if (rowFixed > 0) {
                                max_img_h = std::max(1.0,
                                    rowFixed * _page_dpi_scale * _doc_scale - 2 * cell_pad_pts);
                            }
                        }
                        double img_scale = _doc_scale;
                        if (pixbuf->get_width() * img_scale > max_img_w) {
                            img_scale = max_img_w / pixbuf->get_width();
                        }
                        if (max_img_h > 0 and pixbuf->get_height() * img_scale > max_img_h) {
                            img_scale = max_img_h / pixbuf->get_height();
                        }
                        const double img_w_pts = pixbuf->get_width() * img_scale;
                        const double img_h_pts = pixbuf->get_height() * img_scale;
                        const int w_pango = int(img_w_pts * PANGO_SCALE);
                        const int h_pango = int(img_h_pts * PANGO_SCALE);
                        // Shape rects: y is baseline-relative; place image so its
                        // bottom sits on the baseline (so it visually rests on the
                        // text line, like an inline image).
                        PangoRectangle ink_rect = {0, -h_pango, w_pango, h_pango};
                        PangoRectangle log_rect = ink_rect;
                        PangoAttribute* shape = pango_attr_shape_new(&ink_rect, &log_rect);
                        shape->start_index = ufffc_offsets[k];
                        shape->end_index = ufffc_offsets[k] + 3; // U+FFFC = 3 bytes UTF-8
                        pango_attr_list_insert(attrs, shape);
                    }
                    pango_layout_set_text(cell_layout->gobj(), plain_text, -1);
                    pango_layout_set_attributes(cell_layout->gobj(), attrs);
                    pango_attr_list_unref(attrs);
                    g_free(plain_text);
                    layout_set = true;
                }
                if (err) g_error_free(err);
            }
            if (not layout_set) {
                cell_layout->set_markup(text);
            }
            _apply_doc_scale_to_layout(cell_layout);
            layouts.push_back(cell_layout);
        }
        table_layouts.push_back(std::move(layouts));
    }

    return table_layouts;
}

void CtPrint::_table_get_grid(const CtPageTable::TableLayouts& table_layouts,
                              const CtTableColWidths& col_widths,
                              std::vector<double>& rows_h,
                              std::vector<double>& cols_w,
                              const CtTableCommon* table,
                              int first_row,
                              double fit_scale)
{
    rows_h = std::vector<double>(table_layouts.size(), 0);
    cols_w = std::vector<double>(table_layouts[0].size(), 0);
    auto* pRichTable = dynamic_cast<const CtTableRich*>(table);
    auto layoutRowToSrc = [pRichTable, first_row](size_t layoutRow) -> size_t {
        if (!pRichTable) return layoutRow;
        if (0 == layoutRow) return 0;
        return static_cast<size_t>(first_row) + (layoutRow - 1);
    };
    // Column widths come from the table style (already scaled by fit_scale by the caller).
    // Use them as the authoritative widths so wrap-off cells with very long single-line
    // text don't grow the table beyond the page width — text will visually overflow,
    // but the cell rectangles stay in their declared columns.
    for (size_t c = 0; c < cols_w.size(); ++c) {
        if (col_widths.at(c) > 0) cols_w[c] = col_widths.at(c);
    }
    // If the table has explicit row heights, treat them as fixed (max) rather than
    // just minimums — this matches the editor where row_height pins the cell size.
    auto resolveRowHeight = [&](size_t srcR) -> int {
        if (!table) return 0;
        const CtTableStyle& s = table->getTableStyle();
        const auto it = s.rowMinHeights.find(srcR);
        if (it != s.rowMinHeights.end()) return it->second;
        return s.rowMinHeightDefault;
    };
    // Row heights scale with the document. _doc_scale is the single source of
    // truth — fit_scale arg is unused for row sizing.
    const double row_scale = _doc_scale;
    for (size_t r = 0; r < table_layouts.size(); ++r) {
        const size_t srcR = layoutRowToSrc(r);
        const int rowFixed = resolveRowHeight(srcR);
        const double rowFixedScaled = rowFixed > 0 ? rowFixed * _page_dpi_scale * row_scale : 0;

        for (size_t c = 0; c < table_layouts[0].size(); ++c) {
            auto cell_layout = table_layouts[r][c];
            double cell_text_height = 0;
            for (int layout_line_idx = 0; layout_line_idx < cell_layout->get_line_count(); ++layout_line_idx) {
                auto line_size = _get_width_height_from_layout_line(cell_layout->get_line(layout_line_idx));
                cell_text_height += line_size.height;
                // For wrap-on cells, the layout was already constrained to col_width, so
                // max line width <= col_width; this branch only kicks in if the column
                // width hasn't been set in style (cols_w[c] still 0).
                if (cols_w[c] <= 0 && line_size.width > cols_w[c]) cols_w[c] = line_size.width;
            }

            double cell_height = cell_text_height;
            // Rich cells may contain embedded images. With a fixed row height, each
            // image is scaled to fit (col_width, available_h_for_images / num_images);
            // without one, the row grows to accommodate them.
            if (pRichTable && srcR < pRichTable->get_num_rows()) {
                CtRichCell* pCell = pRichTable->getRichCell(srcR, c);
                if (pCell) {
                    std::vector<std::pair<double,double>> img_dims; // (srcW, srcH)
                    for (CtAnchoredWidget* pW : pCell->getEmbeddedWidgets()) {
                        auto* pImg = dynamic_cast<CtImage*>(pW);
                        if (!pImg) continue;
                        auto pixbuf = pImg->get_pixbuf();
                        if (!pixbuf) continue;
                        const double srcW = pixbuf->get_width();
                        const double srcH = pixbuf->get_height();
                        if (srcW > 0 && srcH > 0) img_dims.emplace_back(srcW, srcH);
                    }
                    if (!img_dims.empty()) {
                        const double maxW = cols_w[c] > 0 ? cols_w[c] : img_dims.front().first;
                        // Mirror _draw_table_text: scale images by fit_scale (capped to
                        // not exceed natural size or the cell's available space). This
                        // keeps cell heights in lock-step with what gets drawn.
                        if (rowFixedScaled > 0) {
                            const double avail_h = std::max(0.0, rowFixedScaled - cell_text_height);
                            const double per_image_h = avail_h / img_dims.size();
                            for (auto [sw, sh] : img_dims) {
                                double s = std::min(1.0, fit_scale);
                                if (sw * s > maxW) s = maxW / sw;
                                if (sh * s > per_image_h) s = per_image_h / sh;
                                cell_height += sh * std::max(0.0, s);
                            }
                        }
                        else {
                            for (auto [sw, sh] : img_dims) {
                                double s = std::min(1.0, fit_scale);
                                if (sw * s > maxW) s = maxW / sw;
                                cell_height += sh * std::max(0.0, s);
                            }
                        }
                    }
                }
            }
            // Add cell padding (top + bottom) so the row is tall enough to keep
            // the content from touching the cell border. _draw_table_text uses
            // `cell_pad = 4 * _doc_scale` and insets the content by it on each
            // side; mirror that here so a flexible row grows to accommodate.
            const double cell_pad_pts = 4.0 * _doc_scale;
            const double cell_height_with_pad = cell_height + 2 * cell_pad_pts;
            if (cell_height_with_pad > rows_h[r]) rows_h[r] = cell_height_with_pad;
        }
        // Pin to the explicit row height if set.
        if (rowFixedScaled > 0) {
            rows_h[r] = rowFixedScaled;
        }
    }
}

double CtPrint::_table_get_width_height(std::vector<double>& data)
{
    double acc = 0;
    for (auto& value: data)
        acc += value + (_table_line_thickness * _page_dpi_scale * _doc_scale);
    return acc;
}

int CtPrint::_table_split_content(const CtTableCommon* table,
                                  const int start_row,
                                  const int check_height,
                                  const Glib::RefPtr<Gtk::PrintContext>& context,
                                  const double fit_scale)
{
    int last_row = start_row;
    CtTableColWidths scaled_col_widths;
    for (auto cw : table->get_col_widths()) scaled_col_widths.push_back(int(cw * fit_scale));
    for (; last_row < (int)table->get_num_rows(); ++last_row) {
        std::vector<double> rows_h, cols_w;
        auto table_layouts = _table_get_layouts(table, start_row, last_row, context, fit_scale);
        _table_get_grid(table_layouts, scaled_col_widths, rows_h, cols_w, table, start_row, fit_scale);
        double table_height = _table_get_width_height(rows_h);
        if (table_height > check_height) {
            if (start_row == last_row) // not enouth place for 1 row + a header, add a new page
                return -1;
            return last_row -1;
        }
    }
    return -1; // though we shouldn't be here
}

void CtPrint::_draw_codebox_box(Cairo::RefPtr<Cairo::Context> cairo_context, double x0, double y0, double codebox_width, double codebox_height)
{
    cairo_context->set_source_rgba(0, 0, 0, 0.3);
    cairo_context->rectangle(x0, y0, codebox_width, codebox_height);
    cairo_context->stroke();
}

void CtPrint::_draw_codebox_code(Cairo::RefPtr<Cairo::Context> cairo_context, Glib::RefPtr<Pango::Layout> codebox_layout, double x0, double y0)
{
    double y = y0;
    cairo_context->set_source_rgb(0, 0, 0);
    for (int layout_line_idx = 0; layout_line_idx < codebox_layout->get_line_count(); ++layout_line_idx)
    {
        auto layout_line = codebox_layout->get_line(layout_line_idx);
        double line_height = _get_width_height_from_layout_line(layout_line).height;
        cairo_context->move_to(x0 + (CtConst::GRID_SLIP_OFFSET * _page_dpi_scale), y + line_height);
        y += line_height;
        layout_line->show_in_cairo_context(cairo_context);
    }
}

void CtPrint::_apply_doc_scale_to_layout(Glib::RefPtr<Pango::Layout> layout)
{
    if (_doc_scale >= 1.0 or not layout) return;
    PangoAttrList* current = pango_layout_get_attributes(layout->gobj());
    PangoAttrList* attrs = current ? pango_attr_list_copy(current) : pango_attr_list_new();
    PangoAttribute* scale_attr = pango_attr_scale_new(_doc_scale);
    scale_attr->start_index = 0;
    scale_attr->end_index = G_MAXUINT;
    pango_attr_list_insert(attrs, scale_attr);
    pango_layout_set_attributes(layout->gobj(), attrs);
    pango_attr_list_unref(attrs);
}

// Parse "#RRGGBB" or "#RRRRGGGGBBBB" to 0..1 RGB. Returns false if format is unknown.
static bool _parse_hex_rgb(const std::string& hex, double& r, double& g, double& b)
{
    if (hex.size() == 7 && hex[0] == '#') {
        try {
            r = std::stoi(hex.substr(1, 2), nullptr, 16) / 255.0;
            g = std::stoi(hex.substr(3, 2), nullptr, 16) / 255.0;
            b = std::stoi(hex.substr(5, 2), nullptr, 16) / 255.0;
            return true;
        } catch (...) { return false; }
    }
    if (hex.size() == 13 && hex[0] == '#') {
        try {
            r = std::stoi(hex.substr(1, 4), nullptr, 16) / 65535.0;
            g = std::stoi(hex.substr(5, 4), nullptr, 16) / 65535.0;
            b = std::stoi(hex.substr(9, 4), nullptr, 16) / 65535.0;
            return true;
        } catch (...) { return false; }
    }
    return false;
}

void CtPrint::_draw_table_grid(Cairo::RefPtr<Cairo::Context> cairo_context,
                               const std::vector<double>& rows_h,
                               const std::vector<double>& cols_w,
                               double x0,
                               double y0,
                               double table_width,
                               double table_height,
                               const CtPageTable* page_table)
{
    // Map a layout-row index back into the source-table row index (header at 0,
    // body row r in layouts → source row first_row + (r-1)).
    auto layoutRowToSrc = [page_table](size_t layoutRow) -> size_t {
        if (!page_table || !page_table->pTable) return layoutRow;
        if (0 == layoutRow) return 0; // header
        return static_cast<size_t>(page_table->first_row) + (layoutRow - 1);
    };

    const CtTableStyle* pStyle = (page_table && page_table->pTable) ? &page_table->pTable->getTableStyle() : nullptr;
    const bool hasCellBg = pStyle && !pStyle->cellBgColors.empty();
    const bool hasTableBg = pStyle && !pStyle->tableBgColor.empty();

    // Fill table-level background first (if any)
    if (hasTableBg) {
        double r, g, b;
        if (_parse_hex_rgb(pStyle->tableBgColor, r, g, b)) {
            cairo_context->save();
            cairo_context->set_source_rgb(r, g, b);
            cairo_context->rectangle(x0, y0, table_width, table_height);
            cairo_context->fill();
            cairo_context->restore();
        }
    }
    // Fill per-cell backgrounds
    if (hasCellBg) {
        double yy = y0;
        for (size_t r = 0; r < rows_h.size(); ++r) {
            double xx = x0;
            for (size_t c = 0; c < cols_w.size(); ++c) {
                const auto srcR = layoutRowToSrc(r);
                const auto it = pStyle->cellBgColors.find({srcR, c});
                if (it != pStyle->cellBgColors.end()) {
                    double rr, gg, bb;
                    if (_parse_hex_rgb(it->second, rr, gg, bb)) {
                        cairo_context->save();
                        cairo_context->set_source_rgb(rr, gg, bb);
                        cairo_context->rectangle(xx, yy, cols_w[c] + _table_line_thickness * _page_dpi_scale * _doc_scale,
                                                          rows_h[r] + _table_line_thickness * _page_dpi_scale * _doc_scale);
                        cairo_context->fill();
                        cairo_context->restore();
                    }
                }
                xx += cols_w[c] + _table_line_thickness * _page_dpi_scale * _doc_scale;
            }
            yy += rows_h[r] + _table_line_thickness * _page_dpi_scale * _doc_scale;
        }
    }

    // Default border style (faint grey, 1px) — used unless the table has any custom border styling.
    double defBorderR = 0.0, defBorderG = 0.0, defBorderB = 0.0, defBorderA = 0.3;
    double defBorderW = 1.0;
    bool hasCustomBorders = pStyle &&
        (pStyle->borderWidth != 1 || pStyle->borderColor != "#000000" ||
         !pStyle->cellBorderWidths.empty() || !pStyle->cellBorderColors.empty());

    // Borders are drawn at point widths, but should also scale with the document
    // so they don't appear disproportionately thick when content is shrunk.
    const double border_scale = _doc_scale;
    if (hasCustomBorders) {
        // Use the table's own border as the default for every cell unless a per-cell override applies.
        double dr, dg, db;
        if (_parse_hex_rgb(pStyle->borderColor, dr, dg, db)) {
            defBorderR = dr; defBorderG = dg; defBorderB = db; defBorderA = 1.0;
        }
        defBorderW = std::max(0, pStyle->borderWidth) * _page_dpi_scale * border_scale;
    }

    auto resolveCellBorder = [&](size_t srcR, size_t c, double& outR, double& outG, double& outB, double& outA, double& outW) {
        outR = defBorderR; outG = defBorderG; outB = defBorderB; outA = defBorderA;
        outW = defBorderW;
        if (!pStyle) return;
        const auto wIt = pStyle->cellBorderWidths.find({srcR, c});
        if (wIt != pStyle->cellBorderWidths.end()) {
            outW = std::max(0, wIt->second) * _page_dpi_scale * border_scale;
        }
        const auto cIt = pStyle->cellBorderColors.find({srcR, c});
        if (cIt != pStyle->cellBorderColors.end()) {
            double rr, gg, bb;
            if (_parse_hex_rgb(cIt->second, rr, gg, bb)) {
                outR = rr; outG = gg; outB = bb; outA = 1.0;
            }
        }
    };

    if (!hasCustomBorders) {
        // Original simple grid (faint grey lines)
        double x = x0;
        double y = y0;
        cairo_context->set_source_rgba(defBorderR, defBorderG, defBorderB, defBorderA);
        cairo_context->set_line_width(defBorderW);
        cairo_context->move_to(x, y);
        cairo_context->line_to(x + table_width, y);
        for (auto& row_h : rows_h) {
            y += row_h + (_table_line_thickness * _page_dpi_scale * _doc_scale);
            cairo_context->move_to(x, y);
            cairo_context->line_to(x + table_width, y);
        }
        y = y0;
        cairo_context->move_to(x, y);
        cairo_context->line_to(x, y + table_height);
        for (auto& col_w : cols_w) {
            x += col_w + (_table_line_thickness * _page_dpi_scale * _doc_scale);
            cairo_context->move_to(x, y);
            cairo_context->line_to(x, y + table_height);
        }
        cairo_context->stroke();
        return;
    }

    // Custom borders: each cell draws only its TOP and LEFT edges (plus its
    // BOTTOM in the last row and RIGHT in the last column). This way each
    // shared edge is drawn exactly once, so internal edges aren't visually
    // doubled compared to outer edges.
    double yy = y0;
    auto strokeLine = [&](double r_, double g_, double b_, double a_, double w_,
                          double x1, double y1, double x2, double y2) {
        if (w_ <= 0) return;
        cairo_context->save();
        cairo_context->set_source_rgba(r_, g_, b_, a_);
        cairo_context->set_line_width(w_);
        cairo_context->move_to(x1, y1);
        cairo_context->line_to(x2, y2);
        cairo_context->stroke();
        cairo_context->restore();
    };
    for (size_t r = 0; r < rows_h.size(); ++r) {
        double xx = x0;
        for (size_t c = 0; c < cols_w.size(); ++c) {
            double rr, gg, bb, aa, ww;
            resolveCellBorder(layoutRowToSrc(r), c, rr, gg, bb, aa, ww);
            const double cellW = cols_w[c] + _table_line_thickness * _page_dpi_scale * _doc_scale;
            const double cellH = rows_h[r] + _table_line_thickness * _page_dpi_scale * _doc_scale;
            // Top edge (always)
            strokeLine(rr, gg, bb, aa, ww, xx, yy + ww / 2.0, xx + cellW, yy + ww / 2.0);
            // Left edge (always)
            strokeLine(rr, gg, bb, aa, ww, xx + ww / 2.0, yy, xx + ww / 2.0, yy + cellH);
            // Bottom edge — only for last row
            if (r + 1 == rows_h.size()) {
                strokeLine(rr, gg, bb, aa, ww, xx, yy + cellH - ww / 2.0, xx + cellW, yy + cellH - ww / 2.0);
            }
            // Right edge — only for last column
            if (c + 1 == cols_w.size()) {
                strokeLine(rr, gg, bb, aa, ww, xx + cellW - ww / 2.0, yy, xx + cellW - ww / 2.0, yy + cellH);
            }
            xx += cellW;
        }
        yy += rows_h[r] + _table_line_thickness * _page_dpi_scale * _doc_scale;
    }
}

void CtPrint::_draw_table_text(Cairo::RefPtr<Cairo::Context> cairo_context,
                               const std::vector<double>& rows_h,
                               const std::vector<double>& cols_w,
                               const CtPageTable::TableLayouts& table_layouts,
                               double x0,
                               double y0,
                               const CtPageTable* page_table)
{
    auto layoutRowToSrc = [page_table](size_t layoutRow) -> size_t {
        if (!page_table || !page_table->pTable) return layoutRow;
        if (0 == layoutRow) return 0;
        return static_cast<size_t>(page_table->first_row) + (layoutRow - 1);
    };
    auto* pRichTable = page_table ? dynamic_cast<const CtTableRich*>(page_table->pTable) : nullptr;

    // Resolve per-cell vertical alignment from the table style. Defaults to "top"
    // (for rich tables) or empty (for plain tables, falls through to "top").
    auto resolveVAlign = [page_table, layoutRowToSrc](size_t layoutRow, size_t c) -> std::string {
        if (!page_table || !page_table->pTable) return "";
        const CtTableStyle& s = page_table->pTable->getTableStyle();
        const auto it = s.cellVAlign.find({layoutRowToSrc(layoutRow), c});
        if (it != s.cellVAlign.end()) return it->second;
        return s.tableVAlignDefault;
    };
    auto resolveHAlign = [page_table, layoutRowToSrc](size_t layoutRow, size_t c) -> std::string {
        if (!page_table || !page_table->pTable) return "";
        const CtTableStyle& s = page_table->pTable->getTableStyle();
        const auto it = s.cellHAlign.find({layoutRowToSrc(layoutRow), c});
        if (it != s.cellHAlign.end()) return it->second;
        return s.tableHAlignDefault;
    };
    auto isCellWrapOff = [page_table, layoutRowToSrc](size_t layoutRow, size_t c) -> bool {
        if (!page_table || !page_table->pTable) return false;
        const CtTableStyle& s = page_table->pTable->getTableStyle();
        const auto it = s.cellWrap.find({layoutRowToSrc(layoutRow), c});
        if (it != s.cellWrap.end()) return !it->second;
        if (s.tableWrapDefaultSet) return !s.tableWrapDefault;
        return false;
    };

    // Cell padding so the content doesn't sit flush against the border line.
    // Scaled by _doc_scale so it stays proportional when the document is shrunk.
    const double cell_pad = 4.0 * _doc_scale;

    cairo_context->set_source_rgb(0, 0, 0);
    double y = y0;
    for (size_t i = 0; i < rows_h.size(); ++i) {
        double row_h = rows_h[i];
        double x = x0 + (CtConst::GRID_SLIP_OFFSET * _page_dpi_scale);
        for (size_t j = 0; j < cols_w.size(); ++j) {
            double col_w = cols_w[j];
            auto layout_cell = table_layouts[i][j];

            // The cell's Pango layout already has shape attributes reserving inline
            // space for embedded images, so layout->get_size() gives the full content
            // box (text + image gaps). Use that to apply valign and overall
            // positioning, then walk the layout to find each U+FFFC position and
            // paint the actual image there.
            int layoutW_pango = 0, layoutH_pango = 0;
            layout_cell->get_size(layoutW_pango, layoutH_pango);
            const double content_w = double(layoutW_pango) / Pango::SCALE;
            const double content_h = double(layoutH_pango) / Pango::SCALE;

            // Inner content box, with cell padding on all sides.
            const double inner_w = std::max(0.0, col_w - 2 * cell_pad);
            const double inner_h = std::max(0.0, row_h - 2 * cell_pad);

            double local_y = y + cell_pad;
            const std::string valign = resolveVAlign(i, j);
            if (valign == "middle" || valign == "center") {
                local_y = y + cell_pad + std::max(0.0, (inner_h - content_h) / 2.0);
            }
            else if (valign == "bottom") {
                local_y = y + cell_pad + std::max(0.0, inner_h - content_h);
            }

            // For wrap-off cells Pango's set_alignment doesn't apply (width=-1), so
            // shift the draw origin manually so single-line content respects halign.
            double draw_x = x + cell_pad;
            if (isCellWrapOff(i, j)) {
                const std::string halign = resolveHAlign(i, j);
                if (halign == "center") {
                    draw_x = x + cell_pad + std::max(0.0, (inner_w - content_w) / 2.0);
                }
                else if (halign == "right") {
                    draw_x = x + cell_pad + std::max(0.0, inner_w - content_w);
                }
            }

            // Render the layout (text). Pango leaves rectangular gaps where shape
            // attrs reserve image space.
            cairo_context->save();
            cairo_context->rectangle(x, y, col_w, row_h);
            cairo_context->clip();
            cairo_context->move_to(draw_x, local_y);
            layout_cell->show_in_cairo_context(cairo_context);

            // Walk layout text byte-by-byte: every U+FFFC corresponds to one
            // embedded image; use index_to_pos to get its layout-relative rect.
            if (pRichTable) {
                const size_t srcR = layoutRowToSrc(i);
                if (srcR < pRichTable->get_num_rows()) {
                    CtRichCell* pCell = pRichTable->getRichCell(srcR, j);
                    if (pCell) {
                        // Build ordered image list (matching the order shape attrs
                        // were inserted in _table_get_layouts).
                        std::vector<const CtImage*> ordered_images;
                        auto rBuffer = pCell->get_buffer();
                        if (rBuffer) {
                            for (Gtk::TextIter it = rBuffer->begin(); ; ) {
                                Glib::RefPtr<Gtk::TextChildAnchor> rA = it.get_child_anchor();
                                if (rA) {
                                    for (CtAnchoredWidget* pW : pCell->getEmbeddedWidgets()) {
                                        if (pW->getTextChildAnchor() == rA) {
                                            if (auto* pImg = dynamic_cast<CtImage*>(pW)) {
                                                ordered_images.push_back(pImg);
                                            }
                                            break;
                                        }
                                    }
                                }
                                if (!it.forward_char()) break;
                            }
                        }
                        // Find U+FFFC byte offsets in the layout text.
                        const char* layoutText = pango_layout_get_text(layout_cell->gobj());
                        std::vector<int> ufffc_offsets;
                        if (layoutText) {
                            for (size_t bi = 0; layoutText[bi] != '\0'; ) {
                                if ((unsigned char)layoutText[bi] == 0xEF &&
                                    (unsigned char)layoutText[bi+1] == 0xBF &&
                                    (unsigned char)layoutText[bi+2] == 0xBC) {
                                    ufffc_offsets.push_back(static_cast<int>(bi));
                                    bi += 3;
                                } else {
                                    ++bi;
                                }
                            }
                        }
                        const size_t common_n = std::min(ufffc_offsets.size(), ordered_images.size());
                        for (size_t k = 0; k < common_n; ++k) {
                            const CtImage* pImg = ordered_images[k];
                            auto pixbuf = pImg->get_pixbuf();
                            if (!pixbuf) continue;
                            const double srcW = pixbuf->get_width();
                            const double srcH = pixbuf->get_height();
                            if (srcW <= 0 || srcH <= 0) continue;
                            PangoRectangle pos;
                            pango_layout_index_to_pos(layout_cell->gobj(), ufffc_offsets[k], &pos);
                            // The shape attr was placed with its bottom at the
                            // baseline (logical_rect.y = -h, height = h), so pos.y
                            // is at the line top and pos.width matches the shape
                            // attr's reserved width. Derive the render scale from
                            // pos.width so the rendered image exactly fills the
                            // reserved gap (handles the case where _table_get_layouts
                            // capped the image to fit the cell).
                            const double img_x = draw_x + double(pos.x) / Pango::SCALE;
                            const double img_y = local_y + double(pos.y) / Pango::SCALE;
                            const double drawn_w = double(pos.width) / Pango::SCALE;
                            const double scale = pixbuf->get_width() > 0
                                ? drawn_w / pixbuf->get_width() : _doc_scale;
                            cairo_context->save();
                            cairo_context->translate(img_x, img_y);
                            cairo_context->scale(scale, scale);
                            Gdk::Cairo::set_source_pixbuf(cairo_context, pixbuf, 0, 0);
                            cairo_context->paint();
                            cairo_context->restore();
                        }
                    }
                }
            }
            cairo_context->restore();

            x += col_w + (_table_line_thickness * _page_dpi_scale * _doc_scale);
        }
        y += row_h + (_table_line_thickness * _page_dpi_scale * _doc_scale);
    }
}

Cairo::Rectangle CtPrint::_get_width_height_from_layout_line(Glib::RefPtr<const Pango::LayoutLine> line)
{
    Pango::Rectangle ink_rect, logical_rect;
    line->get_extents(ink_rect, logical_rect);
    Cairo::Rectangle rect;
    rect.width = logical_rect.get_width() / Pango::SCALE;
    rect.height = logical_rect.get_height() / Pango::SCALE;
    return rect;
}

double CtPrint::_get_height_from_layout(Glib::RefPtr<Pango::Layout> layout)
{
    double height = 0;
    for (int layout_line_idx = 0; layout_line_idx < layout->get_line_count(); ++layout_line_idx) {
        Glib::RefPtr<const Pango::LayoutLine> layout_line = layout->get_line(layout_line_idx);
        double line_height = _get_width_height_from_layout_line(layout_line).height;
        height += line_height;
    }
    return height + 2 * (CtConst::GRID_SLIP_OFFSET * _page_dpi_scale);
}

double CtPrint::_get_width_from_layout(Glib::RefPtr<Pango::Layout> layout)
{
    double width = 0;
    for (int layout_line_idx = 0; layout_line_idx < layout->get_line_count(); ++layout_line_idx) {
        Glib::RefPtr<const Pango::LayoutLine> layout_line = layout->get_line(layout_line_idx);
        double line_width = _get_width_height_from_layout_line(layout_line).width;
        if (line_width > width)
            width = line_width;
    }
    return width + 2 * (CtConst::GRID_SLIP_OFFSET * _page_dpi_scale);
}
