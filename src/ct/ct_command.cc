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
#include <algorithm>
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
    size_t numExecuted = 0;
    try {
        for (auto& cmd : _commands) {
            cmd->execute();
            ++numExecuted;
        }
    }
    catch (...) {
        // Undo the steps that did run, in reverse, so a failure in the middle
        // does not leave the model with a partially applied group.
        spdlog::error("CompoundCommand '{}': step {} of {} failed, rolling back",
                      _description, numExecuted + 1, _commands.size());
        for (size_t i = numExecuted; i > 0; --i) {
            try {
                _commands[i - 1]->undo();
            }
            catch (const std::exception& e) {
                spdlog::error("CompoundCommand '{}': rollback of step {} failed: {}", _description, i, e.what());
            }
        }
        throw;
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
    if (_nodeId > 0 && (_description.empty() || _description[0] != '[')) {
        return "[" + std::to_string(_nodeId) + "] " + _description;
    }
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

    try {
        cmd->execute();

        // Clear redo stack (new action invalidates redo history)
        _redoStack.clear();

        // Add to undo stack
        _undoStack.push_back(std::move(cmd));
        trimUndoStack();
    }
    catch (const std::exception& e) {
        // CompoundCommand::execute rolls back the steps it had applied before
        // rethrowing, so the model is not left half way; the command is dropped.
        spdlog::error("Command execution failed: {} - {}", cmd->getDescription(), e.what());
    }
}

void CtCommandManager::addCommandToStack(std::unique_ptr<CtCommand> cmd)
{
    if (!cmd) {
        spdlog::warn("Attempted to add null command to stack");
        return;
    }

    spdlog::debug("Adding command to stack without executing: {}", cmd->getDescription());

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
        // Push back onto undo stack so the command isn't lost.
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
        // Push back onto redo stack so the command isn't lost.  Batch redo
        // stops on the first failure, so this won't cause an infinite loop.
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
    for (auto it = _undoStack.rbegin(); it != _undoStack.rend(); ++it) {
        std::string desc = (*it)->getDescription();
        gint64 nodeId = (*it)->getNodeId();
        if (nodeId > 0 && (desc.empty() || desc[0] != '[')) {
            desc = "[" + std::to_string(nodeId) + "] " + desc;
        }
        descriptions.push_back(std::move(desc));
    }
    return descriptions;
}

std::vector<std::string> CtCommandManager::getRedoStackDescriptions() const
{
    std::vector<std::string> descriptions;
    descriptions.reserve(_redoStack.size());
    for (auto it = _redoStack.rbegin(); it != _redoStack.rend(); ++it) {
        std::string desc = (*it)->getDescription();
        gint64 nodeId = (*it)->getNodeId();
        if (nodeId > 0 && (desc.empty() || desc[0] != '[')) {
            desc = "[" + std::to_string(nodeId) + "] " + desc;
        }
        descriptions.push_back(std::move(desc));
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
        if (!undo()) break;  // Stop on first failure
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
        if (!redo()) break;  // Stop on first failure
    }
}

void CompoundCommand::collectNodeIds(std::set<gint64>& rNodeIds) const
{
    const gint64 nodeId = getNodeId();
    if (nodeId > 0) rNodeIds.insert(nodeId);
    for (const auto& pCommand : _commands) {
        if (pCommand) pCommand->collectNodeIds(rNodeIds);
    }
}

size_t CtCommandManager::purgeCommandsForNodes(const std::set<gint64>& nodeIds)
{
    if (nodeIds.empty()) return 0u;
    size_t numPurged{0u};
    auto f_touchesAny = [&nodeIds](const std::unique_ptr<CtCommand>& pCommand)->bool{
        if (not pCommand) return false;
        std::set<gint64> commandNodeIds;
        pCommand->collectNodeIds(commandNodeIds);
        for (const gint64 nodeId : commandNodeIds) {
            if (0u != nodeIds.count(nodeId)) return true;
        }
        return false;
    };
    for (std::deque<std::unique_ptr<CtCommand>>* pStack : {&_undoStack, &_redoStack}) {
        const size_t sizeBefore = pStack->size();
        pStack->erase(std::remove_if(pStack->begin(), pStack->end(), f_touchesAny), pStack->end());
        numPurged += sizeBefore - pStack->size();
    }
    if (numPurged > 0u) {
        spdlog::debug("CtCommandManager: purged {} commands touching {} protected nodes",
                      numPurged, nodeIds.size());
    }
    return numPurged;
}

void CtCommandManager::clear()
{
    spdlog::debug("Clearing command history");
    _undoStack.clear();
    _redoStack.clear();
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
        _undoStack.pop_front();
    }
}
