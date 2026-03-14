/*
 * ct_command.h
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

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <glibmm/ustring.h>

class CtDocumentModel;

// Base class for all commands in the command pattern
class CtCommand {
public:
    virtual ~CtCommand() = default;

    // Execute the command (performs the action)
    virtual void execute() = 0;

    // Undo the command (reverses the action)
    virtual void undo() = 0;

    // Redo the command (default: same as execute)
    virtual void redo() { execute(); }

    // Get human-readable description for debugging/UI
    virtual std::string getDescription() const = 0;

    // Node and cursor accessors — default to "unknown"; override in concrete commands
    virtual gint64 getNodeId() const { return -1; }
    virtual int getOldCursorPos() const { return -1; }
    virtual int getNewCursorPos() const { return -1; }

    // Scroll position for undo/redo restoration (-1.0 = not captured)
    virtual double getOldScrollPos() const { return -1.0; }
    virtual double getNewScrollPos() const { return -1.0; }
};

// Compound command that groups multiple commands together
// Useful for operations that should undo/redo as a single unit
class CompoundCommand : public CtCommand {
public:
    explicit CompoundCommand(const std::string& description);

    void addCommand(std::unique_ptr<CtCommand> cmd);
    void execute() override;
    void undo() override;
    void redo() override;
    std::string getDescription() const override;

    bool isEmpty() const { return _commands.empty(); }

    // Node and cursor tracking for bridge integration (used by lightweight text commands)
    void setDescription(const std::string& desc) { _description = desc; }
    void setNodeId(int64_t nodeId) { _nodeId = nodeId; }
    void setOldCursorPos(int pos) { _oldCursorPos = pos; }
    void setNewCursorPos(int pos) { _newCursorPos = pos; }
    void setOldScrollPos(double pos) { _oldScrollPos = pos; }
    void setNewScrollPos(double pos) { _newScrollPos = pos; }
    int64_t getNodeId() const override { return _nodeId; }
    int getOldCursorPos() const override { return _oldCursorPos; }
    int getNewCursorPos() const override { return _newCursorPos; }
    double getOldScrollPos() const override { return _oldScrollPos; }
    double getNewScrollPos() const override { return _newScrollPos; }

    // Document model for notification suppression during delta replay
    void setDocumentModel(std::shared_ptr<CtDocumentModel> model) { _docModel = model; }

private:
    std::vector<std::unique_ptr<CtCommand>> _commands;
    std::string _description;
    int64_t _nodeId{-1};
    int _oldCursorPos{-1};
    int _newCursorPos{-1};
    double _oldScrollPos{-1.0};
    double _newScrollPos{-1.0};
    std::shared_ptr<CtDocumentModel> _docModel;
};

// Manages undo/redo stacks and command execution
class CtCommandManager {
public:
    CtCommandManager();
    ~CtCommandManager();

    // Execute a command and add it to the undo stack
    void executeCommand(std::unique_ptr<CtCommand> cmd);

    // Add a command to the undo stack without executing it
    // Used for commands that represent changes that already happened
    void addCommandToStack(std::unique_ptr<CtCommand> cmd);

    // Undo the last command. Returns true on success, false if undo failed.
    bool undo();

    // Redo the last undone command. Returns true on success, false if redo failed.
    bool redo();

    // Check if undo/redo is available
    bool canUndo() const;
    bool canRedo() const;

    // Get description of next undo/redo action
    std::string getUndoDescription() const;
    std::string getRedoDescription() const;

    // Peek at the next command to be undone/redone (returns nullptr if stack is empty)
    CtCommand* peekUndoCommand() const;
    CtCommand* peekRedoCommand() const;

    // Get descriptions of all commands in the undo/redo stacks
    std::vector<std::string> getUndoStackDescriptions() const;
    std::vector<std::string> getRedoStackDescriptions() const;

    // Undo/Redo multiple commands at once
    void undo(size_t count);
    void redo(size_t count);

    // Command grouping
    void beginCommandGroup(const std::string& description);
    void endCommandGroup();

    // Clear all history
    void clear();

    // Set maximum undo stack depth (0 = unlimited)
    void setMaxUndoDepth(size_t depth);
    size_t getMaxUndoDepth() const { return _maxUndoDepth; }

private:
    void trimUndoStack();

    std::vector<std::unique_ptr<CtCommand>> _undoStack;
    std::vector<std::unique_ptr<CtCommand>> _redoStack;
    std::unique_ptr<CompoundCommand> _activeGroup;
    size_t _maxUndoDepth;
};
