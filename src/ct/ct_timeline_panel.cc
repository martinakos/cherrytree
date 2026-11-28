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
#include <ctime>
#include <limits>
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
    // no fold arrows: a click on an entry unfolds it
    _treeView.set_show_expanders(false);
    _treeView.set_tooltip_column(_columns.colTooltip.index());

    auto colIdx = _treeView.append_column("", _columns.colIcon);
    _pIconColumn = _treeView.get_column(colIdx - 1);
    _pIconColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    _pIconColumn->set_fixed_width(40);
    update_show_icons();

    auto* pNameRenderer = Gtk::manage(new Gtk::CellRendererText{});
    auto* pNameCol = Gtk::manage(new Gtk::TreeViewColumn{_("Node")});
    pNameCol->pack_start(*pNameRenderer, true/*expand*/);
    pNameCol->add_attribute(pNameRenderer->property_markup(), _columns.colNameMarkup);
    _treeView.append_column(*pNameCol);
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
    _treeView.signal_row_expanded().connect(
        sigc::mem_fun(*this, &CtTimelinePanel::_on_row_expanded));

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
    row[_columns.colIsGroup]      = false;
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
    row[_columns.colNodeName]     = entry.nodeName;
    row[_columns.colNodePath]     = entry.nodePath;
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
    e.nodeName = static_cast<std::string>(row[_columns.colNodeName]);
    e.nodePath = static_cast<std::string>(row[_columns.colNodePath]);
    return e;
}

void CtTimelinePanel::_set_group_row(const Gtk::TreeModel::Row& row, const gint64 nodeId)
{
    row[_columns.colIsGroup] = true;
    row[_columns.colNodeId] = nodeId;
}

void CtTimelinePanel::_set_group_name(const Gtk::TreeModel::Row& row)
{
    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(row[_columns.colNodeId]);
    if (ctIter) {
        row[_columns.colName] = ctIter.get_node_name();
        row[_columns.colIcon] = ctIter.get_node_icon();
        row[_columns.colNameMarkup] = "<b>" + str::xml_escape(ctIter.get_node_name()) + "</b>";
        row[_columns.colTooltip] = str::xml_escape(
            CtMiscUtil::get_node_hierarchical_name(ctIter, " / ", false/*for_filename*/));
        return;
    }
    // a deleted node: the name and path stored with its newest action that has them
    std::string name, path;
    find_node_name(row[_columns.colNodeId], name, path);
    row[_columns.colName] = Glib::ustring{name};
    const Glib::ustring deleted = Glib::ustring("(") + _("deleted") + ")";
    row[_columns.colNameMarkup] = name.empty() ? str::xml_escape(deleted)
                                               : "<b>" + str::xml_escape(name) + "</b> " + str::xml_escape(deleted);
    row[_columns.colIcon] = Glib::RefPtr<Gdk::Pixbuf>{};
    row[_columns.colTooltip] = path.empty() ? Glib::ustring{} : Glib::ustring{str::xml_escape(path)};
}

void CtTimelinePanel::_update_group_summary(const Gtk::TreeModel::Row& row)
{
    _set_group_name(row);
    const size_t numActions = row.children().size();
    const gint64 visitTime = row[_columns.colVisitTime];
    if (0u == numActions and visitTime > 0) {
        row[_columns.colOperation] = Glib::ustring{_("Visited")};
        row[_columns.colTimestamp] = str::time_format(_pCtMainWin->get_ct_config()->timestampFormat,
                                                      static_cast<time_t>(visitTime));
        row[_columns.colTimestampRaw] = visitTime;
        return;
    }
    row[_columns.colOperation] = 1u == numActions ? Glib::ustring{_("1 action")}
                                                  : Glib::ustring{str::format(_("%s actions"), std::to_string(numActions))};
    if (numActions > 0) {
        // the time of the newest action
        const Gtk::TreeModel::Row newest = *row.children().begin();
        row[_columns.colTimestamp] = static_cast<Glib::ustring>(newest[_columns.colTimestamp]);
        row[_columns.colTimestampRaw] = static_cast<gint64>(newest[_columns.colTimestampRaw]);
    }
}

void CtTimelinePanel::_for_each_action(const std::function<bool(const Gtk::TreeModel::Row&)>& func) const
{
    for (const auto& group : _rTreeStore->children()) {
        for (const auto& row : group.children()) {
            if (not func(row)) return;
        }
    }
}

