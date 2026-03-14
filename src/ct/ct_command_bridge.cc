/*
 * ct_command_bridge.cc
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

#include "ct_command_bridge.h"
#include "ct_main_win.h"
#include "ct_storage_xml.h"
#include "ct_logging.h"
#include "ct_gtk_compat.h"
#include "ct_text_commands.h"
#include "ct_widget_commands.h"
#include <libxml++/libxml++.h>
#include <functional>
#include <chrono>
#include <set>

// CtCommandBridge implementation

CtCommandBridge::CtCommandBridge(CtMainWin* pMainWin)
    : _pMainWin(pMainWin)
    , _active(false)
{
    spdlog::debug("CtCommandBridge: initializing");

    // Create document model
    _docModel = std::make_shared<CtDocumentModel>();

    // Create observer
    _observer = std::make_unique<BridgeObserver>(this);
    _docModel->addObserver(_observer.get());

    // Create edit session and link it back to this bridge
    _editSession = std::make_unique<CtTextEditSession>(_docModel);
    _editSession->setBridge(this);

    spdlog::info("CtCommandBridge: initialized (inactive by default)");
}

CtCommandBridge::~CtCommandBridge()
{
    spdlog::debug("CtCommandBridge: destroying");

    if (_docModel && _observer) {
        _docModel->removeObserver(_observer.get());
    }
}

void CtCommandBridge::initializeFromExistingDocument()
{
    if (!_pMainWin) {
        spdlog::error("CtCommandBridge: no main window");
        return;
    }

    spdlog::info("CtCommandBridge: initializing model from existing document");

    // TODO: Traverse existing GTK tree and populate model
    // This would be implemented when actually wiring up to existing code
    // For now, this is a placeholder

    spdlog::warn("CtCommandBridge: initializeFromExistingDocument not yet fully implemented");
}

void CtCommandBridge::syncModelFromTree()
{
    if (!_active) {
        return;
    }

    if (!_pMainWin) {
        spdlog::error("CtCommandBridge: no main window for tree sync");
        return;
    }

    spdlog::info("CtCommandBridge: synchronizing model from tree");

    // Reset model but preserve root node and shared_ptr identity
    _docModel->removeObserver(_observer.get());
    _docModel->reset();
    _docModel->addObserver(_observer.get());

    // Traverse tree and populate model
    auto& treeStore = _pMainWin->get_tree_store();
    auto iter = treeStore.get_iter_first();

    if (!iter) {
        spdlog::debug("CtCommandBridge: tree is empty, nothing to sync");
        return;
    }

    // Recursive helper to add nodes
    std::function<void(Gtk::TreeModel::iterator, gint64)> addNodeRecursive;
    addNodeRecursive = [&](Gtk::TreeModel::iterator gtkIter, gint64 parentId) {
        if (!gtkIter) return;

        CtTreeIter treeIter = treeStore.to_ct_tree_iter(gtkIter);
        if (!treeIter) return;

        // Get node data
        gint64 nodeId = treeIter.get_node_id();
        Glib::ustring nodeName = treeIter.get_node_name();
        std::string syntax = treeIter.get_node_syntax_highlighting();

        // Get buffer content as XML
        auto buffer = treeIter.get_node_text_buffer();
        Glib::ustring contentXml;
        if (buffer) {
            contentXml = getBufferContentAsXml(buffer, &treeIter);
        }

        // Create node model
        auto nodeModel = std::make_shared<CtNodeModel>(nodeId);
        nodeModel->setName(nodeName);
        nodeModel->setContentXml(contentXml);
        nodeModel->setSyntax(syntax);

        // Add to document model
        bool added = _docModel->addNode(nodeModel, parentId, -1);
        if (added) {
            spdlog::debug("CtCommandBridge: synced node {} '{}' (parent: {})",
                         nodeId, nodeName.c_str(), parentId);
        } else {
            spdlog::error("CtCommandBridge: failed to sync node {} '{}' (parent: {})",
                         nodeId, nodeName.c_str(), parentId);
            return; // Don't try to add children if parent failed
        }

        // Recursively add children
        auto gtkChildren = gtkIter->children();
        if (!gtkChildren.empty()) {
            for (auto child : gtkChildren) {
                addNodeRecursive(child, nodeId);
            }
        }
    };

    // Add all root nodes - iterate through GTK tree model
    // Parent ID 0 is the virtual root node in the document model
    auto gtkStore = treeStore.get_store();
    if (gtkStore) {
        Gtk::TreeModel::Children roots = gtkStore->children();
        for (auto rootIter : roots) {
            addNodeRecursive(rootIter, 0);
        }
    }

    spdlog::info("CtCommandBridge: model sync complete");
}

void CtCommandBridge::syncTreeFromModel()
{
    if (!_active) {
        return;
    }

    // TODO: Sync GTK tree with model state
    spdlog::debug("CtCommandBridge: syncTreeFromModel called");
}

void CtCommandBridge::resetForNewDocument()
{
    spdlog::info("CtCommandBridge: resetting for new document");

    // Cancel any in-progress session so it doesn't try to commit stale data.
    if (_editSession && _editSession->isActive()) {
        _editSession->cancel();
    }

    // Clear all undo/redo history — it belongs to the previous document.
    _commandManager.clear();

    // Drop all node models — they belong to the previous document.
    // Nodes for the new document will be added lazily on first edit visit.
    _docModel->removeObserver(_observer.get());
    _docModel->reset();
    _docModel->addObserver(_observer.get());

    // Reset transient operation state.
    _currentOp = BridgeOp::None;
    _inCommandExecution = false;
    _skipNextModelSync = false;
    _widgetEditNodeId = 0;
    _captureNodeId = 0;
    _formatChangeNodeId = 0;
}

void CtCommandBridge::executeCommand(std::unique_ptr<CtCommand> cmd)
{
    if (!_active) {
        spdlog::debug("CtCommandBridge: not active, command not executed");
        return;
    }

    if (!cmd) {
        spdlog::warn("CtCommandBridge: attempted to execute null command");
        return;
    }

    // Note: We don't log command descriptions here to avoid expensive XML parsing on every keystroke
    // Command descriptions are generated on-demand when building undo/redo menus
    _commandManager.executeCommand(std::move(cmd));
    // Note: Menu updates happen on-demand via signal_show_menu() to avoid performance issues
}

void CtCommandBridge::addCommandToStack(std::unique_ptr<CtCommand> cmd)
{
    if (!_active) {
        spdlog::debug("CtCommandBridge: not active, command not added to stack");
        return;
    }

    if (!cmd) {
        spdlog::warn("CtCommandBridge: attempted to add null command to stack");
        return;
    }

    // Note: We don't log command descriptions here to avoid expensive XML parsing on every keystroke
    // Command descriptions are generated on-demand when building undo/redo menus
    _commandManager.addCommandToStack(std::move(cmd));
    // Note: Menu updates happen on-demand via signal_show_menu() to avoid performance issues
}

void CtCommandBridge::undo()
{
    if (!_active) {
        spdlog::debug("CtCommandBridge: not active, undo skipped");
        return;
    }

    // Reentrancy guard - prevent nested undo/redo calls during GTK event processing
    if (isInUndoRedo()) {
        spdlog::warn("CtCommandBridge: undo called while another undo/redo is in progress, ignoring");
        return;
    }
    _currentOp = BridgeOp::ExecutingUndo;

    // Flush any active widget edit before undo so it becomes a distinct undo entry
    if (_widgetEditNodeId != 0) {
        endWidgetEdit();
    }

    // Cancel any active edit session before undo
    if (_editSession && _editSession->isActive()) {
        _editSession->cancel();
    }

    // Get the node ID and cursor position from command being undone
    auto cmd = _commandManager.peekUndoCommand();
    if (!cmd) {
        spdlog::debug("CtCommandBridge: no command to undo");
        _currentOp = BridgeOp::None;
        return;
    }

    gint64 affectedNodeId = cmd->getNodeId();
    _pendingCursorPos = cmd->getOldCursorPos();

    // Switch to the affected node if it's different from current
    auto curr_iter = _pMainWin->curr_tree_iter();
    if (affectedNodeId != -1 && curr_iter && curr_iter.get_node_id() != affectedNodeId) {
        auto& treeStore = _pMainWin->get_tree_store();
        auto affectedIter = treeStore.get_node_from_node_id(affectedNodeId);
        if (affectedIter) {
            _pMainWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(affectedIter));
        }
    }

    _inCommandExecution = true;
    _undoRedoGeneration++;

    bool undoSuccess = false;
    try {
        undoSuccess = _commandManager.undo();
    } catch (const std::exception& e) {
        spdlog::error("CtCommandBridge: exception during undo: {}", e.what());
        _inCommandExecution = false;
        _currentOp = BridgeOp::None;
        return;
    } catch (...) {
        spdlog::error("CtCommandBridge: unknown exception during undo");
        _inCommandExecution = false;
        _currentOp = BridgeOp::None;
        return;
    }
    _inCommandExecution = false;

    // Clear the in-progress op BEFORE restarting the session so that
    // beginTextEditSession will actually connect signals and re-sync the model.
    _currentOp = BridgeOp::None;

    // Only skip re-sync when undo succeeded — on failure the model may be
    // out of sync with the buffer, so we need the re-sync to recover.
    if (undoSuccess) {
        _skipNextModelSync = true;
    }

    // Restart edit session after undo with the affected node
    if (affectedNodeId != -1) {
        beginTextEditSession(affectedNodeId);
    } else {
        curr_iter = _pMainWin->curr_tree_iter();
        if (curr_iter) {
            beginTextEditSession(curr_iter.get_node_id());
        }
    }

    // Scroll to the cursor position that was just restored by the observer.
    // Using scroll_to(insert mark) is more reliable than set_value() because it
    // participates in GTK's layout cycle — set_value() races with deferred widget
    // size allocation that can override the scroll position.
    if (auto buf = _pMainWin->curr_buffer()) {
        _pMainWin->get_text_view().mm().scroll_to(
            buf->get_insert(), CtTextView::TEXT_SCROLL_MARGIN);
    }

    // After scroll_to, anchored widgets in the newly-visible area may not have
    // received an expose event yet (they render black). Schedule a full redraw
    // via idle callback so it runs after the scroll and layout are complete.
    if (not _pMainWin->no_gui()) {
        Glib::signal_idle().connect_once([pMainWin = _pMainWin](){
            pMainWin->get_text_view().mm().queue_draw();
        });
    }
}

void CtCommandBridge::redo()
{
    if (!_active) {
        spdlog::debug("CtCommandBridge: not active, redo skipped");
        return;
    }

    // Reentrancy guard - prevent nested undo/redo calls during GTK event processing
    if (isInUndoRedo()) {
        spdlog::warn("CtCommandBridge: redo called while another undo/redo is in progress, ignoring");
        return;
    }
    _currentOp = BridgeOp::ExecutingRedo;

    // Flush any active widget edit before redo so it becomes a distinct undo entry
    if (_widgetEditNodeId != 0) {
        endWidgetEdit();
    }

    // Cancel any active edit session before redo
    if (_editSession && _editSession->isActive()) {
        _editSession->cancel();
    }

    // Get the node ID and cursor position from command being redone
    auto cmd = _commandManager.peekRedoCommand();
    if (!cmd) {
        spdlog::debug("CtCommandBridge: no command to redo");
        _currentOp = BridgeOp::None;
        return;
    }

    gint64 affectedNodeId = cmd->getNodeId();
    _pendingCursorPos = cmd->getNewCursorPos();

    // Switch to the affected node if it's different from current
    auto curr_iter = _pMainWin->curr_tree_iter();
    if (affectedNodeId != -1 && curr_iter && curr_iter.get_node_id() != affectedNodeId) {
        auto& treeStore = _pMainWin->get_tree_store();
        auto affectedIter = treeStore.get_node_from_node_id(affectedNodeId);
        if (affectedIter) {
            _pMainWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(affectedIter));
        }
    }

    _inCommandExecution = true;
    _undoRedoGeneration++;

    bool redoSuccess = false;
    try {
        redoSuccess = _commandManager.redo();
    } catch (const std::exception& e) {
        spdlog::error("CtCommandBridge: exception during redo: {}", e.what());
        _inCommandExecution = false;
        _currentOp = BridgeOp::None;
        return;
    } catch (...) {
        spdlog::error("CtCommandBridge: unknown exception during redo");
        _inCommandExecution = false;
        _currentOp = BridgeOp::None;
        return;
    }
    _inCommandExecution = false;

    // Clear the in-progress op BEFORE restarting the session so that
    // beginTextEditSession will actually connect signals and re-sync the model.
    _currentOp = BridgeOp::None;

    // Only skip re-sync when redo succeeded — on failure the model may be
    // out of sync with the buffer, so we need the re-sync to recover.
    if (redoSuccess) {
        _skipNextModelSync = true;
    }

    // Restart edit session after redo with the affected node
    if (affectedNodeId != -1) {
        beginTextEditSession(affectedNodeId);
    } else {
        curr_iter = _pMainWin->curr_tree_iter();
        if (curr_iter) {
            beginTextEditSession(curr_iter.get_node_id());
        }
    }

    // Scroll to cursor — see undo() for rationale.
    if (auto buf = _pMainWin->curr_buffer()) {
        _pMainWin->get_text_view().mm().scroll_to(
            buf->get_insert(), CtTextView::TEXT_SCROLL_MARGIN);
    }

    // Redraw after scroll — see undo() for rationale.
    if (not _pMainWin->no_gui()) {
        Glib::signal_idle().connect_once([pMainWin = _pMainWin](){
            pMainWin->get_text_view().mm().queue_draw();
        });
    }
}

bool CtCommandBridge::canUndo() const
{
    if (!_active) {
        return false;
    }
    return _commandManager.canUndo();
}

bool CtCommandBridge::canRedo() const
{
    if (!_active) {
        return false;
    }
    return _commandManager.canRedo();
}

void CtCommandBridge::undo(size_t count)
{
    if (!_active || count == 0) {
        return;
    }

    // Limit count to available undo stack size
    size_t actualCount = std::min(count, _commandManager.getUndoStackDescriptions().size());

    spdlog::info("CtCommandBridge: undoing {} command(s)", actualCount);

    // Call single-step undo() for each operation to ensure proper UI updates
    for (size_t i = 0; i < actualCount; ++i) {
        undo();  // Call the single-step undo() method
    }

    // Update undo/redo menus after all operations complete
    _pMainWin->menu_update_undo_redo_menus();
}

void CtCommandBridge::redo(size_t count)
{
    if (!_active || count == 0) {
        return;
    }

    // Limit count to available redo stack size
    size_t actualCount = std::min(count, _commandManager.getRedoStackDescriptions().size());

    spdlog::info("CtCommandBridge: redoing {} command(s)", actualCount);

    // Call single-step redo() for each operation to ensure proper UI updates
    for (size_t i = 0; i < actualCount; ++i) {
        redo();  // Call the single-step redo() method
    }

    // Update undo/redo menus after all operations complete
    _pMainWin->menu_update_undo_redo_menus();
}

std::vector<std::string> CtCommandBridge::getUndoStackDescriptions() const
{
    if (!_active) {
        return {};
    }
    return _commandManager.getUndoStackDescriptions();
}

std::vector<std::string> CtCommandBridge::getRedoStackDescriptions() const
{
    if (!_active) {
        return {};
    }
    return _commandManager.getRedoStackDescriptions();
}

void CtCommandBridge::beginTextEditSession(gint64 nodeId)
{
    if (!_active) {
        return;
    }

    // Don't start text edit sessions during paste, cut, or format operations
    if (isSuppressingTextEdits()) {
        spdlog::debug("CtCommandBridge: suppressing text edit session (special operation in progress)");
        return;
    }

    if (!_editSession) {
        spdlog::error("CtCommandBridge: no edit session");
        return;
    }

    spdlog::debug("CtCommandBridge: beginning text edit session for node {}", nodeId);

    // Get the buffer for the specific node we're starting a session for
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: could not find tree iter for node {}", nodeId);
        return;
    }

    auto buffer = treeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no buffer for node {}", nodeId);
        return;
    }

    spdlog::debug("CtCommandBridge: node {} buffer address = {}", nodeId, static_cast<void*>(buffer.get()));

    // Check if node has embedded widgets (tables, images, codeboxes)
    // Widget nodes must use the XML-based undo path
    bool hasWidgets = !treeIter.get_anchored_widgets().empty();
    spdlog::debug("CtCommandBridge: node {} hasWidgets={}", nodeId, hasWidgets);

    // Re-sync the node model from the buffer to ensure model matches buffer.
    // This handles cases where the buffer was modified without an active session
    // (e.g., widget insertion, key events not captured by session).
    // After undo/redo, skip this — the model is the source of truth and the buffer
    // was rebuilt from it. Re-syncing would overwrite the correct model state.
    // Also skip if the model already has xmlBackup — it already has good XML and
    // getBufferContentAsXml cannot fully reconstruct complex widgets like tables.
    if (_skipNextModelSync) {
        _skipNextModelSync = false;
        // Defensive: verify model and buffer agree on content length.
        // If they diverge (e.g. a prior undo/redo partially failed), force re-sync
        // so the model recovers rather than staying permanently out of sync.
        auto node = _docModel->getNodeById(nodeId);
        int bufferLen = buffer->get_char_count();
        int modelLen = node ? static_cast<int>(node->getContent().length()) : -1;
        if (node && modelLen != bufferLen) {
            spdlog::warn("CtCommandBridge: model/buffer length mismatch for node {} "
                         "(model={}, buffer={}) — forcing re-sync", nodeId, modelLen, bufferLen);
            Glib::ustring currentXml = getBufferContentAsXml(buffer, &treeIter);
            node->setContentXml(currentXml);
        } else {
            spdlog::info("CtCommandBridge: SKIPPING model re-sync after undo/redo for node {}", nodeId);
        }
    } else {
        auto node = _docModel->getNodeById(nodeId);
        if (!node) {
            // Lazy: first visit to this node — add it to the model now (only this one node,
            // not all nodes). This replaces the old eager syncModelFromTree() call.
            auto newNode = std::make_shared<CtNodeModel>(nodeId);
            newNode->setName(treeIter.get_node_name());
            newNode->setSyntax(treeIter.get_node_syntax_highlighting());
            newNode->setContentXml(getBufferContentAsXml(buffer, &treeIter));
            _docModel->addNode(newNode, 0 /* flat model — no hierarchy needed for undo */);
            spdlog::info("CtCommandBridge: lazy-added node {} to model on first edit visit", nodeId);
        } else if (buffer->get_modified()) {
            // Only re-sync when the buffer has been modified since it was last loaded/synced.
            // Skipping this on unmodified buffers avoids expensive XML serialization on
            // every node click, which was the main cause of slow loading for large files.
            spdlog::info("CtCommandBridge: re-syncing model from buffer for node {} (buffer modified)", nodeId);
            Glib::ustring currentXml = getBufferContentAsXml(buffer, &treeIter);
            node->setContentXml(currentXml);
        }
    }

    // Capture scroll position at session start so it can be restored on undo
    {
        auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
        _sessionScrollPosOld = adj ? adj->get_value() : -1.0;
    }

    // Begin signal-based capture, passing treeIter to capture initial XML from buffer
    _editSession->begin(nodeId, buffer, hasWidgets, &treeIter);

    // During undo/redo, suppress signal capture so the buffer rebuild from the
    // observer doesn't modify the model via onBufferInsert/onBufferErase.
    if (isInUndoRedo()) {
        _editSession->setSuppressCapture(true);
    }
}

