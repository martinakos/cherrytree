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
#include "ct_delta_engine.h"
#include "ct_storage_control.h"
#include "ct_misc_utils.h"
#include "ct_const.h"
#include "ct_drawing.h"
#include "ct_widgets.h"
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <glibmm/base64.h>

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
        prevRow[_columns.colDeltaData]   = static_cast<std::string>((*parentIter)[_columns.colDeltaData]);
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
    (*parentIter)[_columns.colDeltaData]   = entry.deltaData;

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
    _stopReplay();
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
            e.deltaData = static_cast<std::string>(parentRow[_columns.colDeltaData]);
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
            e.deltaData = static_cast<std::string>(childRow[_columns.colDeltaData]);
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
            (*parentIter)[_columns.colDeltaData]   = it->deltaData;
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
            prevRow[_columns.colDeltaData]   = static_cast<std::string>((*parentIter)[_columns.colDeltaData]);

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
            (*parentIter)[_columns.colDeltaData]   = it->deltaData;
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
    auto readEntryFromRow = [&](const Gtk::TreeModel::iterator& rowIter) {
        entry.nodeId = (*rowIter)[_columns.colNodeId];
        entry.timestamp = (*rowIter)[_columns.colTimestampRaw];
        entry.cursorPos = (*rowIter)[_columns.colCursorPos];
        entry.scrollPos = (*rowIter)[_columns.colScrollPos];
        entry.actionType = static_cast<Glib::ustring>((*rowIter)[_columns.colActionType]);
        entry.regionOffset = (*rowIter)[_columns.colRegionOffset];
        entry.regionLength = (*rowIter)[_columns.colRegionLength];
        entry.canvasIdx = (*rowIter)[_columns.colCanvasIdx];
        entry.deltaData = static_cast<std::string>((*rowIter)[_columns.colDeltaData]);
    };
    readEntryFromRow(iter);
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
        if (rBuffer) {
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
        }

        Glib::signal_idle().connect_once([this, entryCopy]() {
            _replayEntry(entryCopy);
        });
    });
}

void CtHistoryPanel::_replayEntry(const CtHistoryEntry& entry)
{
    _stopReplay();

    std::string actionType = entry.actionType;
    int canvasIdx = entry.canvasIdx;

    if (actionType == "INS" || actionType == "DEL" || actionType == "FMT" ||
        actionType == "RFM" || actionType == "TED" || actionType == "WIns" ||
        actionType == "WMod" || actionType == "TCel" || actionType == "RCel" ||
        actionType == "CBed")
    {
        _replayViaModel(entry);
    }
    else if (actionType == "DSt" || actionType == "ESt" || actionType == "RSt" ||
             actionType == "MSt" || actionType == "ACv" || actionType == "DCv" ||
             actionType == "MCv" || actionType == "RCv" || actionType == "PCv")
    {
        _replayViaDrawingModel(entry);
    }
    else if (actionType == "NPr" || actionType == "NAd" || actionType == "NMv")
    {
        _replayTreeRow(entry.nodeId);
    }
}