void CtTimelinePanel::add_entry(const CtTimelineEntry& entry)
{
    _updatingList = true;
    auto on_scope_exit = scope_guard([this](void*) { _updatingList = false; });

    // newest on top; the action joins the newest entry when that is on the same node,
    // otherwise it starts a new entry, unfolded (the others fold)
    auto topRows = _rTreeStore->children();
    Gtk::TreeModel::iterator groupIter;
    if (not topRows.empty() and (*topRows.begin())[_columns.colNodeId] == entry.nodeId) {
        groupIter = topRows.begin();
    }
    const bool newGroup = not groupIter;
    if (newGroup) {
        groupIter = _rTreeStore->prepend();
        _set_group_row(*groupIter, entry.nodeId);
    }
    const bool firstAction = groupIter->children().empty();
    _set_row(*_rTreeStore->prepend(groupIter->children()), entry);
    _update_group_summary(*groupIter);
    if (firstAction) {
        // also an entry started by a visit, which had nothing to unfold
        _treeView.expand_row(_rTreeStore->get_path(groupIter), false/*open_all*/);
    }

    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_timeline();
    }
}

void CtTimelinePanel::add_visit(const CtTreeIter& treeIter)
{
    const CtConfig* pConfig = _pCtMainWin->get_ct_config();
    if (not pConfig->timelineEnabled or not pConfig->timelineShowVisits or _selectingNode or not treeIter) return;
    auto topRows = _rTreeStore->children();
    if (not topRows.empty() and (*topRows.begin())[_columns.colNodeId] == treeIter.get_node_id()) return;

    _updatingList = true;
    auto on_scope_exit = scope_guard([this](void*) { _updatingList = false; });
    CtTimelineEntry visit;
    visit.nodeId = treeIter.get_node_id();
    visit.timestamp = std::time(nullptr);
    visit.useDay = pConfig->timelineUseDay;
    visit.nodeName = treeIter.get_node_name().raw();
    visit.nodePath = CtMiscUtil::get_node_hierarchical_name(treeIter, " / ", false/*for_filename*/);
    auto groupIter = _rTreeStore->prepend();
    _set_group_row(*groupIter, visit.nodeId);
    _set_group_visit(*groupIter, visit);
    _update_group_summary(*groupIter);
    // nothing to unfold yet: the others fold as when an entry unfolds
    _fold_other_entries(groupIter);

    if (auto* pStorage = _pCtMainWin->get_ct_storage()) {
        pStorage->pending_edit_db_timeline();
    }
}

void CtTimelinePanel::_set_group_visit(const Gtk::TreeModel::Row& row, const CtTimelineEntry& visit)
{
    row[_columns.colVisitTime] = visit.timestamp;
    row[_columns.colUseDay] = visit.useDay;
    row[_columns.colNodeName] = visit.nodeName;
    row[_columns.colNodePath] = visit.nodePath;
}

CtTimelineEntry CtTimelinePanel::_get_group_visit(const Gtk::TreeModel::Row& row) const
{
    CtTimelineEntry visit;
    visit.nodeId = row[_columns.colNodeId];
    visit.timestamp = row[_columns.colVisitTime];
    visit.useDay = row[_columns.colUseDay];
    visit.actionType = VISIT_ACTION_TYPE;
    visit.actionDescription = _("Visited");
    visit.nodeName = static_cast<std::string>(row[_columns.colNodeName]);
    visit.nodePath = static_cast<std::string>(row[_columns.colNodePath]);
    return visit;
}

void CtTimelinePanel::updateCursorScroll(gint64 nodeId, int cursorPos, int scrollPos)
{
    // the newest entry of the node is the one navigation returns to
    _for_each_action([&](const Gtk::TreeModel::Row& row) {
        if (row[_columns.colNodeId] != nodeId) return true;
        row[_columns.colCursorPos] = cursorPos;
        row[_columns.colScrollPos] = scrollPos;
        return false;
    });
}

