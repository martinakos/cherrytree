/*
 * ct_timeline_panel.cc
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

#include "ct_timeline_panel.h"
#include "ct_main_win.h"
#include "ct_command_bridge.h"
#include "ct_delta_engine.h"
#include "ct_storage_control.h"
#include "ct_misc_utils.h"
#include "ct_const.h"
#include "ct_drawing.h"
#include "ct_widgets.h"
#include <algorithm>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <glibmm/base64.h>

CtTimelinePanel::CtTimelinePanel(CtMainWin* pCtMainWin)
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
        sigc::mem_fun(*this, &CtTimelinePanel::_on_row_activated));

    _scrolledWindow.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
#if GTKMM_MAJOR_VERSION >= 4
    _scrolledWindow.set_child(_treeView);
#else
    _scrolledWindow.add(_treeView);
#endif
    _scrolledWindow.set_size_request(150, -1);
}

void CtTimelinePanel::_set_row(const Gtk::TreeModel::Row& row, const CtTimelineEntry& entry)
{
    row[_columns.colNodeId]       = entry.nodeId;
    row[_columns.colOperation]    = Glib::ustring(entry.actionDescription);
    row[_columns.colTimestamp]    = str::time_format(_pCtMainWin->get_ct_config()->timestampFormat,
                                                     static_cast<time_t>(entry.timestamp));
    row[_columns.colTimestampRaw] = entry.timestamp;
    row[_columns.colCursorPos]    = entry.cursorPos;
    row[_columns.colScrollPos]    = entry.scrollPos;
    row[_columns.colActionType]   = Glib::ustring(entry.actionType);
    row[_columns.colRegionOffset] = entry.regionOffset;
    row[_columns.colRegionLength] = entry.regionLength;
    row[_columns.colCanvasIdx]    = entry.canvasIdx;
    row[_columns.colUseDay]       = entry.useDay;
    row[_columns.colDeltaData]    = entry.deltaData;

    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(entry.nodeId);
    if (ctIter) {
        row[_columns.colName] = ctIter.get_node_name();
        row[_columns.colIcon] = ctIter.get_node_icon();
    }
    else {
        row[_columns.colName] = Glib::ustring("(") + _("deleted") + ")";
    }
}

CtTimelineEntry CtTimelinePanel::_get_row(const Gtk::TreeModel::Row& row) const
{
    CtTimelineEntry e;
    e.nodeId = row[_columns.colNodeId];
    e.timestamp = row[_columns.colTimestampRaw];
    e.cursorPos = row[_columns.colCursorPos];
    e.scrollPos = row[_columns.colScrollPos];
    e.actionDescription = static_cast<Glib::ustring>(row[_columns.colOperation]);
    e.actionType = static_cast<Glib::ustring>(row[_columns.colActionType]);
    e.regionOffset = row[_columns.colRegionOffset];
    e.regionLength = row[_columns.colRegionLength];
    e.canvasIdx = row[_columns.colCanvasIdx];
    e.useDay = row[_columns.colUseDay];
    e.deltaData = static_cast<std::string>(row[_columns.colDeltaData]);
    return e;
}

void CtTimelinePanel::add_entry(const CtTimelineEntry& entry)
{
    _updatingList = true;
    auto on_scope_exit = scope_guard([this](void*) { _updatingList = false; });

    // newest entry on top; a node gets a row for each of its changes
    _set_row(*_rTreeStore->prepend(), entry);

    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_timeline();
    }
}

void CtTimelinePanel::updateCursorScroll(gint64 nodeId, int cursorPos, int scrollPos)
{
    // the newest entry of the node is the one navigation returns to
    for (const auto& row : _rTreeStore->children()) {
        if (row[_columns.colNodeId] == nodeId) {
            row[_columns.colCursorPos] = cursorPos;
            row[_columns.colScrollPos] = scrollPos;
            break;
        }
    }
}

void CtTimelinePanel::remove_entries_for_node(gint64 nodeId)
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
            pStorage->pending_edit_db_timeline();
        }
    }
}

void CtTimelinePanel::clear()
{
    _updatingList = true;
    _stopReplay();
    _rTreeStore->clear();
    _updatingList = false;
    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_timeline();
    }
}

std::vector<CtTimelineEntry> CtTimelinePanel::get_all_entries() const
{
    std::vector<CtTimelineEntry> entries;
    for (const auto& row : _rTreeStore->children()) {
        entries.push_back(_get_row(row));
    }
    return entries;
}

void CtTimelinePanel::load_entries(const std::vector<CtTimelineEntry>& entries)
{
    _updatingList = true;
    _rTreeStore->clear();

    // Prune by use-day if configured
    int currentUseDay = _pCtMainWin->get_ct_config()->timelineUseDay;
    int maxUseDays = _pCtMainWin->get_ct_config()->timelineMaxUseDays;

    // newest first; files written when entries were grouped by node are not in time order
    std::vector<CtTimelineEntry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(), [](const CtTimelineEntry& a, const CtTimelineEntry& b) {
        return a.timestamp > b.timestamp;
    });
    for (const auto& entry : sorted) {
        if (maxUseDays > 0 && currentUseDay - entry.useDay > maxUseDays) {
            continue;
        }
        _set_row(*_rTreeStore->append(), entry);
    }

    _updatingList = false;
}

void CtTimelinePanel::refresh_names()
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

void CtTimelinePanel::adjustOffsetsForNode(gint64 nodeId, const std::vector<CtCommand::OffsetShift>& shifts)
{
    if (shifts.empty()) return;

    for (const auto& row : _rTreeStore->children()) {
        if (row[_columns.colNodeId] != nodeId) continue;

        int offset = row[_columns.colRegionOffset];
        if (offset < 0) continue;

        for (const auto& shift : shifts) {
            if (offset >= shift.offset) {
                offset += shift.delta;
                offset = std::max(0, offset);
            } else if (shift.delta < 0 && offset > shift.offset + shift.delta) {
                offset = shift.offset;
            }
        }
        row[_columns.colRegionOffset] = offset;
    }
}

void CtTimelinePanel::_on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* /*column*/)
{
    if (_updatingList or _centeringView) return;
    auto iter = _rTreeStore->get_iter(path);
    if (!iter) return;

    const CtTimelineEntry entry = _get_row(*iter);
    // A running replay is stopped once the node switch is done (_replayEntry): rebuilding
    // the buffer still on display right before switching the view away hangs GTK. Leaving
    // that buffer flagged modified by the replay would mark the document as needing a save.
    if (_replayState.displayBuffer) {
        _replayState.displayBuffer->set_modified(_replayState.displayBufferWasModified);
    }
    _navigate_to_entry(entry);
}