void CtCommandBridge::endTextEditSession()
{
    if (!_active) {
        return;
    }

    if (!_editSession || !_editSession->isActive()) {
        spdlog::debug("CtCommandBridge: no active edit session to end");
        return;
    }

    gint64 nodeId = _editSession->getActiveNodeId();
    auto node = _docModel->getNodeById(nodeId);
    if (!node) {
        spdlog::error("CtCommandBridge: node {} not found", nodeId);
        _editSession->cancel();
        return;
    }

    spdlog::debug("CtCommandBridge: ending text edit session for node {}", nodeId);

    // Get the buffer for the SESSION's node, not curr_buffer (which may be a different node)
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter sessionTreeIter = treeStore.get_node_from_node_id(nodeId);
    if (!sessionTreeIter) {
        spdlog::error("CtCommandBridge: could not find tree iter for session node {}", nodeId);
        _editSession->cancel();
        return;
    }

    auto buffer = sessionTreeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no buffer for session node {}", nodeId);
        _editSession->cancel();
        return;
    }

    // Get widgets from the node
    auto widgets = sessionTreeIter.get_anchored_widgets();

    // Get current cursor position
    int cursorPos = buffer->get_insert()->get_iter().get_offset();

    // End session and get command — always a CompoundCommand of delta commands
    auto cmd = _editSession->end(buffer, widgets, cursorPos);
    if (cmd) {
        spdlog::info("CtCommandBridge: session created command, adding to undo stack");

        // Stamp scroll positions so undo/redo can restore the viewport
        if (auto* cc = dynamic_cast<CompoundCommand*>(cmd.get())) {
            double scrollPosNew = -1.0;
            auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
            if (adj) scrollPosNew = adj->get_value();
            cc->setOldScrollPos(_sessionScrollPosOld);
            cc->setNewScrollPos(scrollPosNew);
        }

        // Model was kept in sync during session via signal handlers for all node types
        // Delta commands provide full undo/redo without needing XML snapshots
        _commandManager.addCommandToStack(std::move(cmd));
    } else {
        spdlog::debug("CtCommandBridge: session ended but no command created (no changes)");
    }
}

