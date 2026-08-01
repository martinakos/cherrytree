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
#include "ct_command_bridge.h"
#include "ct_storage_control.h"
#include "ct_misc_utils.h"
#include "ct_const.h"
#include "ct_drawing.h"
#include "ct_widgets.h"
#include <spdlog/spdlog.h>

CtHistoryPanel::CtHistoryPanel(CtMainWin* pCtMainWin)
    : _pCtMainWin{pCtMainWin}
{
    _rTreeStore = Gtk::TreeStore::create(_columns);
    _treeView.set_model(_rTreeStore);
    _treeView.set_headers_visible(true);
    _treeView.set_enable_search(false);

    auto colIdx = _treeView.append_column("", _columns.colIcon);
    _treeView.get_column(colIdx - 1)->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    _treeView.get_column(colIdx - 1)->set_fixed_width(40);

    colIdx = _treeView.append_column(_("Node"), _columns.colName);
    auto* pNameCol = _treeView.get_column(colIdx - 1);
    pNameCol->set_resizable(true);
    pNameCol->set_expand(true);
    pNameCol->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    pNameCol->set_min_width(50);

    colIdx = _treeView.append_column(_("Operation"), _columns.colOperation);
    auto* pOpCol = _treeView.get_column(colIdx - 1);
    pOpCol->set_resizable(true);
    pOpCol->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    pOpCol->set_min_width(50);
    pOpCol->set_fixed_width(200);

    colIdx = _treeView.append_column(_("Time"), _columns.colTimestamp);
    auto* pTimeCol = _treeView.get_column(colIdx - 1);
    pTimeCol->set_resizable(true);
    pTimeCol->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    pTimeCol->set_min_width(50);

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

Gtk::TreeModel::iterator CtHistoryPanel::_findOrCreateParent(gint64 nodeId)
{
    for (auto it = _rTreeStore->children().begin(); it != _rTreeStore->children().end(); ++it) {
        if ((*it)[_columns.colNodeId] == nodeId && (*it)[_columns.colIsParent]) {
            return it;
        }
    }
    // Create new parent row at top
    auto parentRow = *_rTreeStore->prepend();
    parentRow[_columns.colNodeId] = nodeId;
    parentRow[_columns.colIsParent] = true;
    parentRow[_columns.colRegionOffset] = -1;
    parentRow[_columns.colRegionLength] = 0;
    parentRow[_columns.colCanvasIdx] = -1;
    parentRow[_columns.colUseDay] = 0;

    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (ctIter) {
        parentRow[_columns.colName] = ctIter.get_node_name();
        parentRow[_columns.colIcon] = ctIter.get_node_icon();
    }
    else {
        parentRow[_columns.colName] = Glib::ustring("(") + _("deleted") + ")";
    }
    return _rTreeStore->children().begin();
}

void CtHistoryPanel::add_entry(const CtHistoryEntry& entry)
{
    _updatingList = true;
    auto on_scope_exit = scope_guard([this](void*) { _updatingList = false; });

    auto parentIter = _findOrCreateParent(entry.nodeId);

    // Demote the current parent data to a sub-entry before overwriting,
    // but only if the parent already has real data (timestampRaw > 0)
    gint64 prevTimestamp = (*parentIter)[_columns.colTimestampRaw];
    if (prevTimestamp > 0) {
        auto prevRow = *_rTreeStore->prepend(parentIter->children());
        prevRow[_columns.colNodeId]       = static_cast<gint64>((*parentIter)[_columns.colNodeId]);
        prevRow[_columns.colCursorPos]    = static_cast<int>((*parentIter)[_columns.colCursorPos]);
        prevRow[_columns.colScrollPos]    = static_cast<int>((*parentIter)[_columns.colScrollPos]);
        prevRow[_columns.colTimestampRaw] = prevTimestamp;
        prevRow[_columns.colOperation]    = static_cast<Glib::ustring>((*parentIter)[_columns.colOperation]);
        prevRow[_columns.colActionType]   = static_cast<Glib::ustring>((*parentIter)[_columns.colActionType]);
        prevRow[_columns.colRegionOffset] = static_cast<int>((*parentIter)[_columns.colRegionOffset]);
        prevRow[_columns.colRegionLength] = static_cast<int>((*parentIter)[_columns.colRegionLength]);
        prevRow[_columns.colCanvasIdx]    = static_cast<int>((*parentIter)[_columns.colCanvasIdx]);
        prevRow[_columns.colUseDay]       = static_cast<int>((*parentIter)[_columns.colUseDay]);
        prevRow[_columns.colIsParent]     = false;
        prevRow[_columns.colTimestamp]    = static_cast<Glib::ustring>((*parentIter)[_columns.colTimestamp]);
    }

    // Update parent row to show the new (latest) action
    Glib::ustring tsStr = str::time_format(
        _pCtMainWin->get_ct_config()->timestampFormat, static_cast<time_t>(entry.timestamp));
    (*parentIter)[_columns.colOperation]    = Glib::ustring(entry.actionDescription);
    (*parentIter)[_columns.colTimestamp]     = tsStr;
    (*parentIter)[_columns.colTimestampRaw] = entry.timestamp;
    (*parentIter)[_columns.colCursorPos]    = entry.cursorPos;
    (*parentIter)[_columns.colScrollPos]    = entry.scrollPos;
    (*parentIter)[_columns.colActionType]   = Glib::ustring(entry.actionType);
    (*parentIter)[_columns.colRegionOffset] = entry.regionOffset;
    (*parentIter)[_columns.colRegionLength] = entry.regionLength;
    (*parentIter)[_columns.colCanvasIdx]    = entry.canvasIdx;
    (*parentIter)[_columns.colUseDay]       = entry.useDay;

    // Re-sort: move this parent to top if it's not already there
    auto firstIter = _rTreeStore->children().begin();
    if (parentIter != firstIter) {
        _rTreeStore->move(parentIter, firstIter);
    }

    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_history();
    }
}