void CtHistoryPanel::_replayViaModel(const CtHistoryEntry& entry)
{
    auto* bridge = _pCtMainWin->get_command_bridge();
    auto docModel = bridge->getDocumentModel();
    if (!docModel) return;

    auto node = docModel->getNodeById(entry.nodeId);
    if (!node) return;

    static const std::unordered_set<std::string> kContentModifyingTypes{
        "INS", "DEL", "FMT", "RFM", "TED", "WIns", "WMod", "TCel", "RCel", "CBed"
    };

    // Collect content-modifying entries for this node, newest-first.
    // Skip drawing and node-level entries — they don't produce deltas
    // and would break the delta chain.
    std::vector<CtHistoryEntry> nodeEntries;
    for (const auto& parentRow : _rTreeStore->children()) {
        gint64 parentNodeId = parentRow[_columns.colNodeId];
        if (parentNodeId != entry.nodeId) continue;

        gint64 parentTs = parentRow[_columns.colTimestampRaw];
        if (parentTs > 0) {
            std::string at = static_cast<Glib::ustring>(parentRow[_columns.colActionType]);
            if (kContentModifyingTypes.count(at)) {
                CtHistoryEntry e;
                e.timestamp = parentTs;
                e.deltaData = static_cast<std::string>(parentRow[_columns.colDeltaData]);
                nodeEntries.push_back(e);
            }
        }
        for (const auto& childRow : parentRow.children()) {
            std::string at = static_cast<Glib::ustring>(childRow[_columns.colActionType]);
            if (kContentModifyingTypes.count(at)) {
                CtHistoryEntry e;
                e.timestamp = childRow[_columns.colTimestampRaw];
                e.deltaData = static_cast<std::string>(childRow[_columns.colDeltaData]);
                nodeEntries.push_back(e);
            }
        }
        break;
    }

    // Find the target entry by timestamp
    int targetIdx = -1;
    for (int i = 0; i < static_cast<int>(nodeEntries.size()); ++i) {
        if (nodeEntries[i].timestamp == entry.timestamp) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0) {
        _replayFallbackHighlight(entry);
        return;
    }

    // The target entry itself must have a valid delta
    if (!CtDeltaEngine::isReplayable(nodeEntries[targetIdx].deltaData)) {
        spdlog::info("Local history: target entry has no delta, fallback highlight");
        _replayFallbackHighlight(entry);
        return;
    }

    // Off-screen delta reversal on a copy of the current content.
    // Entries with empty deltas (e.g. oversized widget edits) are skipped —
    // their effect on text content is nil, so the text state is still accurate.
    CtNodeContent workingCopy = node->getContent();

    // Rewind from newest (index 0) to target+1
    for (int i = 0; i < targetIdx; ++i) {
        if (!CtDeltaEngine::isReplayable(nodeEntries[i].deltaData)) {
            spdlog::debug("Local history: skipping non-replayable entry at index {}", i);
            continue;
        }
        if (!CtDeltaEngine::applyReverse(workingCopy, nodeEntries[i].deltaData)) {
            spdlog::warn("Local history: applyReverse failed at index {}", i);
            _replayFallbackHighlight(entry);
            return;
        }
    }
    CtNodeContent afterState = workingCopy;

    // Apply reverse of the target entry to get beforeState
    if (!CtDeltaEngine::applyReverse(workingCopy, nodeEntries[targetIdx].deltaData)) {
        spdlog::warn("Local history: applyReverse failed for target entry");
        _replayFallbackHighlight(entry);
        return;
    }
    CtNodeContent beforeState = workingCopy;

    // Start visual replay
    _replayState.restoreContent = node->getContent();
    _replayState.beforeContent = std::move(beforeState);
    _replayState.afterContent = std::move(afterState);
    _replayState.replayNodeId = entry.nodeId;
    _replayState.cycleCount = 0;
    _replayState.maxCycles = _pCtMainWin->get_ct_config()->localHistoryReplayCycles;
    _replayState.showingBefore = true;
    _replayState.cursorPos = entry.cursorPos;
    _replayState.scrollPos = entry.scrollPos;

    bridge->endTextEditSession();
    bridge->beginReplay();

    // Show "before" state first
    bridge->setPendingCursorPos(_replayState.cursorPos);
    docModel->setNodeContent(entry.nodeId, _replayState.beforeContent, true);

    constexpr int kCycleMs = 700;
    _replayState.timeout = Glib::signal_timeout().connect([this]() -> bool {
        auto* br = _pCtMainWin->get_command_bridge();
        auto dm = br->getDocumentModel();
        if (!dm || !dm->getNodeById(_replayState.replayNodeId)) {
            _stopReplay();
            return false;
        }

        if (_replayState.showingBefore) {
            // Switch to "after"
            br->setPendingCursorPos(_replayState.cursorPos);
            dm->setNodeContent(_replayState.replayNodeId, _replayState.afterContent, true);
            _replayState.showingBefore = false;
        } else {
            // Switch to "before" and increment cycle
            _replayState.cycleCount++;
            if (_replayState.cycleCount >= _replayState.maxCycles) {
                // Done cycling — restore original
                br->setPendingCursorPos(_replayState.cursorPos);
                dm->setNodeContent(_replayState.replayNodeId, _replayState.restoreContent, true);
                br->endReplay();
                _replayState.replayNodeId = -1;
                _replayState.timeout.disconnect();
                return false;
            }
            br->setPendingCursorPos(_replayState.cursorPos);
            dm->setNodeContent(_replayState.replayNodeId, _replayState.beforeContent, true);
            _replayState.showingBefore = true;
        }
        return true;
    }, kCycleMs);
}

