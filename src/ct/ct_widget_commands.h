/*
 * ct_widget_commands.h
 *
 * Copyright 2009-2025
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

#include "ct_command.h"
#include "ct_document_model.h"
#include <glibmm/ustring.h>
#include <memory>

// Base command for widget operations
// Widgets (images, tables, codeboxes) are embedded in node content XML
// These commands use XML snapshot approach like text commands
class WidgetCommand : public CtCommand {
public:
    WidgetCommand(
        std::shared_ptr<CtDocumentModel> docModel,
        gint64 nodeId,
        const Glib::ustring& oldContentXml,
        const Glib::ustring& newContentXml,
        const std::string& widgetType,
        int oldCursorPos = -1,
        int newCursorPos = -1
    );

    void execute() override;
    void undo() override;
    void redo() override;
    std::string getDescription() const override;

    gint64 getNodeId() const override { return _nodeId; }
    int getOldCursorPos() const override { return _oldCursorPos; }
    int getNewCursorPos() const override { return _newCursorPos; }
    double getOldScrollPos() const override { return _oldScrollPos; }
    double getNewScrollPos() const override { return _newScrollPos; }

    void setOldScrollPos(double pos) { _oldScrollPos = pos; }
    void setNewScrollPos(double pos) { _newScrollPos = pos; }

protected:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    Glib::ustring _oldContentXml;
    Glib::ustring _newContentXml;
    std::string _widgetType;
    int _oldCursorPos;
    int _newCursorPos;
    double _oldScrollPos{-1.0};
    double _newScrollPos{-1.0};
};

// Lightweight delta command for inserting a widget.
// Stores only the CtWidgetDesc (~1-50KB) instead of two full node XML snapshots (~200KB).
class InsertWidgetDeltaCommand : public CtCommand {
public:
    InsertWidgetDeltaCommand(
        std::shared_ptr<CtDocumentModel> docModel,
        gint64 nodeId,
        int charOffset,
        const CtWidgetDesc& widgetDesc,
        const std::string& description,
        int oldCursorPos = -1,
        int newCursorPos = -1
    );

    void execute() override; // insertWidget on model → notifyNodeChanged
    void undo() override;    // removeWidget from model → notifyNodeChanged
    void redo() override;    // same as execute
    std::string getDescription() const override;

    gint64 getNodeId() const override { return _nodeId; }
    int getOldCursorPos() const override { return _oldCursorPos; }
    int getNewCursorPos() const override { return _newCursorPos; }
    double getOldScrollPos() const override { return _oldScrollPos; }
    double getNewScrollPos() const override { return _newScrollPos; }

    void setOldScrollPos(double pos) { _oldScrollPos = pos; }
    void setNewScrollPos(double pos) { _newScrollPos = pos; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    int _charOffset;
    CtWidgetDesc _widgetDesc;
    std::string _description;
    int _oldCursorPos;
    int _newCursorPos;
    double _oldScrollPos{-1.0};
    double _newScrollPos{-1.0};
};

// Lightweight delta command for modifying a widget's properties or content.
// Covers: image edit, table structural changes (row/col add/delete/move/sort/width).
class ModifyWidgetDeltaCommand : public CtCommand {
public:
    ModifyWidgetDeltaCommand(
        std::shared_ptr<CtDocumentModel> docModel,
        gint64 nodeId,
        int charOffset,
        const CtWidgetDesc& oldWidgetDesc,
        const CtWidgetDesc& newWidgetDesc,
        const std::string& description,
        int oldCursorPos = -1,
        int newCursorPos = -1
    );

    void execute() override; // replaceWidget with new → notifyNodeChanged
    void undo() override;    // replaceWidget with old → notifyNodeChanged
    void redo() override;    // same as execute
    std::string getDescription() const override;

    gint64 getNodeId() const override { return _nodeId; }
    int getOldCursorPos() const override { return _oldCursorPos; }
    int getNewCursorPos() const override { return _newCursorPos; }
    double getOldScrollPos() const override { return _oldScrollPos; }
    double getNewScrollPos() const override { return _newScrollPos; }

    void setOldScrollPos(double pos) { _oldScrollPos = pos; }
    void setNewScrollPos(double pos) { _newScrollPos = pos; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    int _charOffset;
    CtWidgetDesc _oldWidgetDesc;
    CtWidgetDesc _newWidgetDesc;
    std::string _description;
    int _oldCursorPos;
    int _newCursorPos;
    double _oldScrollPos{-1.0};
    double _newScrollPos{-1.0};
};

// Lightweight delta command for editing a single table cell's text content.
// Stores only old/new cell text instead of full node XML snapshots (~1000x memory reduction).
class EditTableCellCommand : public CtCommand {
public:
    EditTableCellCommand(
        std::shared_ptr<CtDocumentModel> docModel,
        gint64 nodeId,
        int widgetCharOffset,
        size_t row,
        size_t col,
        const Glib::ustring& oldText,
        const Glib::ustring& newText,
        int oldCursorPos = -1,
        int newCursorPos = -1
    );

    void execute() override;
    void undo() override;
    void redo() override;
    std::string getDescription() const override;

    gint64 getNodeId() const override { return _nodeId; }
    int getOldCursorPos() const override { return _oldCursorPos; }
    int getNewCursorPos() const override { return _newCursorPos; }
    double getOldScrollPos() const override { return _oldScrollPos; }
    double getNewScrollPos() const override { return _newScrollPos; }

    void setOldScrollPos(double pos) { _oldScrollPos = pos; }
    void setNewScrollPos(double pos) { _newScrollPos = pos; }

private:
    void _applyText(const Glib::ustring& text);

    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    int _widgetCharOffset;
    size_t _row;
    size_t _col;
    Glib::ustring _oldText;
    Glib::ustring _newText;
    int _oldCursorPos;
    int _newCursorPos;
    double _oldScrollPos{-1.0};
    double _newScrollPos{-1.0};
};

// Lightweight delta command for editing codebox text content.
// Stores only old/new text instead of full node XML snapshots (~1000x memory reduction).
class EditCodeboxContentCommand : public CtCommand {
public:
    EditCodeboxContentCommand(
        std::shared_ptr<CtDocumentModel> docModel,
        gint64 nodeId,
        int widgetCharOffset,
        const std::string& oldContent,
        const std::string& newContent,
        int oldCursorPos = -1,
        int newCursorPos = -1
    );

    void execute() override;
    void undo() override;
    void redo() override;
    std::string getDescription() const override;

    gint64 getNodeId() const override { return _nodeId; }
    int getOldCursorPos() const override { return _oldCursorPos; }
    int getNewCursorPos() const override { return _newCursorPos; }
    double getOldScrollPos() const override { return _oldScrollPos; }
    double getNewScrollPos() const override { return _newScrollPos; }

    void setOldScrollPos(double pos) { _oldScrollPos = pos; }
    void setNewScrollPos(double pos) { _newScrollPos = pos; }

private:
    void _applyContent(const std::string& content);

    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    int _widgetCharOffset;
    std::string _oldContent;
    std::string _newContent;
    int _oldCursorPos;
    int _newCursorPos;
    double _oldScrollPos{-1.0};
    double _newScrollPos{-1.0};
};