void CtHistoryPanel::updateCursorScroll(gint64 nodeId, int cursorPos, int scrollPos)
{
    for (auto it = _rTreeStore->children().begin(); it != _rTreeStore->children().end(); ++it) {
        if ((*it)[_columns.colNodeId] == nodeId && (*it)[_columns.colIsParent]) {
            (*it)[_columns.colCursorPos] = cursorPos;
            (*it)[_columns.colScrollPos] = scrollPos;
            break;
        }
    }
}

void CtHistoryPanel::remove_entries_for_node(gint64 nodeId)
{
    bool removed = false;
    for (auto it = _rTreeStore->children().begin(); it != _rTreeStore->children().end(); ) {
        if ((*it)[_columns.colNodeId] == nodeId) {
            it = _rTreeStore->erase(it);
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
    _stopFlash();
    _rTreeStore->clear();
    _updatingList = false;
    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_history();
    }
}

std::vector<CtHistoryEntry> CtHistoryPanel::get_all_entries() const
{
    std::vector<CtHistoryEntry> entries;
    for (const auto& parentRow : _rTreeStore->children()) {
        // The parent row holds the latest entry (not duplicated as a child)
        gint64 parentTs = parentRow[_columns.colTimestampRaw];
        if (parentTs > 0) {
            CtHistoryEntry e;
            e.nodeId = parentRow[_columns.colNodeId];
            e.timestamp = parentTs;
            e.cursorPos = parentRow[_columns.colCursorPos];
            e.scrollPos = parentRow[_columns.colScrollPos];
            e.actionDescription = static_cast<Glib::ustring>(parentRow[_columns.colOperation]);
            e.actionType = static_cast<Glib::ustring>(parentRow[_columns.colActionType]);
            e.regionOffset = parentRow[_columns.colRegionOffset];
            e.regionLength = parentRow[_columns.colRegionLength];
            e.canvasIdx = parentRow[_columns.colCanvasIdx];
            e.useDay = parentRow[_columns.colUseDay];
            entries.push_back(e);
        }
        // Then the older sub-entries
        for (const auto& childRow : parentRow.children()) {
            CtHistoryEntry e;
            e.nodeId = childRow[_columns.colNodeId];
            e.timestamp = childRow[_columns.colTimestampRaw];
            e.cursorPos = childRow[_columns.colCursorPos];
            e.scrollPos = childRow[_columns.colScrollPos];
            e.actionDescription = static_cast<Glib::ustring>(childRow[_columns.colOperation]);
            e.actionType = static_cast<Glib::ustring>(childRow[_columns.colActionType]);
            e.regionOffset = childRow[_columns.colRegionOffset];
            e.regionLength = childRow[_columns.colRegionLength];
            e.canvasIdx = childRow[_columns.colCanvasIdx];
            e.useDay = childRow[_columns.colUseDay];
            entries.push_back(e);
        }
    }
    return entries;
}

void CtHistoryPanel::load_entries(const std::vector<CtHistoryEntry>& entries)
{
    _updatingList = true;
    _rTreeStore->clear();

    // Prune by use-day if configured
    int currentUseDay = _pCtMainWin->get_ct_config()->localHistoryUseDay;
    int maxUseDays = _pCtMainWin->get_ct_config()->localHistoryMaxUseDays;

    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (maxUseDays > 0 && currentUseDay - it->useDay > maxUseDays) {
            continue;
        }
        auto parentIter = _findOrCreateParent(it->nodeId);
        Glib::ustring tsStr = str::time_format(
            _pCtMainWin->get_ct_config()->timestampFormat, static_cast<time_t>(it->timestamp));

        gint64 prevTimestamp = (*parentIter)[_columns.colTimestampRaw];
        if (prevTimestamp == 0) {
            // First entry for this node — store directly on the parent row
            (*parentIter)[_columns.colOperation]    = Glib::ustring(it->actionDescription);
            (*parentIter)[_columns.colTimestamp]     = tsStr;
            (*parentIter)[_columns.colTimestampRaw] = it->timestamp;
            (*parentIter)[_columns.colCursorPos]    = it->cursorPos;
            (*parentIter)[_columns.colScrollPos]    = it->scrollPos;
            (*parentIter)[_columns.colActionType]   = Glib::ustring(it->actionType);
            (*parentIter)[_columns.colRegionOffset] = it->regionOffset;
            (*parentIter)[_columns.colRegionLength] = it->regionLength;
            (*parentIter)[_columns.colCanvasIdx]    = it->canvasIdx;
            (*parentIter)[_columns.colUseDay]       = it->useDay;
        }
        else {
            // Demote current parent data to a sub-entry, then update parent
            auto prevRow = *_rTreeStore->prepend(parentIter->children());
            prevRow[_columns.colNodeId]       = static_cast<gint64>((*parentIter)[_columns.colNodeId]);
            prevRow[_columns.colCursorPos]    = static_cast<int>((*parentIter)[_columns.colCursorPos]);
            prevRow[_columns.colScrollPos]    = static_cast<int>((*parentIter)[_columns.colScrollPos]);
            prevRow[_columns.colTimestampRaw] = prevTimestamp;
            prevRow[_columns.colOperation]    = static_cast<Glib::ustring>((*parentIter)[_columns.colOperation]);
            prevRow[_columns.colActionType]   = static_cast<Glib::ustring>((*parentIter)[_columns.colActionType]);
            prevRow[_columns.colRegionOffset] = static_cast<int>((*parentIter)[_columns.colRegionOffset]);
            prevRow[_columns.colRegionLength] = static_cast<int>((*parentIter)[_columns.colRegionLength]);
            prevRow[_columns.colCanvasIdx]    = static_cast<int>((*parentIter)[_columns.colCanvasIdx]);
            prevRow[_columns.colUseDay]       = static_cast<int>((*parentIter)[_columns.colUseDay]);
            prevRow[_columns.colIsParent]     = false;
            prevRow[_columns.colTimestamp]    = static_cast<Glib::ustring>((*parentIter)[_columns.colTimestamp]);

            (*parentIter)[_columns.colOperation]    = Glib::ustring(it->actionDescription);
            (*parentIter)[_columns.colTimestamp]     = tsStr;
            (*parentIter)[_columns.colTimestampRaw] = it->timestamp;
            (*parentIter)[_columns.colCursorPos]    = it->cursorPos;
            (*parentIter)[_columns.colScrollPos]    = it->scrollPos;
            (*parentIter)[_columns.colActionType]   = Glib::ustring(it->actionType);
            (*parentIter)[_columns.colRegionOffset] = it->regionOffset;
            (*parentIter)[_columns.colRegionLength] = it->regionLength;
            (*parentIter)[_columns.colCanvasIdx]    = it->canvasIdx;
            (*parentIter)[_columns.colUseDay]       = it->useDay;
        }
    }

    // Sort parent rows by timestamp (newest first) — they were inserted in reverse order
    _rTreeStore->set_sort_column(Gtk::TreeSortable::DEFAULT_UNSORTED_COLUMN_ID, Gtk::SORT_ASCENDING);

    _updatingList = false;
}