void CtHistoryPanel::_replayViaDrawingModel(const CtHistoryEntry& entry)
{
    auto* bridge = _pCtMainWin->get_command_bridge();
    auto docModel = bridge->getDocumentModel();
    if (!docModel) return;

    auto node = docModel->getNodeById(entry.nodeId);
    if (!node) return;

    static const std::unordered_set<std::string> kDrawingTypes{
        "DSt", "ESt", "RSt", "MSt", "ACv", "DCv", "MCv", "RCv", "PCv"
    };

    std::vector<CtHistoryEntry> drawingEntries;
    for (const auto& parentRow : _rTreeStore->children()) {
        gint64 parentNodeId = parentRow[_columns.colNodeId];
        if (parentNodeId != entry.nodeId) continue;

        gint64 parentTs = parentRow[_columns.colTimestampRaw];
        if (parentTs > 0) {
            std::string at = static_cast<Glib::ustring>(parentRow[_columns.colActionType]);
            if (kDrawingTypes.count(at)) {
                CtHistoryEntry e;
                e.timestamp = parentTs;
                e.deltaData = static_cast<std::string>(parentRow[_columns.colDeltaData]);
                drawingEntries.push_back(e);
            }
        }
        for (const auto& childRow : parentRow.children()) {
            std::string at = static_cast<Glib::ustring>(childRow[_columns.colActionType]);
            if (kDrawingTypes.count(at)) {
                CtHistoryEntry e;
                e.timestamp = childRow[_columns.colTimestampRaw];
                e.deltaData = static_cast<std::string>(childRow[_columns.colDeltaData]);
                drawingEntries.push_back(e);
            }
        }
        break;
    }

    int targetIdx = -1;
    for (int i = 0; i < static_cast<int>(drawingEntries.size()); ++i) {
        if (drawingEntries[i].timestamp == entry.timestamp) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0) {
        if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
        return;
    }

    if (!CtDeltaEngine::isDrawingDelta(drawingEntries[targetIdx].deltaData)) {
        spdlog::info("Local history: target drawing entry has no delta, fallback canvas highlight");
        if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
        return;
    }

    auto canvasesCopy = node->getDrawingCanvases();

    for (int i = 0; i < targetIdx; ++i) {
        if (!CtDeltaEngine::isDrawingDelta(drawingEntries[i].deltaData)) continue;
        if (!CtDeltaEngine::applyReverseDrawing(canvasesCopy, drawingEntries[i].deltaData)) {
            spdlog::warn("Local history: applyReverseDrawing failed at index {}", i);
            if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
            return;
        }
    }
    auto afterCanvases = canvasesCopy;

    if (!CtDeltaEngine::applyReverseDrawing(canvasesCopy, drawingEntries[targetIdx].deltaData)) {
        spdlog::warn("Local history: applyReverseDrawing failed for target drawing entry");
        if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
        return;
    }
    auto beforeCanvases = canvasesCopy;

    _replayState.restoreCanvases = node->getDrawingCanvases();
    _replayState.beforeCanvases = std::move(beforeCanvases);
    _replayState.afterCanvases = std::move(afterCanvases);
    _replayState.replayNodeId = entry.nodeId;
    _replayState.replayingDrawing = true;
    _replayState.cycleCount = 0;
    _replayState.maxCycles = _pCtMainWin->get_ct_config()->localHistoryReplayCycles;
    _replayState.showingBefore = true;

    bridge->beginReplay();

    node->getDrawingCanvasesMut() = _replayState.beforeCanvases;
    docModel->notifyNodeDrawingChanged(entry.nodeId);

    constexpr int kCycleMs = 700;
    _replayState.timeout = Glib::signal_timeout().connect([this]() -> bool {
        auto* br = _pCtMainWin->get_command_bridge();
        auto dm = br->getDocumentModel();
        if (!dm) { _stopReplay(); return false; }
        auto nd = dm->getNodeById(_replayState.replayNodeId);
        if (!nd) { _stopReplay(); return false; }

        if (_replayState.showingBefore) {
            nd->getDrawingCanvasesMut() = _replayState.afterCanvases;
            dm->notifyNodeDrawingChanged(_replayState.replayNodeId);
            _replayState.showingBefore = false;
        } else {
            _replayState.cycleCount++;
            if (_replayState.cycleCount >= _replayState.maxCycles) {
                nd->getDrawingCanvasesMut() = _replayState.restoreCanvases;
                dm->notifyNodeDrawingChanged(_replayState.replayNodeId);
                br->endReplay();
                _replayState.replayNodeId = -1;
                _replayState.replayingDrawing = false;
                _replayState.timeout.disconnect();
                return false;
            }
            nd->getDrawingCanvasesMut() = _replayState.beforeCanvases;
            dm->notifyNodeDrawingChanged(_replayState.replayNodeId);
            _replayState.showingBefore = true;
        }
        return true;
    }, kCycleMs);
}