void CtTimelinePanel::_navigate_to_entry(const CtTimelineEntry& entry)
{
    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(entry.nodeId);
    if (!ctIter) {
        spdlog::warn("Timeline: node {} not found", entry.nodeId);
        return;
    }

    if (entry.regionOffset >= 0 or entry.canvasIdx >= 0) {
        _freeze_text_view();
    }

    _pCtMainWin->_navigatingHistory = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; });

    auto store = _pCtMainWin->get_tree_store().get_store();
    _pCtMainWin->get_tree_view().set_cursor_safe(ctIter);
    _pCtMainWin->get_tree_view().scroll_to_row(store->get_path(ctIter), 0.5);

    CtTimelineEntry entryCopy = entry;
    Glib::signal_idle().connect_once([this, entryCopy]() {
        auto rBuffer = _pCtMainWin->curr_buffer();
        if (rBuffer) {
            // the replay centres the view on the change; entries without a place in the
            // node go back to where the cursor and scroll were
            const bool hasPlace = entryCopy.regionOffset >= 0 or entryCopy.canvasIdx >= 0;
            int charCount = rBuffer->get_char_count();
            int cursorPos = std::min(entryCopy.regionOffset >= 0 ? entryCopy.regionOffset : entryCopy.cursorPos, charCount);
            auto iterCursor = rBuffer->get_iter_at_offset(cursorPos);
            rBuffer->place_cursor(iterCursor);

            auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
            if (vAdj and not hasPlace) {
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

void CtTimelinePanel::_replayEntry(const CtTimelineEntry& entry)
{
    // the view is in place once the replay has started
    auto on_scope_exit = scope_guard([this](void*) { _thaw_text_view(); });
    _stopReplay();

    std::string actionType = entry.actionType;

    if (actionType == "INS" || actionType == "DEL" || actionType == "FMT" ||
        actionType == "RFM" || actionType == "TED" || actionType == "WIns" ||
        actionType == "WMod" || actionType == "TCel" || actionType == "RCel" ||
        actionType == "CBed")
    {
        _replayViaModel(entry);
    }
    else if (actionType == "DSt" || actionType == "ESt" || actionType == "RSt" ||
             actionType == "MSt" || actionType == "SPr" || actionType == "ACv" || actionType == "DCv" ||
             actionType == "MCv" || actionType == "RCv" || actionType == "PCv")
    {
        _replayViaDrawingModel(entry);
    }
    else if (actionType == "NPr" || actionType == "NAd" || actionType == "NMv")
    {
        _replayTreeRow(entry.nodeId);
    }
}

std::vector<CtTimelineEntry> CtTimelinePanel::_collect_node_entries(gint64 nodeId,
                                                                  const std::unordered_set<std::string>& actionTypes) const
{
    std::vector<CtTimelineEntry> entries;
    for (const auto& row : _rTreeStore->children()) {
        if (row[_columns.colNodeId] != nodeId) continue;
        const std::string actionType = static_cast<Glib::ustring>(row[_columns.colActionType]);
        if (actionTypes.count(actionType)) {
            entries.push_back(_get_row(row));
        }
    }
    return entries;
}

/*static*/int CtTimelinePanel::_find_entry(const std::vector<CtTimelineEntry>& entries, const CtTimelineEntry& entry)
{
    // timestamps have one second resolution, so a node can have several entries with the same one
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[i].timestamp == entry.timestamp and entries[i].actionType == entry.actionType and
            entries[i].deltaData == entry.deltaData)
        {
            return i;
        }
    }
    return -1;
}

void CtTimelinePanel::_replayViaModel(const CtTimelineEntry& entry)
{
    auto* bridge = _pCtMainWin->get_command_bridge();
    auto docModel = bridge->getDocumentModel();
    if (!docModel) return;

    auto node = docModel->getNodeById(entry.nodeId);
    if (!node) return;

    static const std::unordered_set<std::string> kContentModifyingTypes{
        "INS", "DEL", "FMT", "RFM", "TED", "WIns", "WMod", "TCel", "RCel", "CBed"
    };

    // Content-modifying entries for this node, newest-first. Drawing and
    // node-level entries don't produce content deltas and would break the chain.
    const std::vector<CtTimelineEntry> nodeEntries = _collect_node_entries(entry.nodeId, kContentModifyingTypes);
    const int targetIdx = _find_entry(nodeEntries, entry);
    if (targetIdx < 0) {
        _replayFallbackHighlight(entry);
        return;
    }

    // The target entry itself must have a valid delta
    if (!CtDeltaEngine::isReplayable(nodeEntries[targetIdx].deltaData)) {
        spdlog::info("Timeline: target entry has no delta, fallback highlight");
        _replayFallbackHighlight(entry);
        return;
    }

    // Start from what the buffer shows, not from the model: the model of a node not
    // yet synced in this session can be empty, and restoring that would blank the node
    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(entry.nodeId);
    auto nodeBuffer = ctIter ? ctIter.get_node_text_buffer() : Glib::RefPtr<Gtk::TextBuffer>{};
    if (!nodeBuffer) return;
    const CtNodeContent currentContent = buildContentFromBuffer(nodeBuffer, ctIter.get_anchored_widgets());

    // Off-screen delta reversal on a copy of the current content.
    // Entries with empty deltas (e.g. oversized widget edits) are skipped —
    // their effect on text content is nil, so the text state is still accurate.
    CtNodeContent workingCopy = currentContent;

    // Rewind from newest (index 0) to target+1
    for (int i = 0; i < targetIdx; ++i) {
        if (!CtDeltaEngine::isReplayable(nodeEntries[i].deltaData)) {
            spdlog::debug("Timeline: skipping non-replayable entry at index {}", i);
            continue;
        }
        if (!CtDeltaEngine::applyReverse(workingCopy, nodeEntries[i].deltaData)) {
            spdlog::warn("Timeline: applyReverse failed at index {}", i);
            _replayFallbackHighlight(entry);
            return;
        }
    }
    CtNodeContent afterState = workingCopy;

    // Apply reverse of the target entry to get beforeState
    if (!CtDeltaEngine::applyReverse(workingCopy, nodeEntries[targetIdx].deltaData)) {
        spdlog::warn("Timeline: applyReverse failed for target entry");
        _replayFallbackHighlight(entry);
        return;
    }
    CtNodeContent beforeState = workingCopy;

    // Start visual replay
    _replayState.restoreContent = currentContent;
    _replayState.displayBuffer = nodeBuffer;
    _replayState.displayBufferWasModified = nodeBuffer->get_modified();
    _replayState.beforeContent = std::move(beforeState);
    _replayState.afterContent = std::move(afterState);
    _replayState.replayNodeId = entry.nodeId;
    _replayState.cycleCount = 0;
    _replayState.maxCycles = _pCtMainWin->get_ct_config()->timelineReplayCycles;
    _replayState.showingBefore = true;
    _replayState.cursorPos = entry.cursorPos;
    // centre the view on the change and keep it there while the buffer is rebuilt on every cycle
    _replayState.scrollPos = _center_view_on_offset(entry.regionOffset, entry.regionLength);

    bridge->endTextEditSession();
    bridge->beginReplay();

    // Lay out both states, ending on "before" which shows first: a state shorter than
    // the displayed one cannot scroll as far (a rebuilt state can differ from what is
    // displayed too), so use a position both allow and the view does not move on every
    // cycle. The view is frozen meanwhile.
    for (const CtNodeContent* pState : {&_replayState.afterContent, &_replayState.beforeContent}) {
        _show_replay_content(*pState, pState == &_replayState.afterContent ? &_replayState.beforeContent : &_replayState.afterContent);
        if (auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment()) {
            const double maxValue = std::max(vAdj->get_lower(), vAdj->get_upper() - vAdj->get_page_size());
            _replayState.scrollPos = std::min(_replayState.scrollPos, maxValue);
        }
    }
    if (auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment()) {
        vAdj->set_value(_replayState.scrollPos);
    }

    constexpr int kCycleMs = 700;
    _replayState.timeout = Glib::signal_timeout().connect([this]() -> bool {
        auto* br = _pCtMainWin->get_command_bridge();
        auto dm = br->getDocumentModel();
        CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
        if (!dm || !dm->getNodeById(_replayState.replayNodeId) ||
            !currIter || currIter.get_node_id() != _replayState.replayNodeId) {
            _stopReplay();
            return false;
        }

        // the state just shown ended up shorter than measured: keep the position it
        // allowed for the rest of the replay
        auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
        if (vAdj and vAdj->get_value() < _replayState.scrollPos) {
            _replayState.scrollPos = vAdj->get_value();
        }

        if (_replayState.showingBefore) {
            // Switch to "after"
            _show_replay_content(_replayState.afterContent, &_replayState.beforeContent);
            _replayState.showingBefore = false;
        } else {
            // Switch to "before" and increment cycle
            _replayState.cycleCount++;
            if (_replayState.cycleCount >= _replayState.maxCycles) {
                // Done cycling — restore original
                _show_replay_content(_replayState.restoreContent, nullptr);
                br->endReplay();
                _restoreBufferModified();
                _restartEditSession(_replayState.replayNodeId);
                _replayState.replayNodeId = -1;
                _replayState.timeout.disconnect();
                return false;
            }
            _show_replay_content(_replayState.beforeContent, &_replayState.afterContent);
            _replayState.showingBefore = true;
        }
        return true;
    }, kCycleMs);
}

void CtTimelinePanel::_replayViaDrawingModel(const CtTimelineEntry& entry)
{
    auto* bridge = _pCtMainWin->get_command_bridge();
    auto docModel = bridge->getDocumentModel();
    if (!docModel) return;

    auto node = docModel->getNodeById(entry.nodeId);
    if (!node) return;

    static const std::unordered_set<std::string> kDrawingTypes{
        "DSt", "ESt", "RSt", "MSt", "SPr", "ACv", "DCv", "MCv", "RCv", "PCv"
    };

    const std::vector<CtTimelineEntry> drawingEntries = _collect_node_entries(entry.nodeId, kDrawingTypes);
    const int targetIdx = _find_entry(drawingEntries, entry);
    if (targetIdx < 0) {
        if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
        return;
    }

    if (!CtDeltaEngine::isDrawingDelta(drawingEntries[targetIdx].deltaData)) {
        spdlog::info("Timeline: target drawing entry has no delta, fallback canvas highlight");
        if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
        return;
    }

    auto canvasesCopy = node->getDrawingCanvases();

    for (int i = 0; i < targetIdx; ++i) {
        if (!CtDeltaEngine::isDrawingDelta(drawingEntries[i].deltaData)) continue;
        if (!CtDeltaEngine::applyReverseDrawing(canvasesCopy, drawingEntries[i].deltaData)) {
            spdlog::warn("Timeline: applyReverseDrawing failed at index {}", i);
            if (entry.canvasIdx >= 0) _replayCanvas(entry.canvasIdx);
            return;
        }
    }
    auto afterCanvases = canvasesCopy;

    if (!CtDeltaEngine::applyReverseDrawing(canvasesCopy, drawingEntries[targetIdx].deltaData)) {
        spdlog::warn("Timeline: applyReverseDrawing failed for target drawing entry");
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
    _replayState.maxCycles = _pCtMainWin->get_ct_config()->timelineReplayCycles;
    _replayState.showingBefore = true;

    // the canvas where it is before and after the change (a deleted or added canvas only has one)
    const size_t canvasIdx = static_cast<size_t>(std::max(entry.canvasIdx, 0));
    const CtDrawingCanvas* pAfter = canvasIdx < _replayState.afterCanvases.size() ? &_replayState.afterCanvases[canvasIdx] : nullptr;
    const CtDrawingCanvas* pBefore = canvasIdx < _replayState.beforeCanvases.size() ? &_replayState.beforeCanvases[canvasIdx] : nullptr;
    if (pAfter) {
        _center_view_on_canvas(*pAfter, pBefore);
    }
    else if (pBefore) {
        _center_view_on_canvas(*pBefore);
    }

    bridge->beginReplay();

    _replayState.canvasIdx = entry.canvasIdx;
    node->getDrawingCanvasesMut() = _replayState.beforeCanvases;
    docModel->notifyNodeDrawingChanged(entry.nodeId);
    _highlight_canvas_change(_replayState.beforeCanvases, _replayState.afterCanvases, _replayState.canvasIdx);

    constexpr int kCycleMs = 700;
    _replayState.timeout = Glib::signal_timeout().connect([this]() -> bool {
        auto* br = _pCtMainWin->get_command_bridge();
        auto dm = br->getDocumentModel();
        if (!dm) { _stopReplay(); return false; }
        auto nd = dm->getNodeById(_replayState.replayNodeId);
        if (!nd) { _stopReplay(); return false; }
        CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
        if (!currIter || currIter.get_node_id() != _replayState.replayNodeId) { _stopReplay(); return false; }

        if (_replayState.showingBefore) {
            nd->getDrawingCanvasesMut() = _replayState.afterCanvases;
            dm->notifyNodeDrawingChanged(_replayState.replayNodeId);
            _highlight_canvas_change(_replayState.afterCanvases, _replayState.beforeCanvases, _replayState.canvasIdx);
            _replayState.showingBefore = false;
        } else {
            _replayState.cycleCount++;
            if (_replayState.cycleCount >= _replayState.maxCycles) {
                nd->getDrawingCanvasesMut() = _replayState.restoreCanvases;
                dm->notifyNodeDrawingChanged(_replayState.replayNodeId);
                _clear_highlight();
                br->endReplay();
                _replayState.replayNodeId = -1;
                _replayState.replayingDrawing = false;
                _replayState.timeout.disconnect();
                return false;
            }
            nd->getDrawingCanvasesMut() = _replayState.beforeCanvases;
            dm->notifyNodeDrawingChanged(_replayState.replayNodeId);
            _highlight_canvas_change(_replayState.beforeCanvases, _replayState.afterCanvases, _replayState.canvasIdx);
            _replayState.showingBefore = true;
        }
        return true;
    }, kCycleMs);
}

void CtTimelinePanel::_replayFallbackHighlight(const CtTimelineEntry& entry)
{
    auto rBuffer = _pCtMainWin->curr_buffer();
    if (!rBuffer) return;
    if (entry.regionLength <= 0) return;

    int charCount = rBuffer->get_char_count();
    int start = std::min(entry.regionOffset, charCount);
    int end = std::min(entry.regionOffset + entry.regionLength, charCount);
    if (start >= end) return;

    rBuffer->place_cursor(rBuffer->get_iter_at_offset(start));
    _center_view_on_offset(start, end - start);
    _settle_layout();
    _highlight_page_rects(_text_range_page_rects(_pCtMainWin->get_text_view().mm(), start, end));

    constexpr int kHoldMs = 2000;
    _replayState.timeout = Glib::signal_timeout().connect(
        [this]() -> bool {
            _clear_highlight();
            return false;
        }, kHoldMs);
}

double CtTimelinePanel::_scroll_to_span(double top, double height)
{
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    if (not vAdj) return -1.0;
    const double pageSize = vAdj->get_page_size();
    const double margin = pageSize / 10.0;
    // already in view, away from the edges: no need to scroll
    if (height <= pageSize - 2 * margin and top >= vAdj->get_value() + margin and
        top + height <= vAdj->get_value() + pageSize - margin)
    {
        return vAdj->get_value();
    }
    // a span taller than the view starts a little below the top edge
    double target = height < pageSize ? top + height / 2.0 - pageSize / 2.0 : top - margin;
    target = std::max(vAdj->get_lower(), std::min(target, vAdj->get_upper() - pageSize));
    vAdj->set_value(target);
    return vAdj->get_value();
}

double CtTimelinePanel::_center_view_on_offset(int offset, int length)
{
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    if (not vAdj) return -1.0;
    if (offset < 0) return vAdj->get_value();

    _centeringView = true;
    auto on_scope_exit = scope_guard([this](void*) { _centeringView = false; });

    Gtk::TextView& textView = _pCtMainWin->get_text_view().mm();
    auto rBuffer = textView.get_buffer();
    // Lines not laid out yet have estimated heights: measure again once scrolling
    // there has made GTK lay them out, until the position settles
    double value = vAdj->get_value();
    for (int pass = 0; pass < 4; ++pass) {
        const int charCount = rBuffer->get_char_count();
        Gdk::Rectangle rectStart, rectEnd;
        textView.get_iter_location(rBuffer->get_iter_at_offset(std::min(offset, charCount)), rectStart);
        textView.get_iter_location(rBuffer->get_iter_at_offset(std::min(offset + std::max(length, 0), charCount)), rectEnd);
        const double top = rectStart.get_y();
        const double bottom = std::max(rectStart.get_y() + rectStart.get_height(), rectEnd.get_y() + rectEnd.get_height());
        const double previous = vAdj->get_value();
        value = _scroll_to_span(top, bottom - top);
        if (pass > 0 and std::abs(value - previous) < 1.0) break;
        _settle_layout();
    }
    return value;
}

double CtTimelinePanel::_center_view_on_canvas(const CtDrawingCanvas& canvas, const CtDrawingCanvas* pOtherCanvas)
{
    const double zoom = _pCtMainWin->get_rt_zoom_scale_factor();
    double top = canvas.y;
    double bottom = canvas.y + canvas.height;
    if (pOtherCanvas) {
        top = std::min(top, pOtherCanvas->y);
        bottom = std::max(bottom, pOtherCanvas->y + pOtherCanvas->height);
    }
    return _scroll_to_span(top * zoom, (bottom - top) * zoom);
}

void CtTimelinePanel::_settle_layout()
{
    const bool wasCentering = _centeringView;
    _centeringView = true;
    while (gtk_events_pending()) gtk_main_iteration_do(false);
    _centeringView = wasCentering;
}

void CtTimelinePanel::_settle_layout_until_stable()
{
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    if (not vAdj) return;
    // widgets such as rich tables size themselves over several layout passes: wait
    // until the height of the buffer stops changing
    const gint64 deadline = g_get_monotonic_time() + 400000;
    double lastUpper{-1};
    int stableChecks{0};
    while (g_get_monotonic_time() < deadline and stableChecks < 3) {
        _settle_layout();
        const double upper = vAdj->get_upper();
        if (upper == lastUpper) {
            ++stableChecks;
        }
        else {
            stableChecks = 0;
            lastUpper = upper;
        }
        g_usleep(10000);
    }
}

void CtTimelinePanel::_show_replay_content(const CtNodeContent& content, const CtNodeContent* pOtherState)
{
    auto* bridge = _pCtMainWin->get_command_bridge();
    // Rebuilt widgets such as rich tables take a few layout passes to reach their size:
    // draw once they have, or the view jumps on every switch between the states
    const bool frozeNow = _freeze_text_view();
    bridge->setPendingCursorPos(_replayState.cursorPos);
    bridge->setPendingScrollPos(_replayState.scrollPos);
    bridge->getDocumentModel()->setNodeContent(_replayState.replayNodeId, content, true);
    _settle_layout_until_stable();
    if (auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment()) {
        vAdj->set_value(_replayState.scrollPos);
    }
    if (pOtherState) _highlight_content_change(content, *pOtherState);
    else _clear_highlight();
    if (frozeNow) _thaw_text_view();
}

bool CtTimelinePanel::_freeze_text_view()
{
    // The view keeps being painted underneath, covered by a picture of how it looks now:
    // freezing the GDK window instead lost what changed meanwhile after a node switch
    auto* pOverlay = _pCtMainWin->get_drawing_overlay();
    if (not pOverlay or pOverlay->hasSnapshot()) return false;
    Gtk::Overlay& overlay = pOverlay->getOverlay();
    const int width = overlay.get_allocated_width();
    const int height = overlay.get_allocated_height();
    if (not overlay.get_mapped() or width <= 1 or height <= 1) return false;
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, width, height);
    auto cr = Cairo::Context::create(surface);
    gtk_widget_draw(GTK_WIDGET(overlay.gobj()), cr->cobj());
    pOverlay->showSnapshot(surface);
    return true;
}

void CtTimelinePanel::_thaw_text_view()
{
    if (auto* pOverlay = _pCtMainWin->get_drawing_overlay()) {
        pOverlay->clearSnapshot();
    }
}

void CtTimelinePanel::_highlight_content_change(const CtNodeContent& shown, const CtNodeContent& other)
{
    const CtContentChange change = CtDeltaEngine::changedRange(other, shown);
    if (change.start < 0) {
        _clear_highlight();
        return;
    }
    const int start = change.start;
    const int end = change.endAfter; // end of the change in the shown state
    Gtk::TextView& textView = _pCtMainWin->get_text_view().mm();
    std::vector<Gdk::Rectangle> rects;

    CtTreeIter treeIter = _pCtMainWin->get_tree_store().get_node_from_node_id(_replayState.replayNodeId);
    if (end == start + 1 and treeIter) {
        // a single changed widget: inside a rich table, the changed text of its cells
        const CtWidgetDesc shownWidget = shown.getWidgetDescAt(start);
        const CtWidgetDesc otherWidget = other.getWidgetDescAt(start);
        if (shownWidget.type == CtAnchWidgType::TableRich and otherWidget.type == CtAnchWidgType::TableRich and
            shownWidget.richTableData.size() == otherWidget.richTableData.size())
        {
            for (auto* pWidget : treeIter.get_anchored_widgets_fast()) {
                if (pWidget->getOffset() != start or pWidget->get_type() != CtAnchWidgType::TableRich) continue;
                auto* pTable = static_cast<CtTableRich*>(pWidget);
                for (size_t row = 0; row < shownWidget.richTableData.size(); ++row) {
                    const auto& shownRow = shownWidget.richTableData[row];
                    const auto& otherRow = otherWidget.richTableData[row];
                    for (size_t col = 0; col < shownRow.size() and col < otherRow.size(); ++col) {
                        const CtContentChange cellChange = CtDeltaEngine::changedCellRange(otherRow[col], shownRow[col]);
                        CtRichCell* pCell = cellChange.start >= 0 ? pTable->getRichCell(row, col) : nullptr;
                        if (not pCell) continue;
                        auto cellRects = _text_range_page_rects(pCell->get_text_view().mm(), cellChange.start, cellChange.endAfter);
                        rects.insert(rects.end(), cellRects.begin(), cellRects.end());
                    }
                }
            }
        }
    }
    if (rects.empty()) {
        rects = _text_range_page_rects(textView, start, end);
        // widgets in the change, whatever size their anchor reports
        if (treeIter) {
            for (auto* pWidget : treeIter.get_anchored_widgets_fast()) {
                if (pWidget->getOffset() >= start and pWidget->getOffset() < end) {
                    rects.push_back(_widget_page_rect(*pWidget));
                }
            }
        }
    }
    _highlight_page_rects(std::move(rects));
}

void CtTimelinePanel::_highlight_canvas_change(const std::vector<CtDrawingCanvas>& shown,
                                               const std::vector<CtDrawingCanvas>& other, int canvasIdx)
{
    if (canvasIdx < 0) {
        _clear_highlight();
        return;
    }
    const size_t idx = static_cast<size_t>(canvasIdx);
    const CtDrawingCanvas* pShown = idx < shown.size() ? &shown[idx] : nullptr;
    const CtDrawingCanvas* pOther = idx < other.size() ? &other[idx] : nullptr;
    const CtDrawingCanvas* pCanvas = pShown ? pShown : pOther;
    if (not pCanvas) {
        _clear_highlight();
        return;
    }
    const double zoom = _pCtMainWin->get_rt_zoom_scale_factor();
    auto f_canvas_rect = [zoom](const CtDrawingCanvas& canvas) {
        return Gdk::Rectangle(static_cast<int>(canvas.x * zoom), static_cast<int>(canvas.y * zoom),
                              static_cast<int>(canvas.width * zoom), static_cast<int>(canvas.height * zoom));
    };
    // where a stroke lies, in page coordinates, drawn on canvas
    auto f_stroke_rect = [zoom](const CtDrawingCanvas& canvas, const CtDrawingStroke& stroke) {
        double minX{0}, minY{0}, maxX{0}, maxY{0};
        for (size_t i = 0; i < stroke.points.size(); ++i) {
            const auto& pt = stroke.points[i];
            if (i == 0 or pt.x < minX) minX = pt.x;
            if (i == 0 or pt.y < minY) minY = pt.y;
            if (i == 0 or pt.x > maxX) maxX = pt.x;
            if (i == 0 or pt.y > maxY) maxY = pt.y;
        }
        if (stroke.type == CtDrawingElementType::Text) {
            maxX += stroke.fontSize * 0.6 * std::max<size_t>(1, stroke.textContent.size());
            maxY += stroke.fontSize * 1.3;
        }
        const double pad = std::max(stroke.lineWidth, 2.0);
        return Gdk::Rectangle(static_cast<int>((canvas.x + minX - pad) * zoom), static_cast<int>((canvas.y + minY - pad) * zoom),
                              static_cast<int>((maxX - minX + 2 * pad) * zoom), static_cast<int>((maxY - minY + 2 * pad) * zoom));
    };

    std::vector<Gdk::Rectangle> rects;
    const bool sameCanvasPlace = pShown and pOther and pShown->x == pOther->x and pShown->y == pOther->y and
                                 pShown->width == pOther->width and pShown->height == pOther->height;
    if (sameCanvasPlace) {
        // strokes that are in one state and not in the other: those on display, or else
        // the place of those the other state has
        auto f_differing = [](const CtDrawingCanvas& a, const CtDrawingCanvas& b) {
            std::vector<const CtDrawingStroke*> strokes;
            for (const auto& stroke : a.strokes) {
                const std::string serialized = CtDeltaEngine::serializeStroke(stroke);
                bool found{false};
                for (const auto& otherStroke : b.strokes) {
                    if (CtDeltaEngine::serializeStroke(otherStroke) == serialized) { found = true; break; }
                }
                if (not found) strokes.push_back(&stroke);
            }
            return strokes;
        };
        auto strokes = f_differing(*pShown, *pOther);
        if (strokes.empty()) strokes = f_differing(*pOther, *pShown);
        for (const auto* pStroke : strokes) {
            rects.push_back(f_stroke_rect(*pShown, *pStroke));
        }
    }
    if (rects.empty()) {
        rects.push_back(f_canvas_rect(*pCanvas));
    }
    _highlight_page_rects(std::move(rects));
}

void CtTimelinePanel::_highlight_page_rects(std::vector<Gdk::Rectangle> pageRects)
{
    auto* pOverlay = _pCtMainWin->get_drawing_overlay();
    if (not pOverlay) return;
    if (pageRects.empty()) {
        pOverlay->clearHighlightRegion();
        return;
    }
    // a margin around the whole change, as a percentage of its size
    constexpr double kMarginPercent{20.0};
    int minX = pageRects.front().get_x(), minY = pageRects.front().get_y();
    int maxX = minX + pageRects.front().get_width(), maxY = minY + pageRects.front().get_height();
    for (const auto& rect : pageRects) {
        minX = std::min(minX, rect.get_x());
        minY = std::min(minY, rect.get_y());
        maxX = std::max(maxX, rect.get_x() + rect.get_width());
        maxY = std::max(maxY, rect.get_y() + rect.get_height());
    }
    const double size = std::max(maxX - minX, maxY - minY);
    const int margin = static_cast<int>(std::clamp(size * kMarginPercent / 100.0, 6.0, 30.0));
    for (auto& rect : pageRects) {
        rect = Gdk::Rectangle(rect.get_x() - margin, rect.get_y() - margin,
                              rect.get_width() + 2 * margin, rect.get_height() + 2 * margin);
    }
    pOverlay->setHighlightRegion(std::move(pageRects), _pCtMainWin->get_ct_config()->timelineHighlightColor);
}

void CtTimelinePanel::_clear_highlight()
{
    if (auto* pOverlay = _pCtMainWin->get_drawing_overlay()) {
        pOverlay->clearHighlightRegion();
    }
}

std::vector<Gdk::Rectangle> CtTimelinePanel::_text_range_page_rects(Gtk::TextView& view, int start, int end)
{
    std::vector<Gdk::Rectangle> rects;
    auto* pOverlay = _pCtMainWin->get_drawing_overlay();
    auto rBuffer = view.get_buffer();
    if (not pOverlay or not rBuffer) return rects;
    auto hAdj = _pCtMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    const int hScroll = hAdj ? static_cast<int>(hAdj->get_value()) : 0;
    const int vScroll = vAdj ? static_cast<int>(vAdj->get_value()) : 0;
    auto f_to_page = [&](const Gdk::Rectangle& bufferRect) {
        int windowX{0}, windowY{0}, overlayX{0}, overlayY{0};
        view.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, bufferRect.get_x(), bufferRect.get_y(), windowX, windowY);
        view.translate_coordinates(pOverlay->getDrawingArea(), windowX, windowY, overlayX, overlayY);
        return Gdk::Rectangle(overlayX + hScroll, overlayY + vScroll, bufferRect.get_width(), bufferRect.get_height());
    };

    const int charCount = rBuffer->get_char_count();
    start = std::clamp(start, 0, charCount);
    end = std::clamp(end, start, charCount);
    if (start == end) {
        // removed in this state: where it was
        Gdk::Rectangle location;
        view.get_iter_location(rBuffer->get_iter_at_offset(start), location);
        rects.push_back(f_to_page(Gdk::Rectangle(location.get_x() - 1, location.get_y(), 3, location.get_height())));
        return rects;
    }
    // one rectangle per display line, grown by each character on it
    std::vector<Gdk::Rectangle> lineRects;
    for (auto iter = rBuffer->get_iter_at_offset(start); iter.get_offset() < end; iter.forward_char()) {
        Gdk::Rectangle location;
        view.get_iter_location(iter, location);
        const int width = std::max(location.get_width(), 2);
        if (not lineRects.empty() and lineRects.back().get_y() == location.get_y()) {
            auto& line = lineRects.back();
            const int x0 = std::min(line.get_x(), location.get_x());
            const int x1 = std::max(line.get_x() + line.get_width(), location.get_x() + width);
            line = Gdk::Rectangle(x0, line.get_y(), x1 - x0, std::max(line.get_height(), location.get_height()));
        }
        else {
            lineRects.push_back(Gdk::Rectangle(location.get_x(), location.get_y(), width, location.get_height()));
        }
    }
    for (const auto& line : lineRects) {
        rects.push_back(f_to_page(line));
    }
    return rects;
}

Gdk::Rectangle CtTimelinePanel::_widget_page_rect(Gtk::Widget& widget)
{
    auto* pOverlay = _pCtMainWin->get_drawing_overlay();
    if (not pOverlay) return Gdk::Rectangle();
    auto hAdj = _pCtMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    int overlayX{0}, overlayY{0};
    widget.translate_coordinates(pOverlay->getDrawingArea(), 0, 0, overlayX, overlayY);
    return Gdk::Rectangle(overlayX + (hAdj ? static_cast<int>(hAdj->get_value()) : 0),
                          overlayY + (vAdj ? static_cast<int>(vAdj->get_value()) : 0),
                          widget.get_allocated_width(), widget.get_allocated_height());
}

void CtTimelinePanel::_replayCanvas(int canvasIdx)
{
    constexpr int kHoldMs = 2000;
    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;

    const std::string highlightColor = _pCtMainWin->get_ct_config()->timelineHighlightColor;

    if (auto node = _pCtMainWin->get_command_bridge()->getDocumentModel()->getNodeById(_pCtMainWin->curr_tree_iter().get_node_id())) {
        const auto& canvases = node->getDrawingCanvases();
        if (canvasIdx >= 0 and static_cast<size_t>(canvasIdx) < canvases.size()) {
            _center_view_on_canvas(canvases[canvasIdx]);
        }
    }
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

void CtTimelinePanel::_replayTreeRow(gint64 /*nodeId*/)
{
    constexpr int kHoldMs = 2000;
    auto& treeView = _pCtMainWin->get_tree_view();

    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    Glib::ustring highlightColor = isDark ? "#554400" : "#FFFFAA";

    auto css = Gtk::CssProvider::create();
    css->load_from_data("treeview.timeline-flash row:selected { background-color: " +
                        highlightColor + "; }");
    treeView.get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    treeView.get_style_context()->add_class("timeline-flash");

    _replayState.replayingTreeRow = true;

    _replayState.timeout = Glib::signal_timeout().connect(
        [this, css, &treeView]() -> bool {
            treeView.get_style_context()->remove_class("timeline-flash");
            treeView.get_style_context()->remove_provider(css);
            _replayState.replayingTreeRow = false;
            return false;
        }, kHoldMs);
}

void CtTimelinePanel::_stopReplay()
{
    // a text replay ended the edit session of its node
    const gint64 restartNodeId = _replayState.replayingDrawing ? -1 : _replayState.replayNodeId;
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
                    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
                    if (currIter and currIter.get_node_id() == _replayState.replayNodeId) {
                        _show_replay_content(_replayState.restoreContent, nullptr);
                    }
                    else {
                        // a node no longer on display: no cursor or scroll to keep
                        docModel->setNodeContent(_replayState.replayNodeId, _replayState.restoreContent, true);
                    }
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
        _pCtMainWin->get_tree_view().get_style_context()->remove_class("timeline-flash");
    }
    if (_replayState.highlightTag) {
        auto rBuffer = _pCtMainWin->curr_buffer();
        if (rBuffer) {
            rBuffer->get_tag_table()->remove(_replayState.highlightTag);
        }
        _replayState.highlightTag.reset();
    }
    _restoreBufferModified();
    _restartEditSession(restartNodeId);
    _clear_highlight();
    _replayState = ReplayState{};
}

void CtTimelinePanel::on_node_leaving()
{
    if (_replayState.displayBuffer) {
        _replayState.displayBuffer->set_modified(_replayState.displayBufferWasModified);
    }
}

void CtTimelinePanel::on_node_switched()
{
    // the node replayed is no longer on display, so it is not rebuilt in view
    if (isReplaying()) {
        _stopReplay();
    }
}

void CtTimelinePanel::_restartEditSession(gint64 nodeId)
{
    // as after undo/redo, so that typing in the node is tracked again
    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
    if (nodeId >= 0 and currIter and currIter.get_node_id() == nodeId) {
        _pCtMainWin->get_command_bridge()->beginTextEditSession(nodeId);
    }
}

void CtTimelinePanel::_restoreBufferModified()
{
    if (_replayState.displayBuffer) {
        _replayState.displayBuffer->set_modified(_replayState.displayBufferWasModified);
        _replayState.displayBuffer.reset();
    }
}