void CtHistoryPanel::refresh_names()
{
    for (auto it = _rTreeStore->children().begin(); it != _rTreeStore->children().end(); ) {
        gint64 nodeId = (*it)[_columns.colNodeId];
        CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
        if (ctIter) {
            (*it)[_columns.colName] = ctIter.get_node_name();
            (*it)[_columns.colIcon] = ctIter.get_node_icon();
            ++it;
        }
        else {
            (*it)[_columns.colName] = Glib::ustring("(") + _("deleted") + ")";
            ++it;
        }
    }
}

void CtHistoryPanel::adjustOffsetsForNode(gint64 nodeId, const std::vector<CtCommand::OffsetShift>& shifts)
{
    if (shifts.empty()) return;

    for (auto parentIt = _rTreeStore->children().begin(); parentIt != _rTreeStore->children().end(); ++parentIt) {
        if ((*parentIt)[_columns.colNodeId] != nodeId) continue;

        // Adjust the parent row's offset (it holds the latest entry)
        int parentOffset = (*parentIt)[_columns.colRegionOffset];
        if (parentOffset >= 0) {
            for (const auto& shift : shifts) {
                if (parentOffset >= shift.offset) {
                    parentOffset += shift.delta;
                    parentOffset = std::max(0, parentOffset);
                } else if (shift.delta < 0 && parentOffset > shift.offset + shift.delta) {
                    parentOffset = shift.offset;
                }
            }
            (*parentIt)[_columns.colRegionOffset] = parentOffset;
        }

        // Adjust sub-entries
        for (auto childIt = parentIt->children().begin(); childIt != parentIt->children().end(); ++childIt) {
            int offset = (*childIt)[_columns.colRegionOffset];
            if (offset < 0) continue;

            for (const auto& shift : shifts) {
                if (offset >= shift.offset) {
                    offset += shift.delta;
                    offset = std::max(0, offset);
                } else if (shift.delta < 0 && offset > shift.offset + shift.delta) {
                    offset = shift.offset;
                }
            }
            (*childIt)[_columns.colRegionOffset] = offset;
        }
        break;
    }
}

