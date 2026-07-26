/*
 * ct_history_panel.cc
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

#include "ct_history_panel.h"
#include "ct_main_win.h"
#include "ct_storage_control.h"
#include "ct_misc_utils.h"
#include "ct_const.h"
#include <spdlog/spdlog.h>

CtHistoryPanel::CtHistoryPanel(CtMainWin* pCtMainWin)
    : _pCtMainWin{pCtMainWin}
{
    _rListStore = Gtk::ListStore::create(_columns);
    _treeView.set_model(_rListStore);
    _treeView.set_headers_visible(true);
    _treeView.set_enable_search(false);

    auto colIdx = _treeView.append_column("", _columns.colIcon);
    _treeView.get_column(colIdx - 1)->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);

    colIdx = _treeView.append_column(_("Node"), _columns.colName);
    auto* pNameCol = _treeView.get_column(colIdx - 1);
    pNameCol->set_expand(true);
    pNameCol->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);

    colIdx = _treeView.append_column(_("Time"), _columns.colTimestamp);
    _treeView.get_column(colIdx - 1)->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);

    _treeView.set_activate_on_single_click(true);
    _treeView.signal_row_activated().connect(
        sigc::mem_fun(*this, &CtHistoryPanel::_on_row_activated));

    _scrolledWindow.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
#if GTKMM_MAJOR_VERSION >= 4
    _scrolledWindow.set_child(_treeView);
#else
    _scrolledWindow.add(_treeView);
#endif
    _scrolledWindow.set_size_request(150, -1);
}

void CtHistoryPanel::add_entry(const CtHistoryEntry& entry)
{
    _updatingList = true;
    auto on_scope_exit = scope_guard([this](void*) { _updatingList = false; });

    auto row = *_rListStore->prepend();
    row[_columns.colNodeId] = entry.nodeId;
    row[_columns.colCursorPos] = entry.cursorPos;
    row[_columns.colScrollPos] = entry.scrollPos;
    row[_columns.colTimestampRaw] = entry.timestamp;
    row[_columns.colCanvasEditX] = entry.canvasEditX;
    row[_columns.colCanvasEditY] = entry.canvasEditY;
    row[_columns.colTimestamp] = str::time_format(
        _pCtMainWin->get_ct_config()->timestampFormat, static_cast<time_t>(entry.timestamp));

    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(entry.nodeId);
    if (ctIter) {
        row[_columns.colName] = ctIter.get_node_name();
        row[_columns.colIcon] = ctIter.get_node_icon();
    }
    else {
        row[_columns.colName] = "?";
    }

    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_history();
    }
}

void CtHistoryPanel::update_entry(gint64 nodeId, int cursorPos, int scrollPos, int canvasEditX, int canvasEditY)
{
    CtHistoryEntry entry;
    entry.nodeId = nodeId;
    entry.timestamp = std::time(nullptr);
    entry.cursorPos = cursorPos;
    entry.scrollPos = scrollPos;
    entry.canvasEditX = canvasEditX;
    entry.canvasEditY = canvasEditY;
    add_entry(entry);
}

void CtHistoryPanel::remove_entries_for_node(gint64 nodeId)
{
    bool removed = false;
    for (auto it = _rListStore->children().begin(); it != _rListStore->children().end(); ) {
        if ((*it)[_columns.colNodeId] == nodeId) {
            it = _rListStore->erase(it);
            removed = true;
        }
        else {
            ++it;
        }
    }
    if (removed) {
        if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
            pStorage->pending_edit_db_history();
        }
    }
}

void CtHistoryPanel::clear()
{
    _updatingList = true;
    _rListStore->clear();
    _updatingList = false;
    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_history();
    }
}

std::vector<CtHistoryEntry> CtHistoryPanel::get_all_entries() const
{
    std::vector<CtHistoryEntry> entries;
    for (const auto& row : _rListStore->children()) {
        CtHistoryEntry e;
        e.nodeId = row[_columns.colNodeId];
        e.timestamp = row[_columns.colTimestampRaw];
        e.cursorPos = row[_columns.colCursorPos];
        e.scrollPos = row[_columns.colScrollPos];
        e.canvasEditX = row[_columns.colCanvasEditX];
        e.canvasEditY = row[_columns.colCanvasEditY];
        entries.push_back(e);
    }
    return entries;
}

void CtHistoryPanel::load_entries(const std::vector<CtHistoryEntry>& entries)
{
    _updatingList = true;
    _rListStore->clear();
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        auto row = *_rListStore->prepend();
        row[_columns.colNodeId] = it->nodeId;
        row[_columns.colCursorPos] = it->cursorPos;
        row[_columns.colScrollPos] = it->scrollPos;
        row[_columns.colTimestampRaw] = it->timestamp;
        row[_columns.colCanvasEditX] = it->canvasEditX;
        row[_columns.colCanvasEditY] = it->canvasEditY;
        row[_columns.colTimestamp] = str::time_format(
            _pCtMainWin->get_ct_config()->timestampFormat, static_cast<time_t>(it->timestamp));
        CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(it->nodeId);
        if (ctIter) {
            row[_columns.colName] = ctIter.get_node_name();
            row[_columns.colIcon] = ctIter.get_node_icon();
        }
        else {
            row[_columns.colName] = "?";
        }
    }
    _updatingList = false;
}

void CtHistoryPanel::refresh_names()
{
    for (auto it = _rListStore->children().begin(); it != _rListStore->children().end(); ) {
        gint64 nodeId = (*it)[_columns.colNodeId];
        CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
        if (ctIter) {
            (*it)[_columns.colName] = ctIter.get_node_name();
            (*it)[_columns.colIcon] = ctIter.get_node_icon();
            ++it;
        }
        else {
            it = _rListStore->erase(it);
        }
    }
}

void CtHistoryPanel::_on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* /*column*/)
{
    if (_updatingList) return;
    auto iter = _rListStore->get_iter(path);
    if (!iter) return;

    CtHistoryEntry entry;
    entry.nodeId = (*iter)[_columns.colNodeId];
    entry.cursorPos = (*iter)[_columns.colCursorPos];
    entry.scrollPos = (*iter)[_columns.colScrollPos];
    entry.canvasEditX = (*iter)[_columns.colCanvasEditX];
    entry.canvasEditY = (*iter)[_columns.colCanvasEditY];
    _navigate_to_entry(entry);
}

