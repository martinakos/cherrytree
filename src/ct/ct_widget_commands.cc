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
#include "ct_command_bridge.h"
#include "ct_logging.h"

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

// EditRichCellCommand implementation

EditRichCellCommand::EditRichCellCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    CtCommandBridge* bridge,
    gint64 nodeId,
    int widgetCharOffset,
    size_t row,
    size_t col,
    const CtCellContent& oldContent,
    const CtCellContent& newContent,
    int oldCursorPos,
    int newCursorPos,
    std::string description)
    : _docModel(std::move(docModel))
    , _bridge(bridge)
    , _nodeId(nodeId)
    , _widgetCharOffset(widgetCharOffset)
    , _row(row)
    , _col(col)
    , _oldContent(oldContent)
    , _newContent(newContent)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
    , _description(std::move(description))
{
}

void EditRichCellCommand::_applyContent(const CtCellContent& content)
{
    if (_bridge) {
        // In-place path: updates model + live cell widget directly,
        // without rebuilding the whole node buffer.
        _bridge->applyRichCellInPlace(_nodeId, _widgetCharOffset, _row, _col, content);
        return;
    }
    // Fallback (e.g. unit tests): model-only path.
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) return;
    node->getContent().setWidgetRichTableCell(_widgetCharOffset, _row, _col, content);
    _docModel->notifyNodeChanged(_nodeId);
}

void EditRichCellCommand::execute() { _applyContent(_newContent); }
void EditRichCellCommand::undo()    { _applyContent(_oldContent); }
void EditRichCellCommand::redo()    { _applyContent(_newContent); }

std::string EditRichCellCommand::getDescription() const
{
    if (!_description.empty()) {
        return "Node " + std::to_string(_nodeId) + ": " + _description;
    }

    // Match the descriptions produced by TextEditCommand
    std::string description = "Node " + std::to_string(_nodeId) + ": ";
    Glib::ustring oldText = _oldContent.getPlainText();
    Glib::ustring newText = _newContent.getPlainText();

    if (newText.length() > oldText.length()) {
        size_t addedLen = newText.length() - oldText.length();
        size_t diffPos = 0;
        while (diffPos < oldText.length() && diffPos < newText.length() &&
               oldText[diffPos] == newText[diffPos]) {
            diffPos++;
        }
        Glib::ustring addedText = newText.substr(diffPos, addedLen);
        if (addedText == "\n") {
            description += "Type newline";
        } else if (addedText.find('\n') != Glib::ustring::npos) {
            description += "Type text with newlines";
        } else {
            Glib::ustring displayText = addedText;
            if (displayText.length() > 1 && displayText[displayText.length() - 1] == ' ') {
                if (displayText.length() < 2 || displayText[displayText.length() - 2] != ' ') {
                    displayText = displayText.substr(0, displayText.length() - 1);
                }
            }
            if (displayText.length() > 30) {
                description += "Type \"" + displayText.substr(0, 30).raw() + "...\"";
            } else {
                description += "Type \"" + displayText.raw() + "\"";
            }
        }
    } else if (oldText.length() > newText.length()) {
        size_t deletedLen = oldText.length() - newText.length();
        description += "Delete " + std::to_string(deletedLen) + " chars";
    } else {
        description += "Edit text";
    }

    return description;
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