void CtHistoryPanel::_on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* /*column*/)
{
    if (_updatingList) return;
    auto iter = _rTreeStore->get_iter(path);
    if (!iter) return;

    bool isParent = (*iter)[_columns.colIsParent];

    // If clicking expand arrow on a parent, don't navigate (GTK handles expand/collapse)
    // But row_activated fires on click, not expand arrow specifically.
    // We navigate on click for both parent and child rows.

    CtHistoryEntry entry;
    if (isParent) {
        // Navigate using parent's data (reflects latest action)
        entry.nodeId = (*iter)[_columns.colNodeId];
        entry.cursorPos = (*iter)[_columns.colCursorPos];
        entry.scrollPos = (*iter)[_columns.colScrollPos];
        entry.actionType = static_cast<Glib::ustring>((*iter)[_columns.colActionType]);
        entry.regionOffset = (*iter)[_columns.colRegionOffset];
        entry.regionLength = (*iter)[_columns.colRegionLength];
        entry.canvasIdx = (*iter)[_columns.colCanvasIdx];
    } else {
        entry.nodeId = (*iter)[_columns.colNodeId];
        entry.cursorPos = (*iter)[_columns.colCursorPos];
        entry.scrollPos = (*iter)[_columns.colScrollPos];
        entry.actionType = static_cast<Glib::ustring>((*iter)[_columns.colActionType]);
        entry.regionOffset = (*iter)[_columns.colRegionOffset];
        entry.regionLength = (*iter)[_columns.colRegionLength];
        entry.canvasIdx = (*iter)[_columns.colCanvasIdx];
    }
    _navigate_to_entry(entry);
}