void CtTimelinePanel::remove_entries_for_node(gint64 nodeId)
{
    std::vector<CtTimelineEntry> kept;
    bool removed = false;
    for (const CtTimelineEntry& entry : get_all_entries()) {
        if (entry.nodeId == nodeId) removed = true;
        else kept.push_back(entry);
    }
    if (removed) {
        // built again, so that the entries before and after the ones removed join
        // when they are on the same node
        _fill_groups(kept);
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
    // newest first: the actions of each entry, then the visit that started it
    std::vector<CtTimelineEntry> entries;
    for (const auto& group : _rTreeStore->children()) {
        for (const auto& row : group.children()) {
            entries.push_back(_get_row(row));
        }
        if (group[_columns.colVisitTime] > 0) {
            entries.push_back(_get_group_visit(group));
        }
    }
    return entries;
}

void CtTimelinePanel::load_entries(const std::vector<CtTimelineEntry>& entries)
{
    // Prune by use-day if configured
    int currentUseDay = _pCtMainWin->get_ct_config()->timelineUseDay;
    int maxUseDays = _pCtMainWin->get_ct_config()->timelineMaxUseDays;

    // newest first; files written when entries were grouped by node are not in time order
    std::vector<CtTimelineEntry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(), [](const CtTimelineEntry& a, const CtTimelineEntry& b) {
        return a.timestamp > b.timestamp;
    });
    std::vector<CtTimelineEntry> kept;
    for (const auto& entry : sorted) {
        if (maxUseDays > 0 && currentUseDay - entry.useDay > maxUseDays) {
            continue;
        }
        kept.push_back(entry);
    }
    _fill_groups(kept);
}

void CtTimelinePanel::_fill_groups(const std::vector<CtTimelineEntry>& entries)
{
    _updatingList = true;
    auto on_scope_exit = scope_guard([this](void*) { _updatingList = false; });
    _rTreeStore->clear();

    // newest first, so the visit that started an entry comes after its actions
    Gtk::TreeModel::iterator groupIter;
    bool groupStarted{false}; // by a visit: older entries go in another entry
    for (const auto& entry : entries) {
        if (not groupIter or groupStarted or (*groupIter)[_columns.colNodeId] != entry.nodeId) {
            if (groupIter) _update_group_summary(*groupIter);
            groupIter = _rTreeStore->append();
            _set_group_row(*groupIter, entry.nodeId);
            groupStarted = false;
        }
        if (VISIT_ACTION_TYPE == entry.actionType) {
            _set_group_visit(*groupIter, entry);
            groupStarted = true;
        }
        else {
            _set_row(*_rTreeStore->append(groupIter->children()), entry);
        }
    }
    if (groupIter) _update_group_summary(*groupIter);

    auto topRows = _rTreeStore->children();
    if (not topRows.empty()) {
        _treeView.expand_row(_rTreeStore->get_path(topRows.begin()), false/*open_all*/);
    }
}

void CtTimelinePanel::refresh_names()
{
    for (const auto& row : _rTreeStore->children()) {
        // the actions of a node still there keep its current name and path, which then
        // show if it is deleted
        if (CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(row[_columns.colNodeId])) {
            const std::string name = ctIter.get_node_name().raw();
            const std::string path = CtMiscUtil::get_node_hierarchical_name(ctIter, " / ", false/*for_filename*/);
            for (const auto& child : row.children()) {
                child[_columns.colNodeName] = name;
                child[_columns.colNodePath] = path;
            }
            if (row[_columns.colVisitTime] > 0) {
                row[_columns.colNodeName] = name;
                row[_columns.colNodePath] = path;
            }
        }
        _set_group_name(row);
    }
}

void CtTimelinePanel::update_show_icons()
{
    _pIconColumn->set_visible(_pCtMainWin->get_ct_config()->timelineShowIcons);
}

bool CtTimelinePanel::find_node_name(const gint64 nodeId, std::string& name, std::string& path) const
{
    // the actions, and the visits on entry rows, newest first
    auto f_found = [&](const Gtk::TreeModel::Row& row) {
        if (row[_columns.colNodeId] != nodeId) return false;
        const std::string rowName = static_cast<std::string>(row[_columns.colNodeName]);
        if (rowName.empty()) return false;
        name = rowName;
        path = static_cast<std::string>(row[_columns.colNodePath]);
        return true;
    };
    for (const auto& group : _rTreeStore->children()) {
        for (const auto& row : group.children()) {
            if (f_found(row)) return true;
        }
        if (group[_columns.colVisitTime] > 0 and f_found(group)) return true;
    }
    return false;
}

