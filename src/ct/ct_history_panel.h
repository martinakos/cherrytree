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

class CtMainWin;

class CtHistoryPanel
{
public:
    CtHistoryPanel(CtMainWin* pCtMainWin);

    Gtk::ScrolledWindow& get_widget() { return _scrolledWindow; }

    void add_entry(const CtHistoryEntry& entry);
    void update_entry(gint64 nodeId, int cursorPos, int scrollPos, int canvasEditX = -1, int canvasEditY = -1);
    void remove_entries_for_node(gint64 nodeId);
    void clear();

    std::vector<CtHistoryEntry> get_all_entries() const;
    void load_entries(const std::vector<CtHistoryEntry>& entries);

    void refresh_names();

private:
    void _on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
    void _navigate_to_entry(const CtHistoryEntry& entry);
    void _show_target_sign(int cursorPos, int canvasEditX, int canvasEditY);

    CtMainWin* _pCtMainWin;

    class ColumnsHistory : public Gtk::TreeModel::ColumnRecord {
    public:
        ColumnsHistory() {
            add(colIcon);
            add(colName);
            add(colTimestamp);
            add(colNodeId);
            add(colCursorPos);
            add(colScrollPos);
            add(colTimestampRaw);
            add(colCanvasEditX);
            add(colCanvasEditY);
        }
        Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> colIcon;
        Gtk::TreeModelColumn<Glib::ustring>              colName;
        Gtk::TreeModelColumn<Glib::ustring>              colTimestamp;
        Gtk::TreeModelColumn<gint64>                     colNodeId;
        Gtk::TreeModelColumn<int>                        colCursorPos;
        Gtk::TreeModelColumn<int>                        colScrollPos;
        Gtk::TreeModelColumn<gint64>                     colTimestampRaw;
        Gtk::TreeModelColumn<int>                        colCanvasEditX;
        Gtk::TreeModelColumn<int>                        colCanvasEditY;
    };

    ColumnsHistory               _columns;
    Glib::RefPtr<Gtk::ListStore> _rListStore;
    Gtk::TreeView                _treeView;
    Gtk::ScrolledWindow          _scrolledWindow;
    bool                         _updatingList{false};

};
