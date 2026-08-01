/*
 * ct_history_panel.h
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

class CtMainWin;
class CtAnchoredWidget;

class CtHistoryPanel
{
public:
    CtHistoryPanel(CtMainWin* pCtMainWin);

    Gtk::ScrolledWindow& get_widget() { return _scrolledWindow; }

    void add_entry(const CtHistoryEntry& entry);
    void remove_entries_for_node(gint64 nodeId);
    void clear();

    std::vector<CtHistoryEntry> get_all_entries() const;
    void load_entries(const std::vector<CtHistoryEntry>& entries);

    void refresh_names();

    bool isFlashing() const { return _flashState.timeout.connected(); }
    bool isFlashingCanvas() const { return _flashState.flashingCanvas; }
    bool isFlashingTreeRow() const { return _flashState.flashingTreeRow; }

    Gtk::TreeView& getTreeView() { return _treeView; }

    // Offset correction: apply shifts from a new command to all older entries for the same node
    void adjustOffsetsForNode(gint64 nodeId, const std::vector<CtCommand::OffsetShift>& shifts);

    // Update cursor/scroll on the top-level row for a node (navigate-away path)
    void updateCursorScroll(gint64 nodeId, int cursorPos, int scrollPos);

private:
    void _on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
    void _navigate_to_entry(const CtHistoryEntry& entry);
    void _flashRegion(const CtHistoryEntry& entry);
    void _stopFlash();

    // Find or create a top-level parent row for a node
    Gtk::TreeModel::iterator _findOrCreateParent(gint64 nodeId);

    CtMainWin* _pCtMainWin;

    class ColumnsHistory : public Gtk::TreeModel::ColumnRecord {
    public:
        ColumnsHistory() {
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
            add(colIsParent);
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
        Gtk::TreeModelColumn<bool>                       colIsParent;
    };

    ColumnsHistory               _columns;
    Glib::RefPtr<Gtk::TreeStore> _rTreeStore;
    Gtk::TreeView                _treeView;
    Gtk::ScrolledWindow          _scrolledWindow;
    bool                         _updatingList{false};

    void _flashCanvas(int canvasIdx);
    void _flashTreeRow(gint64 nodeId);

    // Bounding box flash state
    struct FlashState {
        int regionOffset{-1};
        int regionLength{0};
        int canvasIdx{-1};
        Glib::RefPtr<Gtk::TextTag> highlightTag;
        sigc::connection timeout;
        int flashCount{0};
        bool flashingCanvas{false};
        bool flashingTreeRow{false};
        CtAnchoredWidget* flashingWidget{nullptr};
    };
    FlashState _flashState;
};