void CtTimelinePanel::adjustOffsetsForNode(gint64 nodeId, const std::vector<CtCommand::OffsetShift>& shifts)
{
    if (shifts.empty()) return;

    _for_each_action([&](const Gtk::TreeModel::Row& row) {
        if (row[_columns.colNodeId] == nodeId) {
            row[_columns.colRegionOffset] = CtCommand::applyOffsetShifts(row[_columns.colRegionOffset], shifts);
        }
        return true;
    });
}

void CtTimelinePanel::_on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* /*column*/)
{
    if (_updatingList) return;
    auto iter = _rTreeStore->get_iter(path);
    if (!iter) return;

    if ((*iter)[_columns.colIsGroup]) {
        _on_group_activated(path, (*iter)[_columns.colNodeId]);
        return;
    }
    const CtTimelineEntry entry = _get_row(*iter);
    if (_replayBusy) {
        _pendingEntry = entry;
        _pendingNodeId.reset();
        _replayCancelled = true;
        return;
    }
    // A running replay is stopped once the node switch is done (_replayEntry): rebuilding
    // the buffer still on display right before switching the view away hangs GTK. Leaving
    // that buffer flagged modified by the replay would mark the document as needing a save.
    if (_replayState.displayBuffer) {
        _replayState.displayBuffer->set_modified(_replayState.displayBufferWasModified);
    }
    _navigate_to_entry(entry);
}

void CtTimelinePanel::_on_row_expanded(const Gtk::TreeModel::iterator& iter, const Gtk::TreeModel::Path& path)
{
    // one entry at a time shows its actions
    if (path.size() != 1) return;
    _fold_other_entries(iter);
}

void CtTimelinePanel::_fold_other_entries(const Gtk::TreeModel::iterator& iter)
{
    for (const auto& row : _rTreeStore->children()) {
        if (row == *iter) continue;
        const Gtk::TreeModel::Path rowPath = _rTreeStore->get_path(row);
        if (_treeView.row_expanded(rowPath)) {
            _treeView.collapse_row(rowPath);
        }
    }
}

void CtTimelinePanel::_on_group_activated(const Gtk::TreeModel::Path& path, const gint64 nodeId)
{
    // a click on an entry unfolds or folds its actions and opens its node; only a click
    // on an action replays
    if (_treeView.row_expanded(path)) _treeView.collapse_row(path);
    else _treeView.expand_row(path, false/*open_all*/);
    if (_replayBusy) {
        _pendingEntry.reset();
        _pendingNodeId = nodeId;
        _replayCancelled = true;
        return;
    }
    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
    if (currIter and currIter.get_node_id() == nodeId) {
        // no node switch to stop a replay running on this node
        if (isReplaying()) _stopReplay();
        return;
    }
    // as for an action, a running replay is stopped by the node switch
    if (_replayState.displayBuffer) {
        _replayState.displayBuffer->set_modified(_replayState.displayBufferWasModified);
    }
    _select_node(nodeId);
}

void CtTimelinePanel::_select_node(const gint64 nodeId)
{
    CtTreeIter ctIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (!ctIter) {
        spdlog::warn("Timeline: node {} not found", nodeId);
        return;
    }
    _pCtMainWin->_navigatingHistory = true;
    _selectingNode = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; _selectingNode = false; });
    _pCtMainWin->get_tree_view().set_cursor_safe(ctIter);
    _pCtMainWin->get_tree_view().scroll_to_row(_pCtMainWin->get_tree_store().get_store()->get_path(ctIter), 0.5);
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
    _selectingNode = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; _selectingNode = false; });

    auto store = _pCtMainWin->get_tree_store().get_store();
    _pCtMainWin->get_tree_view().set_cursor_safe(ctIter);
    _pCtMainWin->get_tree_view().scroll_to_row(store->get_path(ctIter), 0.5);

    // another node selected before the replay starts: no replay, and the view unfrozen
    auto f_switched_away = [this](const gint64 nodeId) {
        CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
        if (currIter and currIter.get_node_id() == nodeId) return false;
        _thaw_text_view();
        return true;
    };
    CtTimelineEntry entryCopy = entry;
    Glib::signal_idle().connect_once([this, entryCopy, f_switched_away]() {
        if (f_switched_away(entryCopy.nodeId)) return;
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

        Glib::signal_idle().connect_once([this, entryCopy, f_switched_away]() {
            if (f_switched_away(entryCopy.nodeId)) return;
            _replayEntry(entryCopy);
        });
    });
}