void CtCommandBridge::cancelTextEditSession()
{
    if (!_active) {
        return;
    }

    if (_editSession) {
        _editSession->cancel();
    }
}

void CtCommandBridge::beginWidgetEdit(gint64 nodeId, CtAnchoredWidget* widget, int row, int col)
{
    if (!_active || !_pMainWin) {
        return;
    }

    // Don't start widget tracking during paste/cut/format/undo/redo
    // (prevents spurious GTK focus events from corrupting the enum state)
    if (isSuppressingTextEdits() || isInUndoRedo()) {
        return;
    }

    // Flush any active text edit first so it gets its own undo entry
    if (_editSession && _editSession->isActive()) {
        endTextEditSession();
    }

    // If we're already tracking a different node, flush it first
    if (_widgetEditNodeId != 0 && _widgetEditNodeId != nodeId) {
        endWidgetEdit();
    }

    // Already tracking this node.
    // For table delta path: if the cell changed, flush the old cell and re-capture.
    // (The XML path captured the whole node so cell changes didn't matter, but the
    //  delta path tracks a single cell.)
    if (_widgetEditNodeId != 0) {
        if (_widgetEditRow >= 0 && (row != _widgetEditRow || col != _widgetEditCol)) {
            endWidgetEdit();
            // Fall through to re-capture for the new cell.
        } else {
            return;
        }
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: beginWidgetEdit - node {} not found", nodeId);
        return;
    }

    auto buffer = treeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: beginWidgetEdit - no buffer for node {}", nodeId);
        return;
    }

    _currentOp = BridgeOp::TrackingWidget;
    _widgetEditNodeId = nodeId;
    _widgetEditOldCursorPos = buffer->get_insert()->get_iter().get_offset();
    {
        auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
        _widgetEditOldScrollPos = adj ? adj->get_value() : -1.0;
    }

    // For CtCodebox: lightweight delta (text only).
    // For tables with row/col: lightweight delta (single cell text).
    // For unknown widgets or tables without row/col: XML snapshot fallback.
    if (widget && widget->get_type() == CtAnchWidgType::CodeBox) {
        auto* codebox = static_cast<CtCodebox*>(widget);
        _widgetEditCharOffset = widget->getOffset();
        _widgetEditRow = -1;
        _widgetEditCol = -1;
        _widgetEditOldContent = codebox->get_text_content().raw();
        // Capture initial XML as a fallback in case setWidgetContentData fails later.
        _widgetEditInitialXml = getBufferContentAsXml(buffer, &treeIter);
        spdlog::debug("CtCommandBridge: beginWidgetEdit (delta path) codebox at offset {}",
                      _widgetEditCharOffset);
    } else if (widget && row >= 0 && col >= 0 &&
               (widget->get_type() == CtAnchWidgType::TableLight ||
                widget->get_type() == CtAnchWidgType::TableHeavy)) {
        _widgetEditCharOffset = widget->getOffset();
        _widgetEditRow = row;
        _widgetEditCol = col;
        if (widget->get_type() == CtAnchWidgType::TableLight) {
            _widgetEditOldContent = static_cast<CtTableLight*>(widget)->get_cell_text(row, col).raw();
        } else {
            auto buf = static_cast<CtTableHeavy*>(widget)->get_buffer(row, col);
            _widgetEditOldContent = buf ? buf->get_text().raw() : std::string{};
        }
        // Capture initial XML as a fallback.
        _widgetEditInitialXml = getBufferContentAsXml(buffer, &treeIter);
        spdlog::debug("CtCommandBridge: beginWidgetEdit (delta path) table cell ({},{}) at offset {}",
                      row, col, _widgetEditCharOffset);
    } else {
        _widgetEditCharOffset = -1;
        _widgetEditRow = -1;
        _widgetEditCol = -1;
        _widgetEditOldContent.clear();
        _widgetEditInitialXml = getBufferContentAsXml(buffer, &treeIter);
    }
}