void CtHistoryPanel::_replayFallbackHighlight(const CtHistoryEntry& entry)
{
    auto rBuffer = _pCtMainWin->curr_buffer();
    if (!rBuffer) return;
    if (entry.regionLength <= 0) return;

    int charCount = rBuffer->get_char_count();
    int start = std::min(entry.regionOffset, charCount);
    int end = std::min(entry.regionOffset + entry.regionLength, charCount);
    if (start >= end) return;

    auto tag = rBuffer->create_tag();
    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    tag->property_background() = isDark ? "#554400" : "#FFFFAA";

    auto iterStart = rBuffer->get_iter_at_offset(start);
    auto iterEnd = rBuffer->get_iter_at_offset(end);
    rBuffer->apply_tag(tag, iterStart, iterEnd);
    rBuffer->place_cursor(iterStart);
    _pCtMainWin->get_text_view().mm().scroll_to(rBuffer->get_insert());

    _replayState.highlightTag = tag;
    constexpr int kHoldMs = 2000;
    _replayState.timeout = Glib::signal_timeout().connect(
        [this, tag, rBuffer]() -> bool {
            rBuffer->get_tag_table()->remove(tag);
            _replayState.highlightTag.reset();
            return false;
        }, kHoldMs);
}

void CtHistoryPanel::_replayCanvas(int canvasIdx)
{
    constexpr int kHoldMs = 2000;
    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;

    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    std::string highlightColor = isDark ? "#554400" : "#FFFFAA";

    overlay->setFlashColor(highlightColor);
    overlay->setFlashCanvasIdx(canvasIdx);

    _replayState.replayingCanvas = true;
    _replayState.canvasIdx = canvasIdx;

    _replayState.timeout = Glib::signal_timeout().connect(
        [this, overlay]() -> bool {
            overlay->setFlashCanvasIdx(-1);
            _replayState.replayingCanvas = false;
            return false;
        }, kHoldMs);
}

void CtHistoryPanel::_replayTreeRow(gint64 /*nodeId*/)
{
    constexpr int kHoldMs = 2000;
    auto& treeView = _pCtMainWin->get_tree_view();

    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    Glib::ustring highlightColor = isDark ? "#554400" : "#FFFFAA";

    auto css = Gtk::CssProvider::create();
    css->load_from_data("treeview.history-flash row:selected { background-color: " +
                        highlightColor + "; }");
    treeView.get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    treeView.get_style_context()->add_class("history-flash");

    _replayState.replayingTreeRow = true;

    _replayState.timeout = Glib::signal_timeout().connect(
        [this, css, &treeView]() -> bool {
            treeView.get_style_context()->remove_class("history-flash");
            treeView.get_style_context()->remove_provider(css);
            _replayState.replayingTreeRow = false;
            return false;
        }, kHoldMs);
}

void CtHistoryPanel::_stopReplay()
{
    if (_replayState.timeout.connected()) {
        _replayState.timeout.disconnect();
    }
    if (_replayState.replayNodeId >= 0) {
        auto* bridge = _pCtMainWin->get_command_bridge();
        auto docModel = bridge->getDocumentModel();
        if (docModel) {
            auto nd = docModel->getNodeById(_replayState.replayNodeId);
            if (nd) {
                if (_replayState.replayingDrawing) {
                    nd->getDrawingCanvasesMut() = _replayState.restoreCanvases;
                    docModel->notifyNodeDrawingChanged(_replayState.replayNodeId);
                } else {
                    bridge->setPendingCursorPos(_replayState.cursorPos);
                    docModel->setNodeContent(_replayState.replayNodeId, _replayState.restoreContent, true);
                }
            }
        }
        bridge->endReplay();
    }
    if (_replayState.replayingCanvas) {
        if (auto* overlay = _pCtMainWin->get_drawing_overlay()) {
            overlay->setFlashCanvasIdx(-1);
        }
    }
    if (_replayState.replayingTreeRow) {
        _pCtMainWin->get_tree_view().get_style_context()->remove_class("history-flash");
    }
    if (_replayState.highlightTag) {
        auto rBuffer = _pCtMainWin->curr_buffer();
        if (rBuffer) {
            rBuffer->get_tag_table()->remove(_replayState.highlightTag);
        }
        _replayState.highlightTag.reset();
    }
    _replayState = ReplayState{};
}