void CtTimelinePanel::_replayEntry(const CtTimelineEntry& entry)
{
    _stopReplay();
    _replayBusy = true;
    _replayCancelled = false;
    auto on_scope_exit = scope_guard([this](void*) {
        _replayBusy = false;
        if (_replayCancelled) _finish_cancelled_replay();
        else _thaw_text_view(); // the view is in place once the replay has started
    });

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
    _for_each_action([&](const Gtk::TreeModel::Row& row) {
        if (row[_columns.colNodeId] == nodeId) {
            const std::string actionType = static_cast<Glib::ustring>(row[_columns.colActionType]);
            if (actionTypes.count(actionType)) {
                entries.push_back(_get_row(row));
            }
        }
        return true;
    });
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
    // the view stays where it is while the states are laid out, then is centred on the change
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    _replayState.scrollPos = vAdj ? vAdj->get_value() : -1.0;

    bridge->endTextEditSession();
    bridge->beginReplay();

    // Lay out both states, ending on "before" which shows first: a state shorter than
    // the displayed one cannot scroll as far (a rebuilt state can differ from what is
    // displayed too), so use a position both allow and the view does not move on every
    // cycle. The view is frozen meanwhile.
    const bool frozeNow = _freeze_text_view();
    double maxBothStates = std::numeric_limits<double>::max();
    for (const CtNodeContent* pState : {&_replayState.afterContent, &_replayState.beforeContent}) {
        _show_replay_content(*pState, pState == &_replayState.afterContent ? &_replayState.beforeContent : &_replayState.afterContent);
        if (_replayCancelled) return;
        if (vAdj) {
            maxBothStates = std::min(maxBothStates, std::max(vAdj->get_lower(), vAdj->get_upper() - vAdj->get_page_size()));
        }
    }
    // Centre on the change where it is in these states: the entry's offset follows the
    // node as it is now, where later edits may have moved the changed text
    const CtContentChange change = CtDeltaEngine::changedRange(_replayState.afterContent, _replayState.beforeContent);
    const double centred = change.start >= 0
        ? _center_view_on_offset(change.start, std::max(change.endAfter - change.start, 1))
        : _center_view_on_offset(entry.regionOffset, entry.regionLength);
    if (_replayCancelled) return;
    _replayState.scrollPos = std::min(centred, maxBothStates);
    if (vAdj) {
        vAdj->set_value(_replayState.scrollPos);
    }
    if (frozeNow) _thaw_text_view();

    constexpr int kCycleMs = 700;
    _replayState.timeout = Glib::signal_timeout().connect([this]() -> bool {
        _replayBusy = true;
        const bool keepGoing = _replay_model_tick();
        _replayBusy = false;
        if (_replayCancelled) {
            _finish_cancelled_replay();
            return false;
        }
        return keepGoing;
    }, kCycleMs);
}

// One switch between the states of a text replay; false once the replay is over
bool CtTimelinePanel::_replay_model_tick()
{
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
        if (_replayCancelled) return false;
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
        if (_replayCancelled) return false;
        _replayState.showingBefore = true;
    }
    return true;
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
        if (_replayCancelled) break;
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
    while (gtk_events_pending()) gtk_main_iteration_do(false);
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
    while (g_get_monotonic_time() < deadline and stableChecks < 3 and not _replayCancelled) {
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
    if (_replayCancelled) {
        // another node is on display now: nothing to scroll or highlight
        if (frozeNow) _thaw_text_view();
        return;
    }
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

void CtTimelinePanel::_finish_cancelled_replay()
{
    _replayCancelled = false;
    _stopReplay();
    _thaw_text_view();
    if (_pendingEntry) {
        const CtTimelineEntry entry = *_pendingEntry;
        _pendingEntry.reset();
        _navigate_to_entry(entry);
    }
    else if (_pendingNodeId) {
        const gint64 nodeId = *_pendingNodeId;
        _pendingNodeId.reset();
        _select_node(nodeId);
    }
}

void CtTimelinePanel::on_node_switched()
{
    // replay code running the event loop stops itself once that returns
    if (_replayBusy) {
        _replayCancelled = true;
        return;
    }
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