void CtCommandBridge::endWidgetEdit()
{
    if (_widgetEditNodeId == 0 || !_active || !_pMainWin) {
        return;
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_widgetEditNodeId);

    auto clearState = [&]() {
        _widgetEditNodeId = 0;
        _widgetEditInitialXml.clear();
        _widgetEditOldCursorPos = -1;
        _widgetEditOldScrollPos = -1.0;
        _widgetEditCharOffset = -1;
        _widgetEditRow = -1;
        _widgetEditCol = -1;
        _widgetEditOldContent.clear();
    };

    if (!treeIter) {
        spdlog::error("CtCommandBridge: endWidgetEdit - node {} not found", _widgetEditNodeId);
        clearState();
        return;
    }

    auto buffer = treeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: endWidgetEdit - no buffer for node {}", _widgetEditNodeId);
        clearState();
        return;
    }

    int newCursorPos = buffer->get_insert()->get_iter().get_offset();

    // Save state before clearState() zeroes it
    const gint64 savedNodeId = _widgetEditNodeId;
    const int savedCharOffset = _widgetEditCharOffset;
    const int savedRow = _widgetEditRow;
    const int savedCol = _widgetEditCol;

    // Delta path: find the widget by stored char offset and compare content.
    if (savedCharOffset != -1) {
        auto widgetList = treeIter.get_anchored_widgets();

        if (savedRow >= 0) {
            // Table delta path: compare single cell text.
            Glib::ustring newCellText;
            bool foundWidget = false;
            for (auto* w : widgetList) {
                if (w->getOffset() == savedCharOffset) {
                    if (w->get_type() == CtAnchWidgType::TableLight) {
                        newCellText = static_cast<CtTableLight*>(w)->get_cell_text(savedRow, savedCol);
                        foundWidget = true;
                    } else if (w->get_type() == CtAnchWidgType::TableHeavy) {
                        auto buf = static_cast<CtTableHeavy*>(w)->get_buffer(savedRow, savedCol);
                        newCellText = buf ? buf->get_text() : Glib::ustring{};
                        foundWidget = true;
                    }
                    break;
                }
            }

            Glib::ustring oldCellText(_widgetEditOldContent);
            if (foundWidget && newCellText != oldCellText) {
                auto node = _docModel->getNodeById(_widgetEditNodeId);
                if (node && node->getContent().setWidgetTableCell(
                        savedCharOffset, (size_t)savedRow, (size_t)savedCol, newCellText)) {
                    auto cmd = std::make_unique<EditTableCellCommand>(
                        _docModel,
                        _widgetEditNodeId,
                        savedCharOffset,
                        (size_t)savedRow,
                        (size_t)savedCol,
                        oldCellText,
                        newCellText,
                        _widgetEditOldCursorPos,
                        newCursorPos
                    );
                    double scrollPosNew = -1.0;
                    auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
                    if (adj) scrollPosNew = adj->get_value();
                    cmd->setOldScrollPos(_widgetEditOldScrollPos);
                    cmd->setNewScrollPos(scrollPosNew);
                    spdlog::info("CtCommandBridge: endWidgetEdit created delta table cell command");
                    _commandManager.addCommandToStack(std::move(cmd));
                    clearState();
                    if (!isInUndoRedo()) {
                        _currentOp = BridgeOp::None;
                        beginTextEditSession(savedNodeId);
                    }
                    return;
                }
                // setWidgetTableCell failed (model not in sync) — fall through to XML path
                spdlog::warn("CtCommandBridge: endWidgetEdit table delta failed, falling back to XML path");
            } else if (foundWidget) {
                spdlog::debug("CtCommandBridge: endWidgetEdit - no table cell content change");
                clearState();
                if (!isInUndoRedo()) {
                    _currentOp = BridgeOp::None;
                    beginTextEditSession(savedNodeId);
                }
                return;
            }
            // Widget not found at offset — fall through to XML path
        } else {
            // Codebox delta path: compare full text content.
            std::string newContent;
            bool foundWidget = false;
            for (auto* w : widgetList) {
                if (w->getOffset() == savedCharOffset && w->get_type() == CtAnchWidgType::CodeBox) {
                    newContent = static_cast<CtCodebox*>(w)->get_text_content().raw();
                    foundWidget = true;
                    break;
                }
            }

            if (foundWidget && newContent != _widgetEditOldContent) {
                // Update model to reflect new content
                auto node = _docModel->getNodeById(_widgetEditNodeId);
                if (node && node->getContent().setWidgetContentData(savedCharOffset, newContent)) {
                    auto cmd = std::make_unique<EditCodeboxContentCommand>(
                        _docModel,
                        _widgetEditNodeId,
                        savedCharOffset,
                        _widgetEditOldContent,
                        newContent,
                        _widgetEditOldCursorPos,
                        newCursorPos
                    );
                    double scrollPosNew = -1.0;
                    auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
                    if (adj) scrollPosNew = adj->get_value();
                    cmd->setOldScrollPos(_widgetEditOldScrollPos);
                    cmd->setNewScrollPos(scrollPosNew);
                    spdlog::info("CtCommandBridge: endWidgetEdit created delta codebox command");
                    _commandManager.addCommandToStack(std::move(cmd));
                    clearState();
                    if (!isInUndoRedo()) {
                        _currentOp = BridgeOp::None;
                        beginTextEditSession(savedNodeId);
                    }
                    return;
                }
                // setWidgetContentData failed (model not in sync) — fall through to XML path
                spdlog::warn("CtCommandBridge: endWidgetEdit delta failed, falling back to XML path");
            } else if (foundWidget) {
                spdlog::debug("CtCommandBridge: endWidgetEdit - no codebox content change");
                clearState();
                if (!isInUndoRedo()) {
                    _currentOp = BridgeOp::None;
                    beginTextEditSession(savedNodeId);
                }
                return;
            }
            // Widget not found at offset — fall through to XML path
        }
    }

    // XML snapshot path (used for tables, unknown widgets, and codebox fallback)
    Glib::ustring finalXml = getBufferContentAsXml(buffer, &treeIter);

    if (_widgetEditInitialXml == finalXml) {
        spdlog::debug("CtCommandBridge: endWidgetEdit - no changes detected");
        clearState();
    } else {
        // Update model with the new content
        auto node = _docModel->getNodeById(_widgetEditNodeId);
        if (node) {
            node->setContentXml(finalXml);
        }

        auto cmd = std::make_unique<WidgetCommand>(
            _docModel,
            _widgetEditNodeId,
            _widgetEditInitialXml,
            finalXml,
            "widget-edit",
            _widgetEditOldCursorPos,
            newCursorPos
        );

        // Stamp scroll positions so undo/redo scrolls to the widget location.
        double scrollPosNew = -1.0;
        auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
        if (adj) scrollPosNew = adj->get_value();
        cmd->setOldScrollPos(_widgetEditOldScrollPos);
        cmd->setNewScrollPos(scrollPosNew);

        spdlog::info("CtCommandBridge: endWidgetEdit created command, adding to undo stack");
        _commandManager.addCommandToStack(std::move(cmd));

        clearState();
    }

    // Restart the outer-buffer edit session so edits after the widget are captured.
    // Skip during undo/redo — those operations restart the session themselves.
    if (!isInUndoRedo()) {
        _currentOp = BridgeOp::None;
        beginTextEditSession(savedNodeId);
    }
}

void CtCommandBridge::beginFormatChange(gint64 nodeId, const std::string& formatType)
{
    if (!_active || !_pMainWin) {
        return;
    }

    // Flush widget edit first so format change gets its own undo entry
    if (_widgetEditNodeId != 0) {
        endWidgetEdit();
    }

    // If already capturing a format change, end it first to avoid conflating operations
    if (_currentOp == BridgeOp::CapturingFormat) {
        spdlog::warn("CtCommandBridge: beginFormatChange called while already capturing, ending previous capture first");
        endFormatChange();
    }

    // Capture buffer state before format is applied
    auto buffer = _pMainWin->curr_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no current buffer for format change");
        return;
    }

    // Capture cursor position before formatting
    int cursorPos = buffer->get_insert()->get_iter().get_offset();

    // Get the tree iter for the node we're formatting
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: could not find tree iter for node {}", nodeId);
        return;
    }

    _currentOp = BridgeOp::CapturingFormat;
    _formatChangeNodeId = nodeId;
    _captureFormatType = formatType;
    _formatChangeInitialXml = getBufferContentAsXml(buffer, &treeIter);
    _formatChangeOldCursorPos = cursorPos;
}

void CtCommandBridge::endFormatChange()
{
    if (!_active || !_pMainWin || _currentOp != BridgeOp::CapturingFormat) {
        return;
    }

    // Get the tree iter for the node we're formatting
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_formatChangeNodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: could not find tree iter for node {}", _formatChangeNodeId);
        _currentOp = BridgeOp::None;
        return;
    }

    auto buffer = treeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no buffer for node {} after format change", _formatChangeNodeId);
        _currentOp = BridgeOp::None;
        return;
    }

    Glib::ustring finalXml = getBufferContentAsXml(buffer, &treeIter);
    _formatChangeNewCursorPos = buffer->get_insert()->get_iter().get_offset();

    if (_formatChangeInitialXml == finalXml) {
        _currentOp = BridgeOp::None;
        return;
    }

    auto cmd = std::make_unique<ApplyFormatCommand>(
        _docModel,
        _formatChangeNodeId,
        _formatChangeInitialXml,
        finalXml,
        _captureFormatType,
        _formatChangeOldCursorPos,
        _formatChangeNewCursorPos
    );

    spdlog::info("CtCommandBridge: format change created command, adding to undo stack");
    _commandManager.addCommandToStack(std::move(cmd));

    _currentOp = BridgeOp::None;
}

