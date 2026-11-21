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
#include <unordered_set>

class CtMainWin;
class CtAnchoredWidget;

class CtTimelinePanel
{
public:
    CtTimelinePanel(CtMainWin* pCtMainWin);

    Gtk::ScrolledWindow& get_widget() { return _scrolledWindow; }

    void add_entry(const CtTimelineEntry& entry);
    void remove_entries_for_node(gint64 nodeId);
    void clear();

    std::vector<CtTimelineEntry> get_all_entries() const;
    void load_entries(const std::vector<CtTimelineEntry>& entries);

    void refresh_names();

    bool isReplaying() const { return _replayState.timeout.connected(); }
    bool isReplayingCanvas() const { return _replayState.replayingCanvas; }
    bool isReplayingTreeRow() const { return _replayState.replayingTreeRow; }

    Gtk::TreeView& getTreeView() { return _treeView; }

    // Offset correction: apply shifts from a new command to all older entries for the same node
    void adjustOffsetsForNode(gint64 nodeId, const std::vector<CtCommand::OffsetShift>& shifts);

    // Update cursor/scroll on the top-level row for a node (navigate-away path)
    void updateCursorScroll(gint64 nodeId, int cursorPos, int scrollPos);

private:
    void _on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
    void _navigate_to_entry(const CtTimelineEntry& entry);
    void _replayEntry(const CtTimelineEntry& entry);
    void _stopReplay();
    void _restoreBufferModified();
    void _restartEditSession(gint64 nodeId);
    // Show one state of a model replay, drawn only once its layout is settled
    void _show_replay_content(const CtNodeContent& content);

    // Bring what an entry changed into view, scrolling only when it is not already
    // visible; each returns the new scroll value
    double _scroll_to_span(double top, double height);
    double _center_view_on_offset(int offset, int length);
    double _center_view_on_canvas(const CtDrawingCanvas& canvas, const CtDrawingCanvas* pOtherCanvas = nullptr);
    // Run pending events so GTK lays out the buffer, ignoring timeline clicks meanwhile
    void _settle_layout();
    void _settle_layout_until_stable();
    // No painting of the text view between the click and the view in place
    // returns false when the view was already frozen (or cannot be)
    bool _freeze_text_view();
    void _thaw_text_view();

    void _set_row(const Gtk::TreeModel::Row& row, const CtTimelineEntry& entry);
    CtTimelineEntry _get_row(const Gtk::TreeModel::Row& row) const;
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
    };

    ColumnsTimeline               _columns;
    Glib::RefPtr<Gtk::TreeStore> _rTreeStore;
    Gtk::TreeView                _treeView;
    Gtk::ScrolledWindow          _scrolledWindow;
    bool                         _updatingList{false};
    bool                         _centeringView{false};
    Glib::RefPtr<Gdk::Window>    _rFrozenWindow;

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