void CtHistoryPanel::_navigate_to_entry(const CtHistoryEntry& entry)
{
    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(entry.nodeId);
    if (!ctIter) {
        spdlog::warn("History: node {} not found", entry.nodeId);
        return;
    }

    _pCtMainWin->_navigatingHistory = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; });

    auto store = _pCtMainWin->get_tree_store().get_store();
    _pCtMainWin->get_tree_view().set_cursor_safe(ctIter);
    _pCtMainWin->get_tree_view().scroll_to_row(store->get_path(ctIter), 0.5);

    Glib::signal_idle().connect_once([this, entry]() {
        auto rBuffer = _pCtMainWin->curr_buffer();
        if (!rBuffer) return;

        int charCount = rBuffer->get_char_count();
        int cursorPos = std::min(entry.cursorPos, charCount);
        auto iterCursor = rBuffer->get_iter_at_offset(cursorPos);
        rBuffer->place_cursor(iterCursor);

        auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
        if (vAdj) {
            double scrollVal = std::min(static_cast<double>(entry.scrollPos),
                                        vAdj->get_upper() - vAdj->get_page_size());
            vAdj->set_value(scrollVal);
        }

        int cex = entry.canvasEditX;
        int cey = entry.canvasEditY;
        Glib::signal_idle().connect_once([this, cursorPos, cex, cey]() {
            _show_target_sign(cursorPos, cex, cey);
        });
    });
}

void CtHistoryPanel::_show_target_sign(int cursorPos, int canvasEditX, int canvasEditY)
{
    auto& textView = _pCtMainWin->get_text_view().mm();
    auto rBuffer = textView.get_buffer();
    if (!rBuffer) return;

    int winX, winY;

    if (canvasEditX >= 0 && canvasEditY >= 0) {
        auto hAdj = _pCtMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
        double hScroll = hAdj ? hAdj->get_value() : 0.0;
        double vScroll = vAdj ? vAdj->get_value() : 0.0;
        double zoom = _pCtMainWin->get_rt_zoom_scale_factor();
        winX = static_cast<int>(canvasEditX * zoom - hScroll);
        winY = static_cast<int>(canvasEditY * zoom - vScroll);
    }
    else {
        int cp = std::min(cursorPos, rBuffer->get_char_count());
        auto iterCursor = rBuffer->get_iter_at_offset(cp);
        Gdk::Rectangle rect;
        textView.get_iter_location(iterCursor, rect);
        textView.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET,
            rect.get_x() + rect.get_width() / 2,
            rect.get_y() + rect.get_height() / 2,
            winX, winY);
    }

    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    int viewportH = vAdj ? static_cast<int>(vAdj->get_page_size()) : 0;
    if (winY < 0 || winY > viewportH) {
        winX = textView.get_allocated_width() / 2;
        winY = viewportH / 2;
    }

    _pCtMainWin->get_drawing_overlay()->showTargetSign(winX, winY);
}