// Phase 4: New model-first format operation
void CtCommandBridge::applyFormatV2(
    gint64 nodeId,
    int selStart,
    int selEnd,
    const std::string& attribute,
    const std::string& value,
    int oldCursorPos,
    int newCursorPos)
{
    if (!_active || !_pMainWin) {
        return;
    }

    spdlog::debug("CtCommandBridge::applyFormatV2: {}={} on range [{}, {}) in node {}, cursor {} -> {}",
                  attribute, value, selStart, selEnd, nodeId, oldCursorPos, newCursorPos);

    // Create the format command
    // The command will capture old values during execute() and handle undo/redo
    auto cmd = std::make_unique<ApplyFormatCommandV2>(
        _docModel,
        nodeId,
        selStart,
        selEnd - selStart,  // Command takes length, not end offset
        attribute,
        value,
        oldCursorPos,
        newCursorPos
    );

    // Set pending cursor position so observer will update the buffer
    _pendingCursorPos = newCursorPos;

    // Execute command - this updates the model and notifies observers
    // The observer will see _pendingCursorPos and update the buffer
    _inCommandExecution = true;  // Treat this like undo/redo for observer
    cmd->execute();
    _inCommandExecution = false;

    // Process GTK events to ensure buffer update is complete
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    // Add to undo stack
    _commandManager.addCommandToStack(std::move(cmd));

    spdlog::info("CtCommandBridge::applyFormatV2: format command created and executed");
}

void CtCommandBridge::beginPaste(gint64 nodeId, bool pasteContainsWidgets)
{
    if (!pasteContainsWidgets) {
        // Delta path: same pattern as cut — open a session to capture buffer signals.
        if (!_active || !_pMainWin) return;

        if (_widgetEditNodeId != 0) endWidgetEdit();
        if (_editSession && _editSession->isActive()) endTextEditSession();

        auto& treeStore = _pMainWin->get_tree_store();
        CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
        if (!treeIter) { spdlog::error("CtCommandBridge: beginPaste - node {} not found", nodeId); return; }
        auto buffer = treeIter.get_node_text_buffer();
        if (!buffer) { spdlog::error("CtCommandBridge: beginPaste - no buffer for node {}", nodeId); return; }

        if (!_skipNextModelSync) {
            auto node = _docModel->getNodeById(nodeId);
            if (!node) {
                auto newNode = std::make_shared<CtNodeModel>(nodeId);
                newNode->setName(treeIter.get_node_name());
                newNode->setSyntax(treeIter.get_node_syntax_highlighting());
                newNode->setContentXml(getBufferContentAsXml(buffer, &treeIter));
                _docModel->addNode(newNode, 0);
            } else if (buffer->get_modified()) {
                node->setContentXml(getBufferContentAsXml(buffer, &treeIter));
            }
        }
        _skipNextModelSync = false;

        {
            auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
            _sessionScrollPosOld = adj ? adj->get_value() : -1.0;
        }

        _currentOp = BridgeOp::CapturingPaste;
        _captureNodeId = nodeId;
        bool hasWidgets = !treeIter.get_anchored_widgets().empty();
        _editSession->begin(nodeId, buffer, hasWidgets, &treeIter);
    } else {
        // XML snapshot path (default, safe for all paste types)
        beginXmlCapture(BridgeOp::CapturingPaste, nodeId);
    }
}

void CtCommandBridge::endPaste()
{
    if (!_active || !_pMainWin || _currentOp != BridgeOp::CapturingPaste) return;

    // Session (delta) path — session is active when pasteContainsWidgets was false
    if (_editSession && _editSession->isActive()) {
        auto& treeStore = _pMainWin->get_tree_store();
        CtTreeIter treeIter = treeStore.get_node_from_node_id(_captureNodeId);
        if (!treeIter) {
            _editSession->cancel();
            _currentOp = BridgeOp::None;
            _captureNodeId = 0;
            return;
        }
        auto buffer = treeIter.get_node_text_buffer();
        if (!buffer) {
            _editSession->cancel();
            _currentOp = BridgeOp::None;
            _captureNodeId = 0;
            return;
        }

        auto widgets = treeIter.get_anchored_widgets();
        int cursorPos = buffer->get_insert()->get_iter().get_offset();
        auto cmd = _editSession->end(buffer, widgets, cursorPos);

        if (cmd) {
            if (auto* cc = dynamic_cast<CompoundCommand*>(cmd.get())) {
                cc->setDescription("Node " + std::to_string(_captureNodeId) + ": Paste clipboard");
                double scrollPosNew = -1.0;
                auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
                if (adj) scrollPosNew = adj->get_value();
                cc->setOldScrollPos(_sessionScrollPosOld);
                cc->setNewScrollPos(scrollPosNew);
            }
            spdlog::info("CtCommandBridge: paste command created (delta), adding to undo stack");
            addCommandToStack(std::move(cmd));
        }

        _currentOp = BridgeOp::None;
        _captureNodeId = 0;
    } else {
        // XML snapshot path
        endXmlCapture("Paste clipboard");
    }
}

void CtCommandBridge::beginCut(gint64 nodeId)
{
    if (!_active || !_pMainWin) {
        return;
    }

    // Flush any pending widget or text edit so each gets its own undo entry.
    if (_widgetEditNodeId != 0) {
        endWidgetEdit();
    }
    if (_editSession && _editSession->isActive()) {
        endTextEditSession();
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: beginCut - node {} not found", nodeId);
        return;
    }

    auto buffer = treeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: beginCut - no buffer for node {}", nodeId);
        return;
    }

    // Ensure node is in model (lazy init), then re-sync if needed.
    if (!_skipNextModelSync) {
        auto node = _docModel->getNodeById(nodeId);
        if (!node) {
            auto newNode = std::make_shared<CtNodeModel>(nodeId);
            newNode->setName(treeIter.get_node_name());
            newNode->setSyntax(treeIter.get_node_syntax_highlighting());
            newNode->setContentXml(getBufferContentAsXml(buffer, &treeIter));
            _docModel->addNode(newNode, 0);
        } else if (buffer->get_modified()) {
            node->setContentXml(getBufferContentAsXml(buffer, &treeIter));
        }
    }
    _skipNextModelSync = false;

    // Capture scroll position for undo scroll restoration.
    {
        auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
        _sessionScrollPosOld = adj ? adj->get_value() : -1.0;
    }

    // Mark cut in progress and open a session so the deletion is captured as a delta.
    _currentOp = BridgeOp::CapturingCut;
    _captureNodeId = nodeId;
    bool hasWidgets = !treeIter.get_anchored_widgets().empty();
    _editSession->begin(nodeId, buffer, hasWidgets, &treeIter);
}

void CtCommandBridge::endCut()
{
    if (!_active || !_pMainWin || _currentOp != BridgeOp::CapturingCut) {
        return;
    }

    if (!_editSession || !_editSession->isActive()) {
        _currentOp = BridgeOp::None;
        _captureNodeId = 0;
        return;
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_captureNodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: endCut - node {} not found", _captureNodeId);
        _editSession->cancel();
        _currentOp = BridgeOp::None;
        _captureNodeId = 0;
        return;
    }

    auto buffer = treeIter.get_node_text_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: endCut - no buffer for node {}", _captureNodeId);
        _editSession->cancel();
        _currentOp = BridgeOp::None;
        _captureNodeId = 0;
        return;
    }

    auto widgets = treeIter.get_anchored_widgets();
    int cursorPos = buffer->get_insert()->get_iter().get_offset();
    auto cmd = _editSession->end(buffer, widgets, cursorPos);

    if (cmd) {
        if (auto* cc = dynamic_cast<CompoundCommand*>(cmd.get())) {
            cc->setDescription("Node " + std::to_string(_captureNodeId) + ": Cut clipboard");
            double scrollPosNew = -1.0;
            auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
            if (adj) scrollPosNew = adj->get_value();
            cc->setOldScrollPos(_sessionScrollPosOld);
            cc->setNewScrollPos(scrollPosNew);
        }
        spdlog::info("CtCommandBridge: cut command created (delta), adding to undo stack");
        addCommandToStack(std::move(cmd));
    }

    _currentOp = BridgeOp::None;
    _captureNodeId = 0;
}