void CtHistoryPanel::_navigate_to_entry(const CtHistoryEntry& entry)
{
    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(entry.nodeId);
    if (!ctIter) {
        spdlog::warn("Local history: node {} not found", entry.nodeId);
        return;
    }

    _pCtMainWin->_navigatingHistory = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; });

    auto store = _pCtMainWin->get_tree_store().get_store();
    _pCtMainWin->get_tree_view().set_cursor_safe(ctIter);
    _pCtMainWin->get_tree_view().scroll_to_row(store->get_path(ctIter), 0.5);

    CtHistoryEntry entryCopy = entry;
    Glib::signal_idle().connect_once([this, entryCopy]() {
        auto rBuffer = _pCtMainWin->curr_buffer();
        if (!rBuffer) return;

        int charCount = rBuffer->get_char_count();
        int cursorPos = std::min(entryCopy.cursorPos, charCount);
        auto iterCursor = rBuffer->get_iter_at_offset(cursorPos);
        rBuffer->place_cursor(iterCursor);

        auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
        if (vAdj) {
            double scrollVal = std::min(static_cast<double>(entryCopy.scrollPos),
                                        vAdj->get_upper() - vAdj->get_page_size());
            vAdj->set_value(scrollVal);
        }

        Glib::signal_idle().connect_once([this, entryCopy]() {
            _flashRegion(entryCopy);
        });
    });
}

