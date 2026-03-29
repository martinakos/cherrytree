/*
 * ct_command_bridge.h
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
#include "ct_text_commands.h"
#include "ct_widget_commands.h"
#include "ct_node_content.h"
#include <gtkmm.h>
#include <memory>

// Forward declarations
class CtMainWin;
class CtTreeIter;
class CtAnchoredWidget;
class CtRichCell;

// High-level operation currently in progress.
// Replaces five individual boolean flags and makes illegal state combinations
// impossible at the type level.
enum class BridgeOp {
    None,            // idle, normal editing
    CapturingPaste,  // beginPaste..endPaste
    CapturingCut,    // beginCut..endCut
    CapturingFormat, // beginFormatChange..endFormatChange
    TrackingWidget,  // widget focus-in..focus-out
    ExecutingUndo,   // undo() scope
    ExecutingRedo,   // redo() scope
};

// Bridge class to integrate new command system with existing code
class CtCommandBridge {
public:
    CtCommandBridge(CtMainWin* pMainWin);
    ~CtCommandBridge();

    // Initialize the document model from existing tree
    void initializeFromExistingDocument();

    // Synchronize model with current GTK tree state
    void syncModelFromTree();

    // Synchronize GTK tree with model state
    void syncTreeFromModel();

    // Get the command manager
    CtCommandManager& getCommandManager() { return _commandManager; }

    // Helper: Execute a command and add to undo stack
    void executeCommand(std::unique_ptr<CtCommand> cmd);

    // Helper: Add command to undo stack without executing (for operations already done)
    void addCommandToStack(std::unique_ptr<CtCommand> cmd);

    // Undo/Redo operations
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // Multi-step undo/redo
    void undo(size_t count);
    void redo(size_t count);

    // Get command stack information
    std::vector<std::string> getUndoStackDescriptions() const;
    std::vector<std::string> getRedoStackDescriptions() const;

    // Text editing helpers
    void beginTextEditSession(gint64 nodeId);
    void endTextEditSession();
    void cancelTextEditSession();

    // Format change helpers (captures before/after for undo)
    void beginFormatChange(gint64 nodeId, const std::string& formatType);
    void endFormatChange();

    // Apply formatting via model-first path
    void applyFormatV2(
        gint64 nodeId,
        int selStart,
        int selEnd,
        const std::string& attribute,
        const std::string& value,
        int oldCursorPos,
        int newCursorPos);

    // Paste helpers (captures before/after for undo).
    // pasteContainsWidgets=false: use session-based delta path (same as cut).
    // pasteContainsWidgets=true (default): use XML snapshot path.
    void beginPaste(gint64 nodeId, bool pasteContainsWidgets = true);
    void endPaste();

    // Cut helpers (captures before/after for undo)
    void beginCut(gint64 nodeId);
    void endCut();

    // Check if we're in a special operation that should suppress normal text tracking
    bool isSuppressingTextEdits() const {
        return _currentOp == BridgeOp::CapturingPaste
            || _currentOp == BridgeOp::CapturingCut
            || _currentOp == BridgeOp::CapturingFormat;
    }

    // Check if we're inside an undo or redo operation
    bool isInUndoRedo() const {
        return _currentOp == BridgeOp::ExecutingUndo
            || _currentOp == BridgeOp::ExecutingRedo;
    }

    // Widget edit tracking.
    // widget: the specific widget gaining focus (nullptr = unknown, uses XML path).
    //         For CtCodebox: lightweight delta path (text only).
    //         For CtTableHeavy/CtTableLight with row/col: lightweight delta path (cell text).
    //         For unknown widgets or tables without row/col: XML snapshot fallback.
    void beginWidgetEdit(gint64 nodeId, CtAnchoredWidget* widget = nullptr, int row = -1, int col = -1);
    void endWidgetEdit();

    // RT-4: Rich cell format support.
    // Returns true when the tracked widget is a CtTableRich cell.
    bool isTrackingRichCell() const;
    // Returns the GTK buffer of the active rich cell (empty RefPtr if none).
    Glib::RefPtr<Gtk::TextBuffer> getActiveRichCellBuffer();
    // Returns the active CtRichCell pointer, or nullptr if not in a rich cell.
    CtRichCell* getActiveRichCellPtr() const;
    // Call after applying a format tag to the active rich cell's GTK buffer.
    // Captures old→new cell content delta and pushes an EditRichCellCommand.
    void commitRichCellFormatChange(std::string description = "Format text");

    // Flush the rich cell edit session: end the current session, create an
    // EditRichCellCommand with a description built from captured sub-commands
    // (matching main-page undo granularity), and start a new session.
    void flushRichCellSession();

    // Cancel the active rich cell session without creating a command.
    // Use before bulk buffer modifications (e.g. paste) so individual insert
    // signals aren't captured as separate undo entries.
    void cancelRichCellSession();

    // Widget operation helpers
    Glib::ustring getCurrentBufferXml();

    // Commit a widget structural modification: create ModifyWidgetDeltaCommand and sync model.
    // Call AFTER modifying the live GTK widget. oldDesc must have been captured BEFORE modification.
    void commitWidgetModification(
        gint64 nodeId,
        int charOffset,
        const CtWidgetDesc& oldDesc,
        const CtWidgetDesc& newDesc,
        const std::string& description);

    // Register a newly created node with the document model
    void registerNewNode(gint64 nodeId, gint64 parentId);

    // Reset all state for a new document (clears undo stacks, model, active session).
    // Must be called whenever a new file is opened so stale undo entries are discarded.
    void resetForNewDocument();

    // Check if bridge is active (model is initialized)
    bool isActive() const { return _active; }

    // Enable/disable the bridge
    void setActive(bool active) { _active = active; }

    // Access to document model (for testing)
    std::shared_ptr<CtDocumentModel> getDocumentModel() const { return _docModel; }

    // Helper to get XML content from GTK buffer (public for CtTextEditSession)
    Glib::ustring getBufferContentAsXml(Glib::RefPtr<Gtk::TextBuffer> buffer, const CtTreeIter* treeIter = nullptr);

private:

    // Helper to update buffer from XML
    void updateBufferFromXml(Glib::RefPtr<Gtk::TextBuffer> buffer, const Glib::ustring& xml, const std::string& syntax = "custom-colors", const CtTreeIter* treeIter = nullptr);

    // Shared implementation for paste/cut begin: flush widget edit, guard against re-entry,
    // snapshot initial XML.
    void beginXmlCapture(BridgeOp op, gint64 nodeId);

    // Shared implementation for paste/cut end: diff initial vs final, create command.
    void endXmlCapture(const std::string& description);

    // Observer implementation for model changes
    class BridgeObserver : public CtDocumentObserver {
    public:
        BridgeObserver(CtCommandBridge* bridge) : _bridge(bridge) {}

        void onNodeChanged(gint64 nodeId) override;
        void onNodeAdded(gint64 nodeId, gint64 parentId) override;
        void onNodeDeleted(gint64 nodeId) override;
        void onNodeMoved(gint64 nodeId, gint64 newParentId, int newPosition) override;
        void onTreeStructureChanged() override;

    private:
        // Named helper replacing the inline lambda in onNodeChanged
        void buildBufferForNode(gint64 nodeId,
                                const CtNodeContent& content,
                                const Glib::RefPtr<Gtk::TextBuffer>& buf,
                                CtTreeIter& iter,
                                bool attachToView = true);

        CtCommandBridge* _bridge;
    };

    CtMainWin* _pMainWin;
    std::shared_ptr<CtDocumentModel> _docModel;
    CtCommandManager _commandManager;
    std::unique_ptr<CtTextEditSession> _editSession;
    std::unique_ptr<CtTextEditSession> _richCellSession;  // per-word signal capture on rich cell buffer
    std::unique_ptr<BridgeObserver> _observer;
    bool _active;

    // Current high-level operation (replaces five individual booleans)
    BridgeOp _currentOp{BridgeOp::None};

    // True only while a command's execute/undo/redo runs (so the observer
    // can distinguish replay from the gap before session restart).
    bool _inCommandExecution{false};

    // Format change tracking
    gint64 _formatChangeNodeId{0};
    std::string _captureFormatType;
    int _formatChangeOldCursorPos{-1};
    int _formatChangeNewCursorPos{-1};

    // Widget edit tracking (focus-in / focus-out snapshot approach)
    // _widgetEditNodeId != 0 serves as the "tracking" guard (replaces _trackingWidgetEdit)
    gint64 _widgetEditNodeId{0};
    Glib::ustring _widgetEditInitialXml;
    int _widgetEditOldCursorPos{-1};
    double _widgetEditOldScrollPos{-1.0};
    // Widget delta path (active when _widgetEditCharOffset != -1)
    // _widgetEditRow >= 0 means table delta (vs codebox delta)
    int _widgetEditCharOffset{-1};
    int _widgetEditRow{-1};
    int _widgetEditCol{-1};
    std::string _widgetEditOldContent;       // codebox: UTF-8 text; plain table cell: UTF-8 raw bytes
    CtCellContent _widgetEditOldCellContent; // rich table cell: structured content snapshot
    bool _widgetEditIsRichCell{false};       // true when the tracked widget is a CtTableRich cell

    // Unified paste/cut capture fields (shared between CapturingPaste and CapturingCut)
    gint64 _captureNodeId{0};
    Glib::ustring _captureInitialXml;
    int _captureOldCursorPos{-1};
    int _captureNewCursorPos{-1};

    // Cursor position restoration after undo/redo (consumed by observer in onNodeChanged)
    int _pendingCursorPos{-1};

    // Scroll position captured at beginTextEditSession (before-edit state)
    double _sessionScrollPosOld{-1.0};

    // Generation counter to distinguish separate undo/redo operations
    int _undoRedoGeneration{0};

    // When true, the next beginTextEditSession skips the buffer→model re-sync.
    bool _skipNextModelSync{false};

    // Node ID whose model was last kept in sync by a clean session end.
    // When beginTextEditSession is called for the same node, the expensive
    // getBufferContentAsXml re-sync is skipped because delta commands already
    // maintained the model.  Reset to -1 whenever sync can't be guaranteed
    // (widget edits, paste/cut, format changes, node switches).
    gint64 _lastSyncedNodeId{-1};
};