void CtCommandBridge::beginXmlCapture(BridgeOp op, gint64 nodeId)
{
    if (!_active || !_pMainWin) {
        return;
    }

    if (_widgetEditNodeId != 0) {
        endWidgetEdit();
    }

    // If already capturing a paste, end it first (cut now has its own path)
    if (_currentOp == BridgeOp::CapturingPaste) {
        spdlog::warn("CtCommandBridge: beginXmlCapture called while already capturing paste, ending first");
        endXmlCapture("Paste clipboard");
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    auto buffer = _pMainWin->curr_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no current buffer for XML capture");
        return;
    }

    // Lazy: ensure node is in model before capturing initial XML.
    if (!_docModel->getNodeById(nodeId) && treeIter) {
        auto newNode = std::make_shared<CtNodeModel>(nodeId);
        newNode->setName(treeIter.get_node_name());
        newNode->setSyntax(treeIter.get_node_syntax_highlighting());
        newNode->setContentXml(getBufferContentAsXml(buffer, &treeIter));
        _docModel->addNode(newNode, 0);
    }

    _currentOp = op;
    _captureNodeId = nodeId;
    _captureInitialXml = getCurrentBufferXml();
    _captureOldCursorPos = buffer->property_cursor_position();
}

void CtCommandBridge::endXmlCapture(const std::string& description)
{
    if (!_active || !_pMainWin || _currentOp != BridgeOp::CapturingPaste) {
        return;
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_captureNodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: node {} not found in tree for XML capture end", _captureNodeId);
        _currentOp = BridgeOp::None;
        return;
    }

    auto buffer = _pMainWin->curr_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no current buffer for XML capture end");
        _currentOp = BridgeOp::None;
        return;
    }

    Glib::ustring finalXml = getCurrentBufferXml();
    _captureNewCursorPos = buffer->property_cursor_position();

    if (_captureInitialXml == finalXml) {
        _currentOp = BridgeOp::None;
        return;
    }

    auto cmd = std::make_unique<TextEditCommand>(
        _docModel,
        _captureNodeId,
        _captureInitialXml,
        finalXml,
        _captureOldCursorPos,
        _captureNewCursorPos,
        description
    );

    spdlog::info("CtCommandBridge: {} command created, adding to undo stack", description);
    addCommandToStack(std::move(cmd));

    // Update document model
    auto node = _docModel->getNodeById(_captureNodeId);
    if (node) {
        node->setContentXml(finalXml);
        _docModel->notifyNodeChanged(_captureNodeId);
    }

    _currentOp = BridgeOp::None;
    _captureNodeId = 0;
    _captureInitialXml.clear();
}

void CtCommandBridge::commitWidgetModification(
    gint64 nodeId,
    int charOffset,
    const CtWidgetDesc& oldDesc,
    const CtWidgetDesc& newDesc,
    const std::string& description)
{
    auto cmd = std::make_unique<ModifyWidgetDeltaCommand>(
        _docModel, nodeId, charOffset, oldDesc, newDesc, description);
    addCommandToStack(std::move(cmd));
    auto node = _docModel->getNodeById(nodeId);
    if (node) {
        node->getContent().replaceWidget(charOffset, newDesc);
    }
}

Glib::ustring CtCommandBridge::getCurrentBufferXml()
{
    if (!_active || !_pMainWin) {
        return "";
    }

    auto buffer = _pMainWin->curr_buffer();
    if (!buffer) {
        return "";
    }

    return getBufferContentAsXml(buffer);
}

void CtCommandBridge::registerNewNode(gint64 nodeId, gint64 parentId)
{
    if (!_active) {
        return;
    }

    if (!_pMainWin) {
        spdlog::error("CtCommandBridge: no main window for registering new node");
        return;
    }

    // Check if node already exists in model
    if (_docModel->getNodeById(nodeId)) {
        spdlog::debug("CtCommandBridge: node {} already exists in model", nodeId);
        return;
    }

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("CtCommandBridge: could not find node {} in tree", nodeId);
        return;
    }

    // Get node data from tree
    Glib::ustring nodeName = treeIter.get_node_name();
    auto buffer = treeIter.get_node_text_buffer();
    Glib::ustring contentXml;
    if (buffer) {
        contentXml = getBufferContentAsXml(buffer, &treeIter);
    }

    // Create and add node to model
    auto nodeModel = std::make_shared<CtNodeModel>(nodeId);
    nodeModel->setName(nodeName);
    nodeModel->setContentXml(contentXml);

    _docModel->addNode(nodeModel, parentId);
    spdlog::debug("CtCommandBridge: registered new node {} '{}' (parent: {})", nodeId, nodeName.c_str(), parentId);
}

Glib::ustring CtCommandBridge::getBufferContentAsXml(Glib::RefPtr<Gtk::TextBuffer> buffer, const CtTreeIter* treeIter)
{
    if (!buffer || !_pMainWin) {
        return "";
    }

    // Get the actual text from the buffer
    Glib::ustring bufferText = buffer->get_text();
    spdlog::debug("CtCommandBridge: serializing buffer to XML - buffer text: '{}'", bufferText.c_str());

    try {
        // Create XML document
        xmlpp::Document xml_doc;
        xml_doc.create_root_node("node");
        xmlpp::Element* p_root = xml_doc.get_root_node();

        // Use existing storage helper to serialize buffer
        CtStorageXmlHelper xml_helper(_pMainWin);

        // Serialize the buffer content - need to specify full range (0 to buffer length)
        int start_offset = 0;
        int end_offset = buffer->get_char_count();
        xml_helper.save_buffer_no_widgets_to_xml(p_root, buffer, start_offset, end_offset, 'n');

        // Get the tree iter to serialize widgets - use provided treeIter or fall back to curr_tree_iter
        CtTreeIter iter_to_use = treeIter ? *treeIter : _pMainWin->curr_tree_iter();
        if (iter_to_use) {
            // Serialize anchored widgets
            auto widgets = iter_to_use.get_anchored_widgets();
            spdlog::debug("CtCommandBridge: serializing {} anchored widgets from tree iter", widgets.size());
            for (CtAnchoredWidget* widget : widgets) {
                widget->to_xml(p_root, 0, nullptr, "");
            }
        }

        // Convert XML document to string
        Glib::ustring xmlResult = xml_doc.write_to_string();
        spdlog::debug("CtCommandBridge: XML result: '{}'", xmlResult.substr(0, 200).c_str());
        return xmlResult;
    }
    catch (const std::exception& e) {
        spdlog::error("CtCommandBridge: XML serialization failed: {}", e.what());
        return "";
    }
}