void CtHistoryPanel::_flashRegion(const CtHistoryEntry& entry)
{
    _stopFlash();

    auto& textView = _pCtMainWin->get_text_view().mm();
    auto rBuffer = textView.get_buffer();
    if (!rBuffer) return;

    std::string actionType = entry.actionType;
    int regionOffset = entry.regionOffset;
    int regionLength = entry.regionLength;
    int canvasIdx = entry.canvasIdx;

    // Text operations: flash highlight on the affected region
    if ((actionType == "INS" || actionType == "DEL" || actionType == "FMT" ||
         actionType == "RFM" || actionType == "TED") && regionOffset >= 0)
    {
        int charCount = rBuffer->get_char_count();
        int start = std::min(regionOffset, charCount);
        int end;
        if (regionLength > 0) {
            end = std::min(regionOffset + regionLength, charCount);
        } else {
            // Point ops (deletion, paste): highlight a narrow band
            end = std::min(start + 2, charCount);
            start = std::max(0, start - 2);
        }

        // Scroll to make region visible
        auto iterStart = rBuffer->get_iter_at_offset(start);
        textView.scroll_to(iterStart, 0.1);

        // Create highlight tag
        auto* bridge = _pCtMainWin->get_command_bridge();
        bridge->beginReplay();

        auto tag = rBuffer->create_tag();
        auto* config = _pCtMainWin->get_ct_config();
        bool isDark = false;
        if (auto settings = Gtk::Settings::get_default()) {
            isDark = settings->property_gtk_application_prefer_dark_theme();
        }
        tag->property_background() = isDark ? config->flashColorDark : config->flashColorLight;
        tag->property_background_set() = true;

        auto iterEnd = rBuffer->get_iter_at_offset(end);
        rBuffer->apply_tag(tag, iterStart, iterEnd);

        bridge->endReplay();

        _flashState.highlightTag = tag;
        _flashState.regionOffset = start;
        _flashState.regionLength = end - start;
        _flashState.flashCount = 0;

        int pulseCount = config->flashPulseCount;
        int intervalMs = config->flashPulseIntervalMs;
        int holdMs = config->flashHoldMs;

        _flashState.timeout = Glib::signal_timeout().connect([this, pulseCount, intervalMs, holdMs]() -> bool {
            _flashState.flashCount++;
            auto rBuf = _pCtMainWin->curr_buffer();
            if (!rBuf || !_flashState.highlightTag) {
                _stopFlash();
                return false;
            }

            auto* bridge = _pCtMainWin->get_command_bridge();
            bridge->beginReplay();

            int totalTransitions = pulseCount * 2;
            if (_flashState.flashCount <= totalTransitions) {
                if (_flashState.flashCount % 2 == 1) {
                    // OFF
                    rBuf->remove_tag(_flashState.highlightTag, rBuf->begin(), rBuf->end());
                } else {
                    // ON
                    int s = std::min(_flashState.regionOffset, rBuf->get_char_count());
                    int e = std::min(_flashState.regionOffset + _flashState.regionLength, rBuf->get_char_count());
                    rBuf->apply_tag(_flashState.highlightTag,
                                    rBuf->get_iter_at_offset(s),
                                    rBuf->get_iter_at_offset(e));
                }
                bridge->endReplay();
                return true;
            }

            // Final hold phase done — clean up
            rBuf->remove_tag(_flashState.highlightTag, rBuf->begin(), rBuf->end());
            auto tagTable = rBuf->get_tag_table();
            if (tagTable) tagTable->remove(_flashState.highlightTag);
            _flashState.highlightTag.reset();
            bridge->endReplay();
            return false;
        }, intervalMs);
    }
    else if ((actionType == "WIns" || actionType == "WMod" || actionType == "TCel" ||
              actionType == "RCel" || actionType == "CBed") && regionOffset >= 0)
    {
        // Widget operations: find the actual widget and flash its border
        int charCount = rBuffer->get_char_count();
        int clampedOffset = std::min(regionOffset, charCount);
        auto iterAt = rBuffer->get_iter_at_offset(clampedOffset);
        textView.scroll_to(iterAt, 0.1);

        auto* config = _pCtMainWin->get_ct_config();
        bool isDark = false;
        if (auto settings = Gtk::Settings::get_default()) {
            isDark = settings->property_gtk_application_prefer_dark_theme();
        }

        Gtk::Widget* pWidget = nullptr;
        auto treeIter = _pCtMainWin->curr_tree_iter();
        if (treeIter) {
            auto widgets = treeIter.get_anchored_widgets(clampedOffset, clampedOffset + 1);
            if (!widgets.empty()) {
                pWidget = widgets.front();
            }
        }

        if (pWidget) {
            auto* pAnchoredWidget = dynamic_cast<CtAnchoredWidget*>(pWidget);
            if (!pAnchoredWidget) return;

            Glib::ustring flashColor = isDark ? config->flashColorDark : config->flashColorLight;
            pAnchoredWidget->setFlashHighlight(true, flashColor);

            _flashState.flashingWidget = pAnchoredWidget;
            _flashState.flashCount = 0;

            int intervalMs = config->flashPulseIntervalMs;
            int pulseCount = config->flashPulseCount;

            _flashState.timeout = Glib::signal_timeout().connect(
                [this, pAnchoredWidget, flashColor, pulseCount]() -> bool {
                _flashState.flashCount++;
                int totalTransitions = pulseCount * 2;
                if (_flashState.flashCount <= totalTransitions) {
                    pAnchoredWidget->setFlashHighlight(_flashState.flashCount % 2 == 0, flashColor);
                    return true;
                }
                pAnchoredWidget->setFlashHighlight(false);
                _flashState.flashingWidget = nullptr;
                return false;
            }, intervalMs);
        }
        else {
            // Fallback: widget not found (e.g. after undo), flash text region
            auto* bridge = _pCtMainWin->get_command_bridge();
            bridge->beginReplay();

            int start = std::max(0, clampedOffset - 1);
            int end = std::min(clampedOffset + 2, charCount);
            auto tag = rBuffer->create_tag();
            tag->property_background() = isDark ? config->flashColorDark : config->flashColorLight;
            tag->property_background_set() = true;
            rBuffer->apply_tag(tag, rBuffer->get_iter_at_offset(start), rBuffer->get_iter_at_offset(end));
            bridge->endReplay();

            _flashState.highlightTag = tag;
            _flashState.regionOffset = start;
            _flashState.regionLength = end - start;
            _flashState.flashCount = 0;

            int intervalMs = config->flashPulseIntervalMs;
            int pulseCount = config->flashPulseCount;

            _flashState.timeout = Glib::signal_timeout().connect([this, pulseCount, intervalMs]() -> bool {
                _flashState.flashCount++;
                auto rBuf = _pCtMainWin->curr_buffer();
                if (!rBuf || !_flashState.highlightTag) { _stopFlash(); return false; }

                auto* bridge = _pCtMainWin->get_command_bridge();
                bridge->beginReplay();

                int totalTransitions = pulseCount * 2;
                if (_flashState.flashCount <= totalTransitions) {
                    if (_flashState.flashCount % 2 == 1) {
                        rBuf->remove_tag(_flashState.highlightTag, rBuf->begin(), rBuf->end());
                    } else {
                        int s = std::min(_flashState.regionOffset, rBuf->get_char_count());
                        int e = std::min(_flashState.regionOffset + _flashState.regionLength, rBuf->get_char_count());
                        rBuf->apply_tag(_flashState.highlightTag,
                                        rBuf->get_iter_at_offset(s),
                                        rBuf->get_iter_at_offset(e));
                    }
                    bridge->endReplay();
                    return true;
                }
                rBuf->remove_tag(_flashState.highlightTag, rBuf->begin(), rBuf->end());
                auto tagTable = rBuf->get_tag_table();
                if (tagTable) tagTable->remove(_flashState.highlightTag);
                _flashState.highlightTag.reset();
                bridge->endReplay();
                return false;
            }, intervalMs);
        }
    }
    else if ((actionType == "DSt" || actionType == "ESt" || actionType == "RSt" ||
              actionType == "MSt" || actionType == "ACv" || actionType == "DCv" ||
              actionType == "MCv" || actionType == "RCv" || actionType == "PCv") &&
             canvasIdx >= 0)
    {
        _flashCanvas(canvasIdx);
    }
    else if (actionType == "NPr" || actionType == "NAd" || actionType == "NMv")
    {
        _flashTreeRow(entry.nodeId);
    }
}

