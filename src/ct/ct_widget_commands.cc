/*
 * ct_widget_commands.cc
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

#include "ct_widget_commands.h"
#include "ct_logging.h"

// WidgetCommand base implementation

WidgetCommand::WidgetCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    const Glib::ustring& oldContentXml,
    const Glib::ustring& newContentXml,
    const std::string& widgetType,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(docModel)
    , _nodeId(nodeId)
    , _oldContentXml(oldContentXml)
    , _newContentXml(newContentXml)
    , _widgetType(widgetType)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void WidgetCommand::execute()
{
    if (!_docModel) {
        spdlog::error("WidgetCommand: null document model");
        return;
    }

    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("WidgetCommand: node {} not found", _nodeId);
        return;
    }

    spdlog::debug("WidgetCommand: executing {} for node {}", _widgetType, _nodeId);
    node->setContentXml(_newContentXml);
    _docModel->notifyNodeChanged(_nodeId);
}

void WidgetCommand::undo()
{
    if (!_docModel) {
        spdlog::error("WidgetCommand: null document model");
        return;
    }

    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("WidgetCommand: node {} not found", _nodeId);
        return;
    }

    spdlog::debug("WidgetCommand: undoing {} for node {}", _widgetType, _nodeId);
    node->setContentXml(_oldContentXml);
    _docModel->notifyNodeChanged(_nodeId);
}

void WidgetCommand::redo()
{
    execute();
}

std::string WidgetCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": " + _widgetType;
}

// InsertWidgetDeltaCommand implementation

InsertWidgetDeltaCommand::InsertWidgetDeltaCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int charOffset,
    const CtWidgetDesc& widgetDesc,
    const std::string& description,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(std::move(docModel))
    , _nodeId(nodeId)
    , _charOffset(charOffset)
    , _widgetDesc(widgetDesc)
    , _description(description)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void InsertWidgetDeltaCommand::execute()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) { spdlog::error("InsertWidgetDeltaCommand: node {} not found", _nodeId); return; }
    node->getContent().insertWidget(_charOffset, _widgetDesc);
    _docModel->notifyNodeChanged(_nodeId);
}

void InsertWidgetDeltaCommand::undo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) { spdlog::error("InsertWidgetDeltaCommand::undo: node {} not found", _nodeId); return; }
    node->getContent().removeWidget(_charOffset);
    _docModel->notifyNodeChanged(_nodeId);
}

void InsertWidgetDeltaCommand::redo()
{
    execute();
}

std::string InsertWidgetDeltaCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": " + _description;
}

// ModifyWidgetDeltaCommand implementation

ModifyWidgetDeltaCommand::ModifyWidgetDeltaCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int charOffset,
    const CtWidgetDesc& oldWidgetDesc,
    const CtWidgetDesc& newWidgetDesc,
    const std::string& description,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(std::move(docModel))
    , _nodeId(nodeId)
    , _charOffset(charOffset)
    , _oldWidgetDesc(oldWidgetDesc)
    , _newWidgetDesc(newWidgetDesc)
    , _description(description)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void ModifyWidgetDeltaCommand::execute()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) { spdlog::error("ModifyWidgetDeltaCommand: node {} not found", _nodeId); return; }
    node->getContent().replaceWidget(_charOffset, _newWidgetDesc);
    _docModel->notifyNodeChanged(_nodeId);
}

void ModifyWidgetDeltaCommand::undo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) { spdlog::error("ModifyWidgetDeltaCommand::undo: node {} not found", _nodeId); return; }
    node->getContent().replaceWidget(_charOffset, _oldWidgetDesc);
    _docModel->notifyNodeChanged(_nodeId);
}

void ModifyWidgetDeltaCommand::redo()
{
    execute();
}

std::string ModifyWidgetDeltaCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": " + _description;
}

// EditTableCellCommand implementation

EditTableCellCommand::EditTableCellCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int widgetCharOffset,
    size_t row,
    size_t col,
    const Glib::ustring& oldText,
    const Glib::ustring& newText,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(std::move(docModel))
    , _nodeId(nodeId)
    , _widgetCharOffset(widgetCharOffset)
    , _row(row)
    , _col(col)
    , _oldText(oldText)
    , _newText(newText)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void EditTableCellCommand::_applyText(const Glib::ustring& text)
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) return;
    node->getContent().setWidgetTableCell(_widgetCharOffset, _row, _col, text);
    _docModel->notifyNodeChanged(_nodeId);
}

void EditTableCellCommand::execute()
{
    _applyText(_newText);
}

void EditTableCellCommand::undo()
{
    _applyText(_oldText);
}

void EditTableCellCommand::redo()
{
    _applyText(_newText);
}

std::string EditTableCellCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": Edit table cell";
}

// EditCodeboxContentCommand implementation

EditCodeboxContentCommand::EditCodeboxContentCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int widgetCharOffset,
    const std::string& oldContent,
    const std::string& newContent,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(std::move(docModel))
    , _nodeId(nodeId)
    , _widgetCharOffset(widgetCharOffset)
    , _oldContent(oldContent)
    , _newContent(newContent)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void EditCodeboxContentCommand::_applyContent(const std::string& content)
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) return;
    node->getContent().setWidgetContentData(_widgetCharOffset, content);
    _docModel->notifyNodeChanged(_nodeId);
}

void EditCodeboxContentCommand::execute()
{
    _applyContent(_newContent);
}

void EditCodeboxContentCommand::undo()
{
    _applyContent(_oldContent);
}

void EditCodeboxContentCommand::redo()
{
    _applyContent(_newContent);
}

std::string EditCodeboxContentCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": Edit codebox";
}