void CtCommandBridge::updateBufferFromXml(Glib::RefPtr<Gtk::TextBuffer> buffer, const Glib::ustring& xml, const std::string& syntax, const CtTreeIter* treeIter)
{
    // Note: buildBufferFromContent() is incomplete (Phase 2 TODO - doesn't handle widgets)
    // Until ct_buffer_converter.cc is implemented, we need XML-based buffer updates
    // to properly restore widgets during undo/redo operations

    if (!buffer || !_pMainWin || xml.empty()) {
        return;
    }

    spdlog::debug("CtCommandBridge: deserializing XML to buffer (syntax: {}, xml length: {})", syntax, xml.size());

    try {
        // Parse XML string
        xmlpp::DomParser parser;
        parser.parse_memory(xml);
        if (!parser) {
            spdlog::error("CtCommandBridge: XML parsing failed");
            return;
        }

        const xmlpp::Element* p_root = parser.get_document()->get_root_node();
        if (!p_root) {
            spdlog::error("CtCommandBridge: no root element in XML");
            return;
        }

        // Use the provided tree iter, or fall back to current iter if not provided
        auto target_iter = treeIter ? *treeIter : _pMainWin->curr_tree_iter();

        // Determine which cursor position to restore
        // Priority: pending cursor pos from undo/redo > current cursor position
        int cursor_offset;
        if (_pendingCursorPos >= 0) {
            cursor_offset = _pendingCursorPos;
            spdlog::debug("CtCommandBridge: restoring cursor to pending position: {}", cursor_offset);
            _pendingCursorPos = -1;  // Consume the pending position
        } else {
            cursor_offset = buffer->get_insert()->get_iter().get_offset();
        }

        // Clear current buffer FIRST - this removes widget anchors from buffer
        spdlog::debug("CtCommandBridge: clearing buffer");
        buffer->erase(buffer->begin(), buffer->end());
        spdlog::debug("CtCommandBridge: buffer cleared");

        // Delete old widgets from target node AFTER clearing buffer
        // (widgets must be unanchored before deletion)
        if (target_iter) {
            Gtk::TreeRow row = *static_cast<Gtk::TreeModel::iterator>(target_iter);
            auto widget_list = row.get_value(_pMainWin->get_tree_store().get_columns().colAnchoredWidgets);
            spdlog::debug("CtCommandBridge: deleting {} old widgets", widget_list.size());
            for (CtAnchoredWidget* widget : widget_list) {
                delete widget;
            }
            widget_list.clear();
            // Update the row with empty list
            row.set_value(_pMainWin->get_tree_store().get_columns().colAnchoredWidgets, widget_list);
            spdlog::debug("CtCommandBridge: old widgets deleted");
        }

        // Use existing storage helper to deserialize DIRECTLY into the buffer
        CtStorageXmlHelper xml_helper(_pMainWin);
        std::list<CtAnchoredWidget*> widgets;

        // Deserialize each XML slot directly into the existing buffer
        auto pGtkSourceBuffer = GTK_SOURCE_BUFFER(buffer->gobj());
        CT_SOURCE_BUFFER_BEGIN_NOT_UNDOABLE(pGtkSourceBuffer);

        spdlog::debug("CtCommandBridge: deserializing XML slots");
        int slotCount = 0;
        for (xmlpp::Node* xml_slot : p_root->get_children()) {
            slotCount++;
            const xmlpp::Element* elem = dynamic_cast<const xmlpp::Element*>(xml_slot);
            if (elem) {
                spdlog::debug("CtCommandBridge: processing slot {}: {}", slotCount, elem->get_name().c_str());
            }
            if (!xml_helper.get_text_buffer_one_slot_from_xml(buffer, xml_slot, widgets, nullptr, -1, "")) {
                spdlog::error("CtCommandBridge: failed to deserialize XML slot {}", slotCount);
                break;
            }
        }
        spdlog::debug("CtCommandBridge: deserialized {} slots, {} widgets", slotCount, widgets.size());

        CT_SOURCE_BUFFER_END_NOT_UNDOABLE(pGtkSourceBuffer);
        spdlog::debug("CtCommandBridge: source buffer not-undoable block ended");

        // Restore cursor position after deserialization
        // Clamp the offset to valid range in case buffer is now shorter
        // Use get_char_count() to include child anchors (widgets), not just visible text
        int new_length = buffer->get_char_count();
        int restored_offset = std::min(cursor_offset, new_length);
        spdlog::debug("CtCommandBridge: restoring cursor to offset {} (requested {}, buffer length {})",
                     restored_offset, cursor_offset, new_length);
        auto restore_iter = buffer->get_iter_at_offset(restored_offset);
        buffer->place_cursor(restore_iter);
        spdlog::debug("CtCommandBridge: cursor restored");

        // Add new widgets to target node's anchored widgets list
        if (!widgets.empty() && target_iter) {
            spdlog::debug("CtCommandBridge: adding {} widgets to tree node", widgets.size());
            _pMainWin->get_tree_store().addAnchoredWidgets(target_iter, widgets, &_pMainWin->get_text_view().mm());

            // Verify widgets were added
            auto added_widgets = target_iter.get_anchored_widgets();
            spdlog::debug("CtCommandBridge: after addAnchoredWidgets, tree node has {} widgets", added_widgets.size());
        } else if (!widgets.empty()) {
            spdlog::warn("CtCommandBridge: no tree iter for widgets, widgets will be lost");
        }

        spdlog::debug("CtCommandBridge: buffer deserialization complete, text length: {}, total char count (incl. widgets): {}",
                      buffer->get_text().size(), buffer->get_char_count());
    }
    catch (const Glib::Error& e) {
        spdlog::error("CtCommandBridge: XML deserialization failed (Glib::Error): {}", e.what());
        throw;  // Re-throw to let caller handle
    }
    catch (const xmlpp::exception& e) {
        spdlog::error("CtCommandBridge: XML deserialization failed (xmlpp::exception): {}", e.what());
        throw;  // Re-throw to let caller handle
    }
    catch (const std::exception& e) {
        spdlog::error("CtCommandBridge: XML deserialization failed (std::exception): {}", e.what());
        throw;  // Re-throw to let caller handle
    }
    catch (...) {
        spdlog::error("CtCommandBridge: XML deserialization failed (unknown exception)");
        throw;  // Re-throw to let caller handle
    }
}

// BridgeObserver implementation

void CtCommandBridge::BridgeObserver::onNodeChanged(gint64 nodeId)
{
    if (!_bridge || !_bridge->_pMainWin) {
        return;
    }

    // Track which nodes have already been updated during this undo/redo operation
    // to prevent duplicate updates from cascading model changes within a single operation
    static std::set<gint64> nodesUpdatedThisUndoRedo;
    static int lastUndoRedoGeneration = -1;

    // Clear the tracking set when a NEW undo/redo operation starts
    // (detected by generation counter changing)
    if (!_bridge->_inCommandExecution) {
        nodesUpdatedThisUndoRedo.clear();
        lastUndoRedoGeneration = -1;
    } else if (_bridge->_undoRedoGeneration != lastUndoRedoGeneration) {
        nodesUpdatedThisUndoRedo.clear();
        lastUndoRedoGeneration = _bridge->_undoRedoGeneration;
    } else if (nodesUpdatedThisUndoRedo.count(nodeId) > 0) {
        spdlog::warn("BridgeObserver::onNodeChanged: skipping duplicate update for node {} during undo/redo", nodeId);
        return;
    }

    // Reentrancy guard - prevent recursive calls during buffer updates
    static bool inOnNodeChanged = false;
    if (inOnNodeChanged) {
        spdlog::warn("BridgeObserver::onNodeChanged: ignoring recursive call for node {}", nodeId);
        return;
    }

    // RAII guard to ensure flag is always reset, even if exceptions occur or early returns happen
    struct ReentrancyGuard {
        bool& flag;
        ReentrancyGuard(bool& f) : flag(f) { flag = true; }
        ~ReentrancyGuard() { flag = false; }
    };
    ReentrancyGuard guard(inOnNodeChanged);

    if (_bridge->_inCommandExecution) {
        nodesUpdatedThisUndoRedo.insert(nodeId);
    }

    spdlog::debug("BridgeObserver::onNodeChanged: node {} changed, inCommandExecution={}, pendingCursorPos={}",
                 nodeId, _bridge->_inCommandExecution, _bridge->_pendingCursorPos);

    auto curr_iter = _bridge->_pMainWin->curr_tree_iter();
    if (!curr_iter) {
        return;
    }

    // Get the updated content from the model
    auto node = _bridge->_docModel->getNodeById(nodeId);
    if (!node) {
        spdlog::error("BridgeObserver: node {} not found in model", nodeId);
        return;
    }

    // Find the tree node and update its buffer
    auto treeIter = _bridge->_pMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("BridgeObserver: tree node {} not found", nodeId);
        return;
    }

    // Get the buffers
    auto treeBuffer = treeIter.get_node_text_buffer();
    if (!treeBuffer) {
        spdlog::error("BridgeObserver: no buffer for node {}", nodeId);
        return;
    }

    bool isCurrentNode = (curr_iter.get_node_id() == nodeId);

    // Rebuild buffer from structured model content (handles both text-only and widget nodes)
    try {
        if (!isCurrentNode) {
            buildBufferForNode(nodeId, node->getContent(), treeBuffer, treeIter, false);
        } else if (_bridge->_pendingCursorPos >= 0 || _bridge->_inCommandExecution) {
            buildBufferForNode(nodeId, node->getContent(), treeBuffer, treeIter);

            if (_bridge->_pendingCursorPos >= 0) {
                int cursor_offset = _bridge->_pendingCursorPos;
                _bridge->_pendingCursorPos = -1;
                int max_offset = treeBuffer->get_char_count();
                auto restore_iter = treeBuffer->get_iter_at_offset(std::min(cursor_offset, max_offset));
                treeBuffer->place_cursor(restore_iter);
            }
            // Note: scroll restoration is NOT done here. Setting scroll position
            // during buffer rebuild is unreliable because GTK layout changes from
            // widget insertion can override it. Instead, undo()/redo() apply scroll
            // position after all GTK events have been processed.
        }
        // Else: skip update during normal editing — buffer is already correct
    } catch (...) {
        spdlog::error("BridgeObserver::onNodeChanged: exception during buffer reconstruction for node {}", nodeId);
    }
}