void CtHistoryPanel::_flashCanvas(int canvasIdx)
{
    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;

    auto* config = _pCtMainWin->get_ct_config();

    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    std::string flashColor = isDark ? config->flashColorDark.raw() : config->flashColorLight.raw();

    overlay->setFlashColor(flashColor);
    overlay->setFlashCanvasIdx(canvasIdx);

    _flashState.flashingCanvas = true;
    _flashState.canvasIdx = canvasIdx;
    _flashState.flashCount = 0;

    int pulseCount = config->flashPulseCount;
    int intervalMs = config->flashPulseIntervalMs;

    _flashState.timeout = Glib::signal_timeout().connect(
        [this, overlay, pulseCount]() -> bool {
            _flashState.flashCount++;
            int totalTransitions = pulseCount * 2;
            if (_flashState.flashCount <= totalTransitions) {
                if (_flashState.flashCount % 2 == 1) {
                    overlay->setFlashCanvasIdx(-1);
                } else {
                    overlay->setFlashCanvasIdx(_flashState.canvasIdx);
                }
                return true;
            }
            overlay->setFlashCanvasIdx(-1);
            _flashState.flashingCanvas = false;
            return false;
        }, intervalMs);
}

void CtHistoryPanel::_flashTreeRow(gint64 nodeId)
{
    auto& treeView = _pCtMainWin->get_tree_view();
    auto* config = _pCtMainWin->get_ct_config();

    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    Glib::ustring flashColor = isDark ? config->flashColorDark : config->flashColorLight;

    auto css = Gtk::CssProvider::create();
    css->load_from_data("treeview.history-flash row:selected { background-color: " +
                        flashColor + "; }");
    treeView.get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    treeView.get_style_context()->add_class("history-flash");

    _flashState.flashingTreeRow = true;
    _flashState.flashCount = 0;

    int pulseCount = config->flashPulseCount;
    int intervalMs = config->flashPulseIntervalMs;

    _flashState.timeout = Glib::signal_timeout().connect(
        [this, pulseCount, css, &treeView]() -> bool {
            _flashState.flashCount++;
            int totalTransitions = pulseCount * 2;
            if (_flashState.flashCount <= totalTransitions) {
                if (_flashState.flashCount % 2 == 1) {
                    treeView.get_style_context()->remove_class("history-flash");
                } else {
                    treeView.get_style_context()->add_class("history-flash");
                }
                return true;
            }
            treeView.get_style_context()->remove_class("history-flash");
            treeView.get_style_context()->remove_provider(css);
            _flashState.flashingTreeRow = false;
            return false;
        }, intervalMs);
}

void CtHistoryPanel::_stopFlash()
{
    if (_flashState.timeout.connected()) {
        _flashState.timeout.disconnect();
    }
    if (_flashState.highlightTag) {
        auto rBuf = _pCtMainWin->curr_buffer();
        if (rBuf) {
            auto* bridge = _pCtMainWin->get_command_bridge();
            bridge->beginReplay();
            rBuf->remove_tag(_flashState.highlightTag, rBuf->begin(), rBuf->end());
            auto tagTable = rBuf->get_tag_table();
            if (tagTable) tagTable->remove(_flashState.highlightTag);
            bridge->endReplay();
        }
        _flashState.highlightTag.reset();
    }
    if (_flashState.flashingCanvas) {
        if (auto* overlay = _pCtMainWin->get_drawing_overlay()) {
            overlay->setFlashCanvasIdx(-1);
        }
    }
    if (_flashState.flashingWidget) {
        _flashState.flashingWidget->setFlashHighlight(false);
        _flashState.flashingWidget = nullptr;
    }
    if (_flashState.flashingTreeRow) {
        _pCtMainWin->get_tree_view().get_style_context()->remove_class("history-flash");
    }
    _flashState = FlashState{};
}
