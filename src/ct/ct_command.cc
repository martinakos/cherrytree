/*
 * ct_command.cc
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

#include "ct_command.h"
#include "ct_document_model.h"
#include "ct_logging.h"
#include <typeinfo>
#include <fstream>
#include <glibmm.h>
#include <libxml++/libxml++.h>

// CompoundCommand implementation

CompoundCommand::CompoundCommand(const std::string& description)
    : _description(description)
{
}

void CompoundCommand::addCommand(std::unique_ptr<CtCommand> cmd)
{
    _commands.push_back(std::move(cmd));
}

void CompoundCommand::execute()
{
    spdlog::debug("Executing CompoundCommand: {}", _description);
    for (auto& cmd : _commands) {
        cmd->execute();
    }
}

void CompoundCommand::undo()
{
    spdlog::info("CompoundCommand::undo - desc='{}', {} sub-commands", _description, _commands.size());

    // Suppress notifications during delta replay to avoid intermediate buffer rebuilds
    if (_docModel) _docModel->suppressNotifications(true);

    try {
        for (auto it = _commands.rbegin(); it != _commands.rend(); ++it) {
            (*it)->undo();
        }
    }
    catch (...) {
        // Re-enable notifications even if exception occurred
        if (_docModel) _docModel->suppressNotifications(false);
        throw;
    }

    // Re-enable notifications and fire pending changes
    if (_docModel) _docModel->suppressNotifications(false);
}

void CompoundCommand::redo()
{
    spdlog::info("CompoundCommand::redo - desc='{}', {} sub-commands", _description, _commands.size());

    // Suppress notifications during delta replay to avoid intermediate buffer rebuilds
    if (_docModel) _docModel->suppressNotifications(true);

    try {
        for (auto& cmd : _commands) {
            cmd->redo();
        }
    }
    catch (...) {
        // Re-enable notifications even if exception occurred
        if (_docModel) _docModel->suppressNotifications(false);
        throw;
    }

    // Re-enable notifications and fire pending changes
    if (_docModel) _docModel->suppressNotifications(false);
}

std::string CompoundCommand::getDescription() const
{
    return _description;
}

// CtCommandManager implementation

CtCommandManager::CtCommandManager()
    : _maxUndoDepth(100)  // Default: 100 commands
{
}

CtCommandManager::~CtCommandManager()
{
    clear();
}

void CtCommandManager::executeCommand(std::unique_ptr<CtCommand> cmd)
{
    if (!cmd) {
        spdlog::warn("Attempted to execute null command");
        return;
    }

    // Note: We don't log command description here to avoid expensive XML parsing on every keystroke

    // If we're building a command group, add to it instead of executing directly
    if (_activeGroup) {
        _activeGroup->addCommand(std::move(cmd));
        return;
    }

    try {
        cmd->execute();

        // Clear redo stack (new action invalidates redo history)
        _redoStack.clear();

        // Add to undo stack
        _undoStack.push_back(std::move(cmd));
        trimUndoStack();
    }
    catch (const std::exception& e) {
        spdlog::error("Command execution failed: {}", e.what());
    }
}

void CtCommandManager::addCommandToStack(std::unique_ptr<CtCommand> cmd)
{
    if (!cmd) {
        spdlog::warn("Attempted to add null command to stack");
        return;
    }

    spdlog::debug("Adding command to stack without executing: {}", cmd->getDescription());

    // If we're building a command group, add to it
    if (_activeGroup) {
        _activeGroup->addCommand(std::move(cmd));
        return;
    }

    // Clear redo stack (new action invalidates redo history)
    _redoStack.clear();

    // Add to undo stack without executing
    _undoStack.push_back(std::move(cmd));

    // Trim stack if needed
    trimUndoStack();
}

bool CtCommandManager::undo()
{
    if (!canUndo()) {
        spdlog::info("Cannot undo: undo stack is empty");
        return false;
    }

    auto cmd = std::move(_undoStack.back());
    _undoStack.pop_back();

    spdlog::info("CtCommandManager: Undoing command: {} (type={})", cmd->getDescription(), typeid(*cmd).name());

    try {
        cmd->undo();
        _redoStack.push_back(std::move(cmd));
        spdlog::info("CtCommandManager: Undo successful");
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Command undo failed: {} - {}", cmd->getDescription(), e.what());
        // Put command back on undo stack on failure
        _undoStack.push_back(std::move(cmd));
        return false;
    }
}

bool CtCommandManager::redo()
{
    if (!canRedo()) {
        spdlog::debug("Cannot redo: redo stack is empty");
        return false;
    }

    auto cmd = std::move(_redoStack.back());
    _redoStack.pop_back();

    spdlog::debug("Redoing command: {}", cmd->getDescription());

    try {
        cmd->redo();
        _undoStack.push_back(std::move(cmd));
        trimUndoStack();
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Command redo failed: {} - {}", cmd->getDescription(), e.what());
        // Put command back on redo stack on failure
        _redoStack.push_back(std::move(cmd));
        return false;
    }
}

bool CtCommandManager::canUndo() const
{
    return !_undoStack.empty();
}

bool CtCommandManager::canRedo() const
{
    return !_redoStack.empty();
}

std::string CtCommandManager::getUndoDescription() const
{
    if (!canUndo()) {
        return "";
    }
    return _undoStack.back()->getDescription();
}

std::string CtCommandManager::getRedoDescription() const
{
    if (!canRedo()) {
        return "";
    }
    return _redoStack.back()->getDescription();
}

CtCommand* CtCommandManager::peekUndoCommand() const
{
    if (!canUndo()) {
        return nullptr;
    }
    return _undoStack.back().get();
}

CtCommand* CtCommandManager::peekRedoCommand() const
{
    if (!canRedo()) {
        return nullptr;
    }
    return _redoStack.back().get();
}

std::vector<std::string> CtCommandManager::getUndoStackDescriptions() const
{
    std::vector<std::string> descriptions;
    descriptions.reserve(_undoStack.size());
    // Iterate in reverse order (most recent first)
    for (auto it = _undoStack.rbegin(); it != _undoStack.rend(); ++it) {
        descriptions.push_back((*it)->getDescription());
    }
    return descriptions;
}

std::vector<std::string> CtCommandManager::getRedoStackDescriptions() const
{
    std::vector<std::string> descriptions;
    descriptions.reserve(_redoStack.size());
    // Iterate in reverse order (most recent first)
    for (auto it = _redoStack.rbegin(); it != _redoStack.rend(); ++it) {
        descriptions.push_back((*it)->getDescription());
    }
    return descriptions;
}

void CtCommandManager::undo(size_t count)
{
    if (count == 0) {
        return;
    }

    // Limit count to available undo stack size
    size_t actualCount = std::min(count, _undoStack.size());

    spdlog::info("CtCommandManager: Undoing {} command(s)", actualCount);

    for (size_t i = 0; i < actualCount; ++i) {
        undo();  // Call single undo() method
    }
}

void CtCommandManager::redo(size_t count)
{
    if (count == 0) {
        return;
    }

    // Limit count to available redo stack size
    size_t actualCount = std::min(count, _redoStack.size());

    spdlog::debug("CtCommandManager: Redoing {} command(s)", actualCount);

    for (size_t i = 0; i < actualCount; ++i) {
        redo();  // Call single redo() method
    }
}

void CtCommandManager::beginCommandGroup(const std::string& description)
{
    if (_activeGroup) {
        spdlog::warn("beginCommandGroup called while group already active");
        endCommandGroup();
    }

    spdlog::debug("Beginning command group: {}", description);
    _activeGroup = std::make_unique<CompoundCommand>(description);
}

void CtCommandManager::endCommandGroup()
{
    if (!_activeGroup) {
        spdlog::warn("endCommandGroup called with no active group");
        return;
    }

    spdlog::debug("Ending command group: {}", _activeGroup->getDescription());

    // Only add the group if it contains commands
    if (!_activeGroup->isEmpty()) {
        executeCommand(std::move(_activeGroup));
    }

    _activeGroup.reset();
}

void CtCommandManager::clear()
{
    spdlog::debug("Clearing command history");
    _undoStack.clear();
    _redoStack.clear();
    _activeGroup.reset();
}

void CtCommandManager::setMaxUndoDepth(size_t depth)
{
    _maxUndoDepth = depth;
    trimUndoStack();
}

void CtCommandManager::trimUndoStack()
{
    if (_maxUndoDepth == 0) {
        return;  // Unlimited
    }

    while (_undoStack.size() > _maxUndoDepth) {
        spdlog::debug("Trimming undo stack (max depth: {})", _maxUndoDepth);
        _undoStack.erase(_undoStack.begin());
    }
}