void CtCommandBridge::BridgeObserver::buildBufferForNode(
    gint64 nodeId,
    const CtNodeContent& content,
    const Glib::RefPtr<Gtk::TextBuffer>& buf,
    CtTreeIter& iter,
    bool attachToView)
{
    // Delete old widgets before clearing buffer (they're unanchored by the clear)
    Gtk::TreeRow row = *static_cast<Gtk::TreeModel::iterator>(iter);
    auto old_widgets = row.get_value(_bridge->_pMainWin->get_tree_store().get_columns().colAnchoredWidgets);
    for (CtAnchoredWidget* w : old_widgets) { delete w; }
    old_widgets.clear();
    row.set_value(_bridge->_pMainWin->get_tree_store().get_columns().colAnchoredWidgets, old_widgets);

    auto pGtkSourceBuffer = GTK_SOURCE_BUFFER(buf->gobj());
    CT_SOURCE_BUFFER_BEGIN_NOT_UNDOABLE(pGtkSourceBuffer);
    std::list<CtAnchoredWidget*> new_widgets;
    try {
        new_widgets = buildBufferFromContent(content, buf, _bridge->_pMainWin);
    } catch (...) {
        spdlog::error("BridgeObserver::buildBufferForNode: exception in buildBufferFromContent for node {}", nodeId);
        buf->set_text("(Content reconstruction failed due to corrupt data)");
    }
    CT_SOURCE_BUFFER_END_NOT_UNDOABLE(pGtkSourceBuffer);

    if (!new_widgets.empty()) {
        Gtk::TextView* pView = attachToView ? &_bridge->_pMainWin->get_text_view().mm() : nullptr;
        _bridge->_pMainWin->get_tree_store().addAnchoredWidgets(iter, new_widgets, pView);
    }

}

void CtCommandBridge::BridgeObserver::onNodeAdded(gint64 nodeId, gint64 parentId)
{
    if (!_bridge || !_bridge->_pMainWin) {
        return;
    }

    spdlog::debug("BridgeObserver: node {} added to parent {}", nodeId, parentId);

    // Get node from model
    auto node = _bridge->_docModel->getNodeById(nodeId);
    if (!node) {
        spdlog::error("BridgeObserver: cannot add node {}, not found in model", nodeId);
        return;
    }

    // Create CtNodeData from model
    CtNodeData nodeData;
    nodeData.nodeId = nodeId;
    nodeData.name = node->getName();
    nodeData.syntax = node->getSyntax();
    nodeData.tags = node->getTags();
    nodeData.isReadOnly = node->isReadOnly();
    nodeData.isBold = node->isBold();
    nodeData.customIconId = node->getCustomIconId();
    nodeData.foregroundRgb24 = node->getForegroundRgb24();
    nodeData.tsCreation = node->getCreationTime();
    nodeData.tsLastSave = node->getLastSaveTime();

    // Get parent iterator
    auto& treeStore = _bridge->_pMainWin->get_tree_store();
    Gtk::TreeModel::iterator* parentIter = nullptr;
    Gtk::TreeModel::iterator parentGtkIter;
    if (parentId != -1) {
        auto parentCtIter = treeStore.get_node_from_node_id(parentId);
        if (parentCtIter) {
            parentGtkIter = static_cast<Gtk::TreeModel::iterator>(parentCtIter);
            parentIter = &parentGtkIter;
        }
    }

    // Add node to GTK tree
    treeStore.append_node(&nodeData, parentIter);
    spdlog::debug("BridgeObserver: added node {} to GTK tree", nodeId);
}

void CtCommandBridge::BridgeObserver::onNodeDeleted(gint64 nodeId)
{
    if (!_bridge || !_bridge->_pMainWin) {
        return;
    }

    spdlog::debug("BridgeObserver: node {} deleted", nodeId);

    // Remove from navigation history
    for (auto it = _bridge->_pMainWin->_visitedNodes.begin(); it != _bridge->_pMainWin->_visitedNodes.end(); ) {
        if (*it == nodeId) {
            it = _bridge->_pMainWin->_visitedNodes.erase(it);
            if (_bridge->_pMainWin->_visitedNodesIdx > 0) {
                _bridge->_pMainWin->_visitedNodesIdx--;
            }
        } else {
            ++it;
        }
    }

    // Find the node in GTK tree and remove it
    auto& treeStore = _bridge->_pMainWin->get_tree_store();
    auto nodeIter = treeStore.get_node_from_node_id(nodeId);

    if (!nodeIter) {
        spdlog::warn("BridgeObserver: node {} not found in GTK tree for deletion", nodeId);
        return;
    }

    // If it's the current node, select another one first
    auto currIter = _bridge->_pMainWin->curr_tree_iter();
    if (currIter && currIter.get_node_id() == nodeId) {
        spdlog::debug("BridgeObserver: deleted node was current, selecting another");
        // Try to select next sibling, or parent
        auto gtkIter = static_cast<Gtk::TreeModel::iterator>(nodeIter);
        auto nextIter = gtkIter;
        ++nextIter;
        if (nextIter) {
            _bridge->_pMainWin->get_tree_view().set_cursor_safe(nextIter);
        } else {
            auto parentIter = gtkIter->parent();
            if (parentIter) {
                _bridge->_pMainWin->get_tree_view().set_cursor_safe(parentIter);
            }
        }
    }

    // Remove from GTK tree
    auto gtkStore = treeStore.get_store();
    if (gtkStore) {
        gtkStore->erase(static_cast<Gtk::TreeModel::iterator>(nodeIter));
        spdlog::debug("BridgeObserver: removed node {} from GTK tree", nodeId);
    }
}

void CtCommandBridge::BridgeObserver::onNodeMoved(gint64 nodeId, gint64 newParentId, int newPosition)
{
    if (!_bridge || !_bridge->_pMainWin) {
        return;
    }

    spdlog::debug("BridgeObserver: node {} moved to parent {} at position {}",
                  nodeId, newParentId, newPosition);

    // For now, simplest implementation: remove and re-add
    // This handles the tree structure correctly but loses expansion state
    // TODO: Implement in-place move to preserve expansion state

    auto& treeStore = _bridge->_pMainWin->get_tree_store();
    auto nodeIter = treeStore.get_node_from_node_id(nodeId);

    if (!nodeIter) {
        spdlog::error("BridgeObserver: cannot move node {}, not found in GTK tree", nodeId);
        return;
    }

    // Get the node from the model
    auto node = _bridge->_docModel->getNodeById(nodeId);
    if (!node) {
        spdlog::error("BridgeObserver: cannot move node {}, not found in model", nodeId);
        return;
    }

    // Create CtNodeData from model
    CtNodeData nodeData;
    nodeData.nodeId = nodeId;
    nodeData.name = node->getName();
    nodeData.syntax = node->getSyntax();
    nodeData.tags = node->getTags();
    nodeData.isReadOnly = node->isReadOnly();
    nodeData.isBold = node->isBold();
    nodeData.customIconId = node->getCustomIconId();
    nodeData.foregroundRgb24 = node->getForegroundRgb24();
    nodeData.tsCreation = node->getCreationTime();
    nodeData.tsLastSave = node->getLastSaveTime();

    // Remove from old location (this also removes children in GTK tree)
    auto gtkStore = treeStore.get_store();
    gtkStore->erase(static_cast<Gtk::TreeModel::iterator>(nodeIter));

    // Find new parent
    Gtk::TreeModel::iterator* newParentIter = nullptr;
    Gtk::TreeModel::iterator newParentGtkIter;
    if (newParentId != -1) {
        auto parentCtIter = treeStore.get_node_from_node_id(newParentId);
        if (parentCtIter) {
            newParentGtkIter = static_cast<Gtk::TreeModel::iterator>(parentCtIter);
            newParentIter = &newParentGtkIter;
        }
    }

    // Add at new location
    auto newIter = treeStore.append_node(&nodeData, newParentIter);

    // Recursively add children from model
    std::function<void(gint64, Gtk::TreeModel::iterator*)> addChildren;
    addChildren = [&](gint64 parentModelId, Gtk::TreeModel::iterator* parentGtkIter) {
        auto parentNode = _bridge->_docModel->getNodeById(parentModelId);
        if (!parentNode) return;

        for (const auto& child : parentNode->getChildren()) {
            CtNodeData childData;
            childData.nodeId = child->getNodeId();
            childData.name = child->getName();
            childData.syntax = child->getSyntax();
            childData.tags = child->getTags();
            childData.isReadOnly = child->isReadOnly();
            childData.isBold = child->isBold();
            childData.customIconId = child->getCustomIconId();
            childData.foregroundRgb24 = child->getForegroundRgb24();
            childData.tsCreation = child->getCreationTime();
            childData.tsLastSave = child->getLastSaveTime();

            auto childCtIter = treeStore.append_node(&childData, parentGtkIter);
            auto childGtkIter = static_cast<Gtk::TreeModel::iterator>(childCtIter);
            addChildren(child->getNodeId(), &childGtkIter);
        }
    };

    auto newGtkIter = static_cast<Gtk::TreeModel::iterator>(newIter);
    addChildren(nodeId, &newGtkIter);

    spdlog::debug("BridgeObserver: moved node {} to new position", nodeId);
}

void CtCommandBridge::BridgeObserver::onTreeStructureChanged()
{
    if (!_bridge || !_bridge->_pMainWin) {
        return;
    }

    spdlog::debug("BridgeObserver: tree structure changed");

    // TODO: Rebuild entire GTK tree from model
    // This is for mass operations
}
