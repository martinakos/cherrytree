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

    // Show "before" state first
    bridge->setPendingCursorPos(_replayState.cursorPos);
    bridge->setPendingScrollPos(_replayState.scrollPos);
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
            br->setPendingScrollPos(_replayState.scrollPos);
            dm->setNodeContent(_replayState.replayNodeId, _replayState.afterContent, true);
            _replayState.showingBefore = false;
        } else {
            // Switch to "before" and increment cycle
            _replayState.cycleCount++;
            if (_replayState.cycleCount >= _replayState.maxCycles) {
                // Done cycling — restore original
                br->setPendingCursorPos(_replayState.cursorPos);
                br->setPendingScrollPos(_replayState.scrollPos);
                dm->setNodeContent(_replayState.replayNodeId, _replayState.restoreContent, true);
                br->endReplay();
                _restoreBufferModified();
                _replayState.replayNodeId = -1;
                _replayState.timeout.disconnect();
                return false;
            }
            br->setPendingCursorPos(_replayState.cursorPos);
            br->setPendingScrollPos(_replayState.scrollPos);
            dm->setNodeContent(_replayState.replayNodeId, _replayState.beforeContent, true);
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

    // centre on the canvas where it is after the change, or before it for a deleted canvas
    const size_t canvasIdx = static_cast<size_t>(std::max(entry.canvasIdx, 0));
    if (canvasIdx < _replayState.afterCanvases.size()) {
        _center_view_on_canvas(_replayState.afterCanvases[canvasIdx]);
    }
    else if (canvasIdx < _replayState.beforeCanvases.size()) {
        _center_view_on_canvas(_replayState.beforeCanvases[canvasIdx]);
    }

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

void CtTimelinePanel::_replayFallbackHighlight(const CtTimelineEntry& entry)
{
    auto rBuffer = _pCtMainWin->curr_buffer();
    if (!rBuffer) return;
    if (entry.regionLength <= 0) return;

    int charCount = rBuffer->get_char_count();
    int start = std::min(entry.regionOffset, charCount);
    int end = std::min(entry.regionOffset + entry.regionLength, charCount);
    if (start >= end) return;

    _replayState.displayBuffer = rBuffer;
    _replayState.displayBufferWasModified = rBuffer->get_modified();
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
    _center_view_on_offset(start, end - start);

    _replayState.highlightTag = tag;
    constexpr int kHoldMs = 2000;
    _replayState.timeout = Glib::signal_timeout().connect(
        [this, tag, rBuffer]() -> bool {
            rBuffer->get_tag_table()->remove(tag);
            _replayState.highlightTag.reset();
            _restoreBufferModified();
            return false;
        }, kHoldMs);
}

double CtTimelinePanel::_scroll_to_span(double top, double height)
{
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    if (not vAdj) return -1.0;
    const double pageSize = vAdj->get_page_size();
    // a span taller than the view starts a little below the top edge
    double target = height < pageSize ? top + height / 2.0 - pageSize / 2.0 : top - pageSize / 10.0;
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
        while (gtk_events_pending()) gtk_main_iteration_do(false);
    }
    return value;
}

double CtTimelinePanel::_center_view_on_canvas(const CtDrawingCanvas& canvas)
{
    const double zoom = _pCtMainWin->get_rt_zoom_scale_factor();
    return _scroll_to_span(canvas.y * zoom, canvas.height * zoom);
}

void CtTimelinePanel::_replayCanvas(int canvasIdx)
{
    constexpr int kHoldMs = 2000;
    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;

    bool isDark = false;
    if (auto settings = Gtk::Settings::get_default()) {
        isDark = settings->property_gtk_application_prefer_dark_theme();
    }
    std::string highlightColor = isDark ? "#554400" : "#FFFFAA";

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
                    // a node no longer on display is rebuilt without consuming these
                    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
                    if (currIter and currIter.get_node_id() == _replayState.replayNodeId) {
                        bridge->setPendingCursorPos(_replayState.cursorPos);
                        bridge->setPendingScrollPos(_replayState.scrollPos);
                    }
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
    _replayState = ReplayState{};
}

void CtTimelinePanel::_restoreBufferModified()
{
    if (_replayState.displayBuffer) {
        _replayState.displayBuffer->set_modified(_replayState.displayBufferWasModified);
        _replayState.displayBuffer.reset();
    }
}
