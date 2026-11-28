/*
 * ct_timeline_panel.h
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

#pragma once

#include <gtkmm.h>
#include "ct_types.h"
#include "ct_command.h"
#include "ct_node_content.h"
#include "ct_drawing.h"
#include <functional>
#include <unordered_set>
#include <optional>

class CtMainWin;
class CtAnchoredWidget;
class CtTreeIter;

class CtTimelinePanel
{
public:
    CtTimelinePanel(CtMainWin* pCtMainWin);

    Gtk::ScrolledWindow& get_widget() { return _scrolledWindow; }

    void add_entry(const CtTimelineEntry& entry);
    // The user opened a node: an entry for it on top, when visits are shown and the
    // newest entry is on another node
    void add_visit(const CtTreeIter& treeIter);
    // Action type of the saved entry that records a visit
    static constexpr const char* VISIT_ACTION_TYPE{"VIS"};
    void remove_entries_for_node(gint64 nodeId);
    void clear();

    std::vector<CtTimelineEntry> get_all_entries() const;
    void load_entries(const std::vector<CtTimelineEntry>& entries);

    void refresh_names();
    // Shows or hides the node icons, as the preferences say
    void update_show_icons();
    // The name and path stored with the newest action on the node; false when none
    bool find_node_name(gint64 nodeId, std::string& name, std::string& path) const;

    bool isReplaying() const { return _replayState.timeout.connected(); }
    bool isReplayingCanvas() const { return _replayState.replayingCanvas; }
    bool isReplayingTreeRow() const { return _replayState.replayingTreeRow; }

    Gtk::TreeView& getTreeView() { return _treeView; }

    // Offset correction: apply shifts from a new command to all older entries for the same node
    void adjustOffsetsForNode(gint64 nodeId, const std::vector<CtCommand::OffsetShift>& shifts);

    // Update cursor/scroll on the top-level row for a node (navigate-away path)
    void updateCursorScroll(gint64 nodeId, int cursorPos, int scrollPos);

    // The tree is leaving a node: the replay's changes to its buffer are not edits
    void on_node_leaving();
    // The tree has switched to another node: a running replay or highlight stops
    void on_node_switched();

private:
    void _on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
    void _navigate_to_entry(const CtTimelineEntry& entry);
    void _replayEntry(const CtTimelineEntry& entry);
    void _stopReplay();
    void _finish_cancelled_replay();
    bool _replay_model_tick();
    void _restoreBufferModified();
    void _restartEditSession(gint64 nodeId);
    // Show one state of a model replay, drawn only once its layout is settled, with
    // what differs from pOtherState highlighted (no highlight when nullptr)
    void _show_replay_content(const CtNodeContent& content, const CtNodeContent* pOtherState);

    // Highlight of what the replayed entry changes, in the state on display, drawn
    // on the overlay in page coordinates (overlay coordinates plus the scroll offsets)
    void _highlight_content_change(const CtNodeContent& shown, const CtNodeContent& other);
    void _highlight_canvas_change(const std::vector<CtDrawingCanvas>& shown,
                                  const std::vector<CtDrawingCanvas>& other, int canvasIdx);
    void _highlight_page_rects(std::vector<Gdk::Rectangle> pageRects);
    void _clear_highlight();
    // one rectangle per display line covering [start, end) of the buffer of view
    std::vector<Gdk::Rectangle> _text_range_page_rects(Gtk::TextView& view, int start, int end);
    Gdk::Rectangle _widget_page_rect(Gtk::Widget& widget);

    // Bring what an entry changed into view, scrolling only when it is not already
    // visible; each returns the new scroll value
    double _scroll_to_span(double top, double height);
    double _center_view_on_offset(int offset, int length);
    double _center_view_on_canvas(const CtDrawingCanvas& canvas, const CtDrawingCanvas* pOtherCanvas = nullptr);
    // Run pending events so GTK lays out the buffer, ignoring timeline clicks meanwhile
    void _settle_layout();
    void _settle_layout_until_stable();
    // What is drawn stays as it is between the click and the view in place
    // returns false when the view was already frozen (or cannot be)
    bool _freeze_text_view();
    void _thaw_text_view();

    void _set_row(const Gtk::TreeModel::Row& row, const CtTimelineEntry& entry);
    CtTimelineEntry _get_row(const Gtk::TreeModel::Row& row) const;
    // Entry rows group the consecutive actions on one node: the node name and icon on the
    // entry, the actions as its children, newest first
    void _set_group_row(const Gtk::TreeModel::Row& row, gint64 nodeId);
    void _set_group_name(const Gtk::TreeModel::Row& row);
    void _update_group_summary(const Gtk::TreeModel::Row& row);
    // Fills the list from actions newest first, the newest entry unfolded
    void _fill_groups(const std::vector<CtTimelineEntry>& entries);
    // Every action row, newest first; stops when func returns false
    void _for_each_action(const std::function<bool(const Gtk::TreeModel::Row&)>& func) const;
    void _on_group_activated(const Gtk::TreeModel::Path& path, gint64 nodeId);
    void _on_row_expanded(const Gtk::TreeModel::iterator& iter, const Gtk::TreeModel::Path& path);
    void _fold_other_entries(const Gtk::TreeModel::iterator& iter);
    // An entry row starting with a visit: the visit as a saved entry
    void _set_group_visit(const Gtk::TreeModel::Row& row, const CtTimelineEntry& visit);
    CtTimelineEntry _get_group_visit(const Gtk::TreeModel::Row& row) const;
    // Selects the node in the tree, without a replay
    void _select_node(gint64 nodeId);
    // Entries of a node with one of the given action types, newest first
    std::vector<CtTimelineEntry> _collect_node_entries(gint64 nodeId, const std::unordered_set<std::string>& actionTypes) const;
    static int _find_entry(const std::vector<CtTimelineEntry>& entries, const CtTimelineEntry& entry);

    CtMainWin* _pCtMainWin;

    class ColumnsTimeline : public Gtk::TreeModel::ColumnRecord {
    public:
        ColumnsTimeline() {
            add(colIcon);
            add(colName);
            add(colOperation);
            add(colTimestamp);
            add(colNodeId);
            add(colCursorPos);
            add(colScrollPos);
            add(colTimestampRaw);
            add(colActionType);
            add(colRegionOffset);
            add(colRegionLength);
            add(colCanvasIdx);
            add(colUseDay);
            add(colDeltaData);
            add(colIsGroup);
            add(colNodeName);
            add(colNodePath);
            add(colTooltip);
            add(colNameMarkup);
            add(colVisitTime);
        }
        Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> colIcon;
        Gtk::TreeModelColumn<Glib::ustring>              colName;
        Gtk::TreeModelColumn<Glib::ustring>              colOperation;
        Gtk::TreeModelColumn<Glib::ustring>              colTimestamp;
        Gtk::TreeModelColumn<gint64>                     colNodeId;
        Gtk::TreeModelColumn<int>                        colCursorPos;
        Gtk::TreeModelColumn<int>                        colScrollPos;
        Gtk::TreeModelColumn<gint64>                     colTimestampRaw;
        Gtk::TreeModelColumn<Glib::ustring>              colActionType;
        Gtk::TreeModelColumn<int>                        colRegionOffset;
        Gtk::TreeModelColumn<int>                        colRegionLength;
        Gtk::TreeModelColumn<int>                        colCanvasIdx;
        Gtk::TreeModelColumn<int>                        colUseDay;
        Gtk::TreeModelColumn<std::string>                colDeltaData;
        Gtk::TreeModelColumn<bool>                       colIsGroup;
        Gtk::TreeModelColumn<std::string>                colNodeName; // as stored with the action
        Gtk::TreeModelColumn<std::string>                colNodePath; // as stored with the action
        Gtk::TreeModelColumn<Glib::ustring>              colTooltip;  // markup: the node path
        Gtk::TreeModelColumn<Glib::ustring>              colNameMarkup; // the name as shown: bold, "(deleted)" after it
        Gtk::TreeModelColumn<gint64>                     colVisitTime;  // entry rows started by a visit: its time, else 0
    };

    ColumnsTimeline               _columns;
    Glib::RefPtr<Gtk::TreeStore> _rTreeStore;
    Gtk::TreeView                _treeView;
    Gtk::TreeViewColumn*         _pIconColumn{nullptr};
    Gtk::ScrolledWindow          _scrolledWindow;
    bool                         _updatingList{false};
    // the timeline itself is opening a node: not a visit
    bool                         _selectingNode{false};
    // Replay code is running and may run the event loop (laying out a state): a node
    // switch or a timeline click meanwhile only cancels the replay, which stops itself
    // as soon as the step it is in returns, then goes to the entry clicked if any
    bool                         _replayBusy{false};
    bool                         _replayCancelled{false};
    std::optional<CtTimelineEntry> _pendingEntry;
    // the node of an entry clicked meanwhile, opened without a replay
    std::optional<gint64>          _pendingNodeId;

    void _replayViaModel(const CtTimelineEntry& entry);
    void _replayViaDrawingModel(const CtTimelineEntry& entry);
    void _replayFallbackHighlight(const CtTimelineEntry& entry);
    void _replayCanvas(int canvasIdx);
    void _replayTreeRow(gint64 nodeId);

    struct ReplayState {
        sigc::connection timeout;
        int canvasIdx{-1};
        bool replayingCanvas{false};
        bool replayingTreeRow{false};
        // Model-based replay (text content)
        CtNodeContent restoreContent;
        CtNodeContent beforeContent;
        CtNodeContent afterContent;
        gint64 replayNodeId{-1};
        int cycleCount{0};
        int maxCycles{3};
        bool showingBefore{true};
        int cursorPos{0};
        double scrollPos{-1.0};
        // Drawing model replay
        std::vector<CtDrawingCanvas> restoreCanvases;
        std::vector<CtDrawingCanvas> beforeCanvases;
        std::vector<CtDrawingCanvas> afterCanvases;
        bool replayingDrawing{false};
        // Fallback highlight
        Glib::RefPtr<Gtk::TextTag> highlightTag;
        // Buffer changed only for display; its modified flag is put back at the end
        Glib::RefPtr<Gtk::TextBuffer> displayBuffer;
        bool displayBufferWasModified{false};
    };
    ReplayState _replayState;
};
