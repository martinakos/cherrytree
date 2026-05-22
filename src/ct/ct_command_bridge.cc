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
#include "ct_const.h"
#include "ct_misc_utils.h"
#include <libxml++/libxml++.h>
#include <functional>
#include <chrono>
#include <set>

// Forward declaration (defined later in this file, used by endTextEditSession and endFormatChange)
static std::pair<std::string, std::string> decomposeTagName(const std::string& tagName);

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

    // Rich cell session: captures buffer signals for description building only (no model sync)
    _richCellSession = std::make_unique<CtTextEditSession>(_docModel);
    _richCellSession->setBridge(this);
    _richCellSession->setSkipModelSync(true);

    spdlog::info("CtCommandBridge: initialized (inactive by default)");
}

CtCommandBridge::~CtCommandBridge()
{
    spdlog::debug("CtCommandBridge: destroying");

    _scrollFixupConnection.disconnect();
    if (_docModel && _observer) {
        _docModel->removeObserver(_observer.get());
    }
}

void CtCommandBridge::initializeFromExistingDocument()
{
    if (!_active || !_pMainWin) return;

    spdlog::info("CtCommandBridge: initializing model hierarchy from existing document");

    // Suppress observer notifications — we're just populating the structural
    // skeleton; GTK is already the source of truth here.
    _docModel->suppressNotifications(true);

    auto& treeStore = _pMainWin->get_tree_store();

    std::function<void(Gtk::TreeModel::iterator, gint64)> registerNode;
    registerNode = [&](Gtk::TreeModel::iterator gtkIter, gint64 parentId) {
        CtTreeIter ti = treeStore.to_ct_tree_iter(gtkIter);
        if (!ti) return;
        const gint64 nid = ti.get_node_id();

        if (!_docModel->getNodeById(nid)) {
            auto nodeModel = std::make_shared<CtNodeModel>(nid);
            nodeModel->setName(ti.get_node_name());
            nodeModel->setSyntax(ti.get_node_syntax_highlighting());
            nodeModel->setTags(ti.get_node_tags());
            nodeModel->setReadOnly(ti.get_node_read_only());
            nodeModel->setBold(ti.get_node_is_bold());
            nodeModel->setCustomIconId(ti.get_node_custom_icon_id());
            nodeModel->setForegroundRgb24(ti.get_node_foreground());
            nodeModel->setExcludedFromSearch(ti.get_node_is_excluded_from_search());
            nodeModel->setChildrenExcludedFromSearch(ti.get_node_children_are_excluded_from_search());
            nodeModel->setCreationTime(ti.get_node_creating_time());
            nodeModel->setLastSaveTime(ti.get_node_modification_time());
            nodeModel->setSharedMasterId(ti.get_node_shared_master_id());
            nodeModel->setSequence(ti.get_node_sequence());
            // Content is NOT loaded here — it is captured lazily when a snapshot
            // is needed (e.g. before node_delete) or via beginTextEditSession.
            _docModel->addNode(nodeModel, parentId, -1);
        }

        #if GTKMM_MAJOR_VERSION >= 4
        for (auto child = gtkIter->children().begin(); child; ++child)
            registerNode(child, nid);
        #else
        for (auto child : gtkIter->children())
            registerNode(child, nid);
        #endif
    };

    auto iter = treeStore.get_iter_first();
    for (; iter; ++iter) {
        registerNode(iter, 0);
    }

    _docModel->suppressNotifications(false);
    spdlog::info("CtCommandBridge: model hierarchy initialized");
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

        // Create node model and sync ALL properties from GTK
        auto nodeModel = std::make_shared<CtNodeModel>(nodeId);
        nodeModel->setName(treeIter.get_node_name());
        nodeModel->setSyntax(treeIter.get_node_syntax_highlighting());
        nodeModel->setTags(treeIter.get_node_tags());
        nodeModel->setReadOnly(treeIter.get_node_read_only());
        nodeModel->setBold(treeIter.get_node_is_bold());
        nodeModel->setCustomIconId(treeIter.get_node_custom_icon_id());
        nodeModel->setForegroundRgb24(treeIter.get_node_foreground());
        nodeModel->setExcludedFromSearch(treeIter.get_node_is_excluded_from_search());
        nodeModel->setChildrenExcludedFromSearch(treeIter.get_node_children_are_excluded_from_search());
        nodeModel->setCreationTime(treeIter.get_node_creating_time());
        nodeModel->setLastSaveTime(treeIter.get_node_modification_time());
        nodeModel->setSharedMasterId(treeIter.get_node_shared_master_id());
        nodeModel->setSequence(treeIter.get_node_sequence());

        // Get buffer content
        auto buffer = treeIter.get_node_text_buffer();
        if (buffer) {
            nodeModel->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
        }

        // Add to document model
        bool added = _docModel->addNode(nodeModel, parentId, -1);
        if (added) {
            spdlog::debug("CtCommandBridge: synced node {} '{}' (parent: {})",
                         nodeId, nodeModel->getName().c_str(), parentId);
        } else {
            spdlog::error("CtCommandBridge: failed to sync node {} '{}' (parent: {})",
                         nodeId, nodeModel->getName().c_str(), parentId);
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
    _lastSyncedNodeId = -1;
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

void CtCommandBridge::pushNodeCommand(std::unique_ptr<CtCommand> cmd)
{
    if (!_active) {
        spdlog::debug("CtCommandBridge::pushNodeCommand — bridge not active, skipping");
        return;
    }
    if (!cmd) {
        spdlog::warn("CtCommandBridge::pushNodeCommand — null command, skipping");
        return;
    }

    // Flush any in-progress text or widget edit session before executing a
    // structural node operation, so the sessions land on the stack in order.
    if (_editSession && _editSession->isActive()) {
        endTextEditSession();
    }
    if (_widgetEditNodeId != 0) {
        endWidgetEdit();
    }

    _currentOp = BridgeOp::ExecutingNodeOp;
    _commandManager.executeCommand(std::move(cmd));
    _currentOp = BridgeOp::None;

    // Restart an edit session so subsequent keystrokes are captured.
    // Without this, characters typed before the next cursor-change or
    // special-key event go into the GTK buffer untracked.
    auto curr_iter = _pMainWin->curr_tree_iter();
    if (curr_iter) {
        beginTextEditSession(curr_iter.get_node_id());
    }
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

    // End any active edit session before undo so intermediate edits become
    // a separate undo entry.  Using cancel() would discard the edits from the
    // command history while leaving the model/buffer modified, desynchronizing
    // the redo stack's expected model state.
    endTextEditSession();

    // Get the node ID and cursor position from command being undone
    auto cmd = _commandManager.peekUndoCommand();
    if (!cmd) {
        spdlog::debug("CtCommandBridge: no command to undo");
        _currentOp = BridgeOp::None;
        return;
    }

    gint64 affectedNodeId = cmd->getNodeId();
    _pendingCursorPos = cmd->getOldCursorPos();
    // Save before the undo runs: the observer consumes _pendingCursorPos, and the
    // node-switch handler resets the cursor to 0 during undo, so scrollTargetOffset
    // is the only reliable record of where we need to scroll after undo.
    int const scrollTargetOffset = _pendingCursorPos;
    // Use the command's stored scroll position if available (reliable for all command types).
    double const cmdScrollPos = cmd->getOldScrollPos();

    // Save current scroll position as fallback before the buffer gets rebuilt
    double savedScrollVal = 0;
    if (not _pMainWin->no_gui()) {
        savedScrollVal = _pMainWin->getScrolledwindowText().get_vadjustment()->get_value();
    }
    // Prefer command's stored scroll position over current vadjustment
    if (cmdScrollPos >= 0) {
        savedScrollVal = cmdScrollPos;
    }

    // Pass scroll position to the observer so it can restore it synchronously
    // after rebuilding the buffer, preventing a visible flash to the top of the node.
    _pendingScrollPos = _pMainWin->no_gui() ? -1.0 : savedScrollVal;

    // Switch to the affected node if it's different from current
    auto curr_iter = _pMainWin->curr_tree_iter();
    if (affectedNodeId != -1 && curr_iter && curr_iter.get_node_id() != affectedNodeId) {
        auto& treeStore = _pMainWin->get_tree_store();
        auto affectedIter = treeStore.get_node_from_node_id(affectedNodeId);
        if (affectedIter) {
            _pMainWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(affectedIter));
        }
    }

    spdlog::debug("CtCommandBridge::undo - about to undo '{}' for node {}",
                  cmd->getDescription(), affectedNodeId);

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

    // Restart edit session after undo with the affected node.
    // If the affected node was deleted by the undo (e.g. undoing AddNodeCommand),
    // fall back to whatever node is currently selected.
    auto restartSession = [&](gint64 nodeId) {
        auto& ts = _pMainWin->get_tree_store();
        if (nodeId != -1 && ts.get_node_from_node_id(nodeId)) {
            beginTextEditSession(nodeId);
        } else {
            curr_iter = _pMainWin->curr_tree_iter();
            if (curr_iter) beginTextEditSession(curr_iter.get_node_id());
        }
    };
    restartSession(affectedNodeId);

    // Restore cursor and scroll after layout settles.
    // Use PRIORITY_LOW so this runs after GTK's size-allocation pass — for nodes
    // with many anchored widgets, PRIORITY_DEFAULT_IDLE fires too early and
    // scroll_to computes the wrong pixel position.
    if (not _pMainWin->no_gui()) {
        Glib::signal_idle().connect_once([this, pMainWin = _pMainWin, scrollTargetOffset, savedScrollVal, cmdScrollPos](){
            // Disconnect the signal_changed fixup — this idle callback takes over
            _scrollFixupConnection.disconnect();
            if (auto buf = pMainWin->curr_buffer()) {
                Gtk::TextView& tv = pMainWin->get_text_view().mm();
                auto adj = pMainWin->getScrolledwindowText().get_vadjustment();
                // Place cursor in main buffer (only meaningful for non-widget commands)
                if (cmdScrollPos < 0 && scrollTargetOffset >= 0) {
                    int maxOff = buf->get_char_count();
                    auto targetIter = buf->get_iter_at_offset(std::min(scrollTargetOffset, maxOff));
                    buf->place_cursor(targetIter);
                }
                // Restore scroll position
                adj->set_value(savedScrollVal);
                // Always verify cursor is visible — the stored scroll position may
                // be stale (e.g. captured at node load before the user scrolled).
                {
                    Gdk::Rectangle iterRect, visibleRect;
                    auto cursorIter = buf->get_iter_at_mark(buf->get_insert());
                    tv.get_iter_location(cursorIter, iterRect);
                    tv.get_visible_rect(visibleRect);
                    if (iterRect.get_y() < visibleRect.get_y() ||
                        iterRect.get_y() + iterRect.get_height() > visibleRect.get_y() + visibleRect.get_height())
                    {
                        tv.scroll_to(cursorIter, CtTextView::TEXT_SCROLL_MARGIN);
                    }
                }
            }
            pMainWin->get_text_view().mm().queue_draw();
        }, Glib::PRIORITY_LOW);
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

    // End any active edit session before redo so intermediate edits become
    // a separate undo entry.  Using cancel() would discard the edits from the
    // command history while leaving the model/buffer modified, desynchronizing
    // the redo stack's expected model state.
    endTextEditSession();

    // Get the node ID and cursor position from command being redone
    auto cmd = _commandManager.peekRedoCommand();
    if (!cmd) {
        spdlog::debug("CtCommandBridge: no command to redo");
        _currentOp = BridgeOp::None;
        return;
    }

    gint64 affectedNodeId = cmd->getNodeId();
    _pendingCursorPos = cmd->getNewCursorPos();
    int const scrollTargetOffset = _pendingCursorPos;
    // Use the command's stored scroll position if available (reliable for all command types).
    double const cmdScrollPos = cmd->getNewScrollPos();

    // Save current scroll position as fallback before the buffer gets rebuilt
    double savedScrollVal = 0;
    if (not _pMainWin->no_gui()) {
        savedScrollVal = _pMainWin->getScrolledwindowText().get_vadjustment()->get_value();
    }
    // Prefer command's stored scroll position over current vadjustment
    if (cmdScrollPos >= 0) {
        savedScrollVal = cmdScrollPos;
    }

    // Pass scroll position to the observer so it can restore it synchronously
    // after rebuilding the buffer, preventing a visible flash to the top of the node.
    _pendingScrollPos = _pMainWin->no_gui() ? -1.0 : savedScrollVal;

    // Switch to the affected node if it's different from current
    auto curr_iter = _pMainWin->curr_tree_iter();
    if (affectedNodeId != -1 && curr_iter && curr_iter.get_node_id() != affectedNodeId) {
        auto& treeStore = _pMainWin->get_tree_store();
        auto affectedIter = treeStore.get_node_from_node_id(affectedNodeId);
        if (affectedIter) {
            _pMainWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(affectedIter));
        }
    }

    spdlog::debug("CtCommandBridge::redo - about to redo '{}' for node {}",
                  cmd->getDescription(), affectedNodeId);

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

    // Restart edit session after redo with the affected node.
    // Fall back to curr_tree_iter if the node was deleted by the redo.
    {
        auto& ts = _pMainWin->get_tree_store();
        if (affectedNodeId != -1 && ts.get_node_from_node_id(affectedNodeId)) {
            beginTextEditSession(affectedNodeId);
        } else {
            curr_iter = _pMainWin->curr_tree_iter();
            if (curr_iter) beginTextEditSession(curr_iter.get_node_id());
        }
    }

    // Restore cursor and scroll — see undo() for rationale.
    if (not _pMainWin->no_gui()) {
        Glib::signal_idle().connect_once([this, pMainWin = _pMainWin, scrollTargetOffset, savedScrollVal, cmdScrollPos](){
            // Disconnect the signal_changed fixup — this idle callback takes over
            _scrollFixupConnection.disconnect();
            if (auto buf = pMainWin->curr_buffer()) {
                Gtk::TextView& tv = pMainWin->get_text_view().mm();
                auto adj = pMainWin->getScrolledwindowText().get_vadjustment();
                // Place cursor in main buffer (only meaningful for non-widget commands)
                if (cmdScrollPos < 0 && scrollTargetOffset >= 0) {
                    int maxOff = buf->get_char_count();
                    auto targetIter = buf->get_iter_at_offset(std::min(scrollTargetOffset, maxOff));
                    buf->place_cursor(targetIter);
                }
                // Restore scroll position
                adj->set_value(savedScrollVal);
                // Always verify cursor is visible — the stored scroll position may
                // be stale (e.g. captured at node load before the user scrolled).
                {
                    Gdk::Rectangle iterRect, visibleRect;
                    auto cursorIter = buf->get_iter_at_mark(buf->get_insert());
                    tv.get_iter_location(cursorIter, iterRect);
                    tv.get_visible_rect(visibleRect);
                    if (iterRect.get_y() < visibleRect.get_y() ||
                        iterRect.get_y() + iterRect.get_height() > visibleRect.get_y() + visibleRect.get_height())
                    {
                        tv.scroll_to(cursorIter, CtTextView::TEXT_SCROLL_MARGIN);
                    }
                }
            }
            pMainWin->get_text_view().mm().queue_draw();
        }, Glib::PRIORITY_LOW);
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

    spdlog::debug("CtCommandBridge: undoing {} command(s)", actualCount);

    // Call single-step undo() for each operation to ensure proper UI updates.
    // Stop on first failure to avoid cascading errors and command loss.
    for (size_t i = 0; i < actualCount; ++i) {
        size_t stackBefore = _commandManager.getUndoStackDescriptions().size();
        undo();
        if (_commandManager.getUndoStackDescriptions().size() >= stackBefore) break;
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

    spdlog::debug("CtCommandBridge: redoing {} command(s)", actualCount);

    // Call single-step redo() for each operation to ensure proper UI updates.
    // Stop on first failure to avoid cascading errors and command loss.
    for (size_t i = 0; i < actualCount; ++i) {
        size_t stackBefore = _commandManager.getRedoStackDescriptions().size();
        redo();
        if (_commandManager.getRedoStackDescriptions().size() >= stackBefore) break;
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

    // Re-sync the node model from the buffer to ensure model matches buffer.
    // This handles cases where the buffer was modified without an active session
    // (e.g., widget insertion, key events not captured by session).
    // After undo/redo, skip this — the model is the source of truth and the buffer
    // was rebuilt from it. Re-syncing would overwrite the correct model state.
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
            node->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
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
            newNode->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
            _docModel->addNode(newNode, 0 /* flat model — no hierarchy needed for undo */);
            spdlog::info("CtCommandBridge: lazy-added node {} to model on first edit visit", nodeId);
        } else if (nodeId == _lastSyncedNodeId) {
            // The previous session for this node ended cleanly — delta commands kept
            // the model in sync.  Verify model/buffer agree before skipping re-sync:
            // a new empty node can end its first session with "no changes" (both empty),
            // but between sessions the buffer may receive keypress content that the
            // model never saw.
            int bufLen = buffer->get_char_count();
            int modLen = static_cast<int>(node->getContent().length());
            if (modLen != bufLen) {
                spdlog::warn("CtCommandBridge: model/buffer length mismatch for node {} "
                             "(model={}, buffer={}) — forcing re-sync despite delta-sync flag",
                             nodeId, modLen, bufLen);
                node->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
            } else {
                spdlog::debug("CtCommandBridge: skipping model re-sync for node {} (delta-synced)", nodeId);
            }
        } else if (node->getContent().isEmpty() && buffer->get_char_count() > 0) {
            // Node was registered by initializeFromExistingDocument() (props-only, no content).
            // Sync content from the live buffer before starting the edit session.
            spdlog::info("CtCommandBridge: syncing content for props-only node {} on first edit visit", nodeId);
            node->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
        } else if (buffer->get_modified()) {
            // Buffer was modified outside a tracked session (e.g., switched back from
            // another node, or widget edit changed the buffer).  Re-sync from XML.
            spdlog::info("CtCommandBridge: re-syncing model from buffer for node {} (buffer modified)", nodeId);
            node->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
        }
    }

    // Capture scroll position at session start so it can be restored on undo
    {
        auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
        _sessionScrollPosOld = adj ? adj->get_value() : -1.0;
    }

    // Begin signal-based capture
    _editSession->begin(nodeId, buffer, &treeIter);

    // Also capture tag apply/remove signals so that link insertion and other
    // tag-only operations within a text edit session produce proper undo entries.
    _editSession->startTagCapture(buffer);

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

    // Drain any pending tag changes (e.g. link tag applied during this session)
    auto tagChanges = _editSession->drainAndCoalesceTagChanges();
    _editSession->stopTagCapture();

    // End session and get command — always a CompoundCommand of delta commands
    auto cmd = _editSession->end(buffer, widgets, cursorPos);

    // If we have tag changes, inject them into the compound command
    if (!tagChanges.empty()) {
        CompoundCommand* cc = cmd ? dynamic_cast<CompoundCommand*>(cmd.get()) : nullptr;
        // If no text-edit command was created, create a new compound for tags only
        if (!cc) {
            auto tagCompound = std::make_unique<CompoundCommand>(
                "[" + std::to_string(nodeId) + "] Format");
            tagCompound->setNodeId(nodeId);
            tagCompound->setDocumentModel(_docModel);
            tagCompound->setOldCursorPos(cursorPos);
            tagCompound->setNewCursorPos(cursorPos);
            cmd = std::move(tagCompound);
            cc = dynamic_cast<CompoundCommand*>(cmd.get());
        }
        for (const auto& tc : tagChanges) {
            auto [attr, val] = decomposeTagName(tc.tagName);
            if (attr.empty()) {
                spdlog::warn("CtCommandBridge: endTextEditSession — unknown tag '{}', skipping", tc.tagName);
                continue;
            }
            int len = tc.end - tc.start;
            if (tc.isApply) {
                cc->addCommand(std::make_unique<ApplyFormatCommand>(
                    _docModel, nodeId, tc.start, len, attr, val, -1, -1));
            } else {
                cc->addCommand(std::make_unique<RemoveFormatCommand>(
                    _docModel, nodeId, tc.start, len, attr, -1, -1));
            }
        }
    }

    if (cmd) {
        spdlog::info("CtCommandBridge: session created command, adding to undo stack");

        // Stamp scroll positions so undo/redo can restore the viewport
        if (auto* cc = dynamic_cast<CompoundCommand*>(cmd.get())) {
            double scrollPosNew = -1.0;
            auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
            if (adj) scrollPosNew = adj->get_value();
            // _sessionScrollPosOld == 0 is likely stale from node load (captured
            // before images were laid out).  Fall back to the current scroll so
            // undoing the first edit doesn't jump to the top of the node.
            double scrollPosOld = _sessionScrollPosOld;
            if (scrollPosOld == 0 && scrollPosNew > 0) {
                scrollPosOld = scrollPosNew;
            }
            cc->setOldScrollPos(scrollPosOld);
            cc->setNewScrollPos(scrollPosNew);
        }

        // Model was kept in sync during session via signal handlers for all node types
        // Delta commands provide full undo/redo without needing XML snapshots
        _commandManager.addCommandToStack(std::move(cmd));
    } else {
        spdlog::debug("CtCommandBridge: session ended but no command created (no changes)");
    }

    // Mark this node's model as in sync — delta commands maintained it.
    // The next beginTextEditSession for the same node can skip the expensive
    // buffer re-serialization (which re-encodes every PNG image).
    _lastSyncedNodeId = nodeId;
}

void CtCommandBridge::cancelTextEditSession()
{
    if (!_active) {
        return;
    }

    if (_editSession) {
        _editSession->cancel();
    }
    _lastSyncedNodeId = -1;  // cancelled session — can't guarantee model sync
}

void CtCommandBridge::onCursorMoved()
{
    if (!_active || !_editSession || !_editSession->isActive()) return;
    if (!_editSession->hasCapturedCommands()) return;
    gint64 nodeId = _editSession->getActiveNodeId();
    endTextEditSession();
    beginTextEditSession(nodeId);
}

void CtCommandBridge::beginWidgetEdit(gint64 nodeId, CtAnchoredWidget* widget, int row, int col)
{
    if (!_active || !_pMainWin) {
        _lastSyncedNodeId = -1;
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
    // Invalidate AFTER endTextEditSession (which sets _lastSyncedNodeId = nodeId)
    _lastSyncedNodeId = -1;

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
    // When focus is inside a child widget (table cell, codebox), the node
    // buffer's insert mark is stale (often 0).  Use the widget's char offset
    // so that undo scrolls to the widget rather than jumping to the top.
    _widgetEditOldCursorPos = widget ? widget->getOffset()
                                     : buffer->get_insert()->get_iter().get_offset();
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
        _widgetEditIsRichCell = false;
        _widgetEditOldContent = codebox->get_text_content().raw();
        // Delta path tracks codebox text directly; no content snapshot needed.
        _widgetEditInitialContent = CtNodeContent{};
        spdlog::debug("CtCommandBridge: beginWidgetEdit (delta path) codebox at offset {}",
                      _widgetEditCharOffset);
    } else if (widget && row >= 0 && col >= 0 &&
               widget->get_type() == CtAnchWidgType::TableRich) {
        _widgetEditCharOffset = widget->getOffset();
        _widgetEditRow = row;
        _widgetEditCol = col;
        _widgetEditIsRichCell = true;
        if (_docModel) {
            auto node = _docModel->getNodeById(nodeId);
            if (node) {
                auto desc = node->getContent().getWidgetDescAt(widget->getOffset());
                if (desc.hasRichTableData() &&
                    (size_t)row < desc.richTableData.size() &&
                    (size_t)col < desc.richTableData[row].size()) {
                    _widgetEditOldCellContent = desc.richTableData[row][col];
                }
            }
        }
        // Rich-cell edits are tracked via _richCellSession + EditRichCellCommand;
        // no content snapshot needed (and re-encoding every image would be expensive).
        _widgetEditInitialContent = CtNodeContent{};
        // Start a cell-level edit session for per-word undo granularity
        auto* richTable = static_cast<CtTableRich*>(widget);
        auto cellBuf = richTable->get_buffer(row, col);
        if (cellBuf && _richCellSession) {
            if (_richCellSession->isActive()) _richCellSession->cancel();
            _richCellSession->begin(nodeId, cellBuf);
        }
        spdlog::debug("CtCommandBridge: beginWidgetEdit (delta) rich cell ({},{}) at offset {}",
                      row, col, _widgetEditCharOffset);
    } else if (widget && row >= 0 && col >= 0 &&
               (widget->get_type() == CtAnchWidgType::TableLight ||
                widget->get_type() == CtAnchWidgType::TableHeavy)) {
        _widgetEditCharOffset = widget->getOffset();
        _widgetEditRow = row;
        _widgetEditCol = col;
        _widgetEditIsRichCell = false;
        if (widget->get_type() == CtAnchWidgType::TableLight) {
            _widgetEditOldContent = static_cast<CtTableLight*>(widget)->get_cell_text(row, col).raw();
        } else {
            auto buf = static_cast<CtTableHeavy*>(widget)->get_buffer(row, col);
            _widgetEditOldContent = buf ? buf->get_text().raw() : std::string{};
        }
        // Delta path tracks the cell text directly; no content snapshot needed.
        _widgetEditInitialContent = CtNodeContent{};
        spdlog::debug("CtCommandBridge: beginWidgetEdit (delta path) table cell ({},{}) at offset {}",
                      row, col, _widgetEditCharOffset);
    } else {
        _widgetEditCharOffset = -1;
        _widgetEditRow = -1;
        _widgetEditCol = -1;
        _widgetEditIsRichCell = false;
        _widgetEditOldContent.clear();
        _widgetEditInitialContent = buildContentFromBuffer(buffer, treeIter.get_anchored_widgets());
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
        _widgetEditInitialContent = CtNodeContent{};
        _widgetEditOldCursorPos = -1;
        _widgetEditOldScrollPos = -1.0;
        _widgetEditCharOffset = -1;
        _widgetEditRow = -1;
        _widgetEditCol = -1;
        _widgetEditIsRichCell = false;
        _widgetEditOldContent.clear();
        _widgetEditOldCellContent = CtCellContent{};
        if (_richCellSession && _richCellSession->isActive()) _richCellSession->cancel();
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

    // When focus is inside a child widget the node buffer insert mark is stale;
    // use the widget's char offset so undo/redo scrolls to the widget.
    int newCursorPos = (_widgetEditCharOffset >= 0) ? _widgetEditCharOffset
                     : buffer->get_insert()->get_iter().get_offset();

    // Save state before clearState() zeroes it
    const gint64 savedNodeId = _widgetEditNodeId;
    const int savedCharOffset = _widgetEditCharOffset;
    const int savedRow = _widgetEditRow;
    const int savedCol = _widgetEditCol;

    // Delta path: find the widget by stored char offset and compare content.
    if (savedCharOffset != -1) {
        auto widgetList = treeIter.get_anchored_widgets();

        if (savedRow >= 0) {
            // Table delta path: compare single cell content.
            CtCellContent newRichContent;
            Glib::ustring newCellText;
            bool foundRich = false;
            bool foundWidget = false;
            for (auto* w : widgetList) {
                if (w->getOffset() == savedCharOffset) {
                    if (w->get_type() == CtAnchWidgType::TableRich) {
                        auto* rt = static_cast<CtTableRich*>(w);
                        newRichContent = rt->getRichCell(savedRow, savedCol)->extractContent();
                        foundRich = true;
                    } else if (w->get_type() == CtAnchWidgType::TableLight) {
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

            if (foundRich) {
                // Flush the cell session (creates an EditRichCellCommand if there are pending changes)
                flushRichCellSession();
                clearState();
                if (!isInUndoRedo()) {
                    _currentOp = BridgeOp::None;
                    beginTextEditSession(savedNodeId);
                }
                return;
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

    // Fallback for unknown/unsupported widget types — no delta command available.
    // Sync model so display stays coherent, but undo is not recorded.
    CtNodeContent finalContent = buildContentFromBuffer(buffer, treeIter.get_anchored_widgets());

    if (_widgetEditInitialContent == finalContent) {
        spdlog::debug("CtCommandBridge: endWidgetEdit - no changes detected");
    } else {
        spdlog::warn("CtCommandBridge: endWidgetEdit - unknown widget type modified; model synced but undo not available");
        auto node = _docModel->getNodeById(_widgetEditNodeId);
        if (node) node->setContent(finalContent);
    }
    clearState();

    // Restart the outer-buffer edit session so edits after the widget are captured.
    // Skip during undo/redo — those operations restart the session themselves.
    if (!isInUndoRedo()) {
        _currentOp = BridgeOp::None;
        beginTextEditSession(savedNodeId);
    }
}

bool CtCommandBridge::isTrackingRichCell() const
{
    return _currentOp == BridgeOp::TrackingWidget && _widgetEditIsRichCell;
}

Glib::RefPtr<Gtk::TextBuffer> CtCommandBridge::getActiveRichCellBuffer()
{
    if (!isTrackingRichCell() || !_pMainWin) return {};
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_widgetEditNodeId);
    if (!treeIter) return {};
    for (auto* w : treeIter.get_anchored_widgets()) {
        if (w->getOffset() == _widgetEditCharOffset && w->get_type() == CtAnchWidgType::TableRich) {
            return static_cast<CtTableRich*>(w)->getRichCell(_widgetEditRow, _widgetEditCol)->get_buffer();
        }
    }
    return {};
}

CtRichCell* CtCommandBridge::getActiveRichCellPtr() const
{
    if (!isTrackingRichCell() || !_pMainWin) return nullptr;
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_widgetEditNodeId);
    if (!treeIter) return nullptr;
    for (auto* w : treeIter.get_anchored_widgets()) {
        if (w->getOffset() == _widgetEditCharOffset && w->get_type() == CtAnchWidgType::TableRich) {
            return static_cast<CtTableRich*>(w)->getRichCell(_widgetEditRow, _widgetEditCol);
        }
    }
    return nullptr;
}

void CtCommandBridge::flushRichCellSession()
{
    if (!_richCellSession || !_richCellSession->isActive()) return;
    if (!isTrackingRichCell() || !_pMainWin) {
        _richCellSession->cancel();
        return;
    }

    // Find the rich table and cell
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_widgetEditNodeId);
    if (!treeIter) { _richCellSession->cancel(); return; }

    CtTableRich* richTable = nullptr;
    for (auto* w : treeIter.get_anchored_widgets()) {
        if (w->getOffset() == _widgetEditCharOffset && w->get_type() == CtAnchWidgType::TableRich) {
            richTable = static_cast<CtTableRich*>(w);
            break;
        }
    }
    if (!richTable) { _richCellSession->cancel(); return; }

    auto* cell = richTable->getRichCell(_widgetEditRow, _widgetEditCol);
    if (!cell) { _richCellSession->cancel(); return; }

    auto cellBuf = cell->get_buffer();

    // Check if the session captured any buffer insert/erase events before ending it.
    // (Format tag changes don't fire these signals, so the session would be empty.)
    bool hadCaptures = _richCellSession->hasCapturedCommands();

    // End the session to get a compound command (used for its description only)
    std::list<CtAnchoredWidget*> emptyWidgets;
    int cellCursorPos = cellBuf ? cellBuf->get_insert()->get_iter().get_offset() : -1;
    auto compound = _richCellSession->end(cellBuf, emptyWidgets, cellCursorPos);
    // Use the widget's char offset in the node buffer for undo/redo cursor
    // positioning — the cell buffer cursor offset is meaningless in the node buffer.
    int cursorPos = _widgetEditCharOffset;

    // Extract current cell content
    CtCellContent newContent = cell->extractContent();

    if (newContent == _widgetEditOldCellContent) {
        // No net change — restart session without pushing a command
        if (cellBuf) {
            _richCellSession->begin(_widgetEditNodeId, cellBuf);
        }
        return;
    }

    // No compound command: either all captured commands had empty descriptions
    // (e.g. spaces only) or content changed outside signal capture (e.g. format
    // tag applied before commitRichCellFormatChange).  Don't create an undo
    // entry here — format changes are handled by commitRichCellFormatChange,
    // and space-only edits match main-page behaviour.  Only update the baseline
    // when text signals were actually captured (space-only case); for format-tag
    // changes (no captures) leave the baseline for commitRichCellFormatChange.
    if (!compound) {
        if (hadCaptures) {
            if (_docModel) {
                auto node = _docModel->getNodeById(_widgetEditNodeId);
                if (node) {
                    node->getContent().setWidgetRichTableCell(
                        _widgetEditCharOffset, _widgetEditRow, _widgetEditCol, newContent);
                }
            }
            _widgetEditOldCellContent = newContent;
        }
        if (cellBuf) {
            _richCellSession->begin(_widgetEditNodeId, cellBuf);
        }
        return;
    }

    // Build description from the compound command
    std::string desc;
    if (compound) {
        desc = compound->getDescription();
        // Strip "N " prefix — EditRichCellCommand adds its own
        std::string prefix = "[" + std::to_string(_widgetEditNodeId) + "] ";
        if (desc.find(prefix) == 0) {
            desc = desc.substr(prefix.length());
        }
    }

    // Sync model
    if (_docModel) {
        auto node = _docModel->getNodeById(_widgetEditNodeId);
        if (node) {
            node->getContent().setWidgetRichTableCell(
                _widgetEditCharOffset, _widgetEditRow, _widgetEditCol, newContent);
        }
    }

    auto cmd = std::make_unique<EditRichCellCommand>(
        _docModel,
        this,
        _widgetEditNodeId,
        _widgetEditCharOffset,
        (size_t)_widgetEditRow,
        (size_t)_widgetEditCol,
        _widgetEditOldCellContent,
        newContent,
        _widgetEditOldCursorPos,
        cursorPos,
        std::move(desc));

    double scrollPosNew = -1.0;
    auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
    if (adj) scrollPosNew = adj->get_value();
    cmd->setOldScrollPos(_widgetEditOldScrollPos);
    cmd->setNewScrollPos(scrollPosNew);

    spdlog::info("CtCommandBridge: flushRichCellSession — EditRichCellCommand added");
    _commandManager.addCommandToStack(std::move(cmd));

    // Update baseline for next flush
    _widgetEditOldCellContent = newContent;
    _widgetEditOldCursorPos = cursorPos;

    // Begin a new cell session for the next word
    if (cellBuf) {
        _richCellSession->begin(_widgetEditNodeId, cellBuf);
    }
}

void CtCommandBridge::cancelRichCellSession()
{
    if (_richCellSession && _richCellSession->isActive()) {
        _richCellSession->cancel();
    }
}

void CtCommandBridge::applyRichCellInPlace(gint64 nodeId,
                                           int charOffset,
                                           size_t row,
                                           size_t col,
                                           const CtCellContent& content)
{
    spdlog::debug("applyRichCellInPlace: node={} off={} ({},{}) widgets={} spans={}",
                  nodeId, charOffset, row, col,
                  content.embeddedWidgets.size(), content.textSpans.size());
    // Update the model first.
    if (_docModel) {
        if (auto node = _docModel->getNodeById(nodeId)) {
            node->getContent().setWidgetRichTableCell(charOffset, row, col, content);
        }
    }

    // Update the live CtRichCell widget directly so we don't have to tear
    // down and rebuild the whole table widget tree (which crashes when the
    // focused cell text view is destroyed mid-rebuild).
    Glib::RefPtr<Gtk::TextBuffer> cellBuf;
    bool foundCell = false;
    if (_pMainWin) {
        auto treeIter = _pMainWin->get_tree_store().get_node_from_node_id(nodeId);
        if (treeIter) {
            for (auto* w : treeIter.get_anchored_widgets()) {
                if (w->getOffset() == charOffset && w->get_type() == CtAnchWidgType::TableRich) {
                    auto* richTable = static_cast<CtTableRich*>(w);
                    if (auto* cell = richTable->getRichCell(row, col)) {
                        foundCell = true;
                        spdlog::debug("applyRichCellInPlace: calling populateFromContent on cell {:p}", (void*)cell);
                        // Cancel any active rich cell signal capture before
                        // mutating the cell buffer — populateFromContent fires
                        // erase/insert signals that would otherwise be captured
                        // as edits and turn into a spurious EditRichCellCommand
                        // on the next flushRichCellSession (clobbering the redo
                        // stack via addCommandToStack).
                        if (_richCellSession && _richCellSession->isActive()) {
                            _richCellSession->cancel();
                        }
                        cell->populateFromContent(content);
                        cellBuf = cell->get_buffer();
                        spdlog::debug("applyRichCellInPlace: after populateFromContent, cell buffer char count={}",
                                      cellBuf ? cellBuf->get_char_count() : -1);
                    }
                    break;
                }
            }
        }
    }
    if (!foundCell) {
        spdlog::warn("applyRichCellInPlace: did NOT find rich cell at ({},{}) off={} for node {}",
                     row, col, charOffset, nodeId);
    }

    // If we're still tracking this cell, resync the baseline so that the next
    // flushRichCellSession sees newContent == baseline and bails out cleanly,
    // and restart the session against the (now-rewritten) cell buffer.
    if (_widgetEditNodeId == nodeId &&
        _widgetEditCharOffset == charOffset &&
        _widgetEditRow == (int)row &&
        _widgetEditCol == (int)col)
    {
        _widgetEditOldCellContent = content;
        if (_richCellSession && cellBuf) {
            _richCellSession->begin(nodeId, cellBuf);
        }
    }

    // Tell the observer to skip the full rebuild for this notification —
    // we've already brought GTK in line with the model above.
    _skipNextRebuildNodeId = nodeId;
    if (_docModel) {
        _docModel->notifyNodeChanged(nodeId);
    }
    _skipNextRebuildNodeId = -1;
}

void CtCommandBridge::commitRichCellFormatChange(std::string description)
{
    if (!isTrackingRichCell() || !_pMainWin) return;

    // Flush any pending text edits so they get their own undo entry before the format change
    flushRichCellSession();

    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(_widgetEditNodeId);
    if (!treeIter) return;

    CtTableRich* richTable = nullptr;
    for (auto* w : treeIter.get_anchored_widgets()) {
        if (w->getOffset() == _widgetEditCharOffset && w->get_type() == CtAnchWidgType::TableRich) {
            richTable = static_cast<CtTableRich*>(w);
            break;
        }
    }
    if (!richTable) return;

    auto* cell = richTable->getRichCell(_widgetEditRow, _widgetEditCol);
    CtCellContent newContent = cell->extractContent();

    if (newContent == _widgetEditOldCellContent) {
        // No change — but ensure the session is restarted if it was cancelled
        auto cellBuffer = cell->get_buffer();
        if (_richCellSession && !_richCellSession->isActive() && cellBuffer) {
            _richCellSession->begin(_widgetEditNodeId, cellBuffer);
        }
        return;
    }

    // Sync model
    if (_docModel) {
        auto node = _docModel->getNodeById(_widgetEditNodeId);
        if (node) {
            node->getContent().setWidgetRichTableCell(
                _widgetEditCharOffset, _widgetEditRow, _widgetEditCol, newContent);
        }
    }

    auto cellBuffer = cell->get_buffer();
    // Use the widget's char offset in the node buffer for undo/redo cursor
    // positioning — the cell buffer cursor offset is meaningless in the node buffer.
    int cursorPos = _widgetEditCharOffset;

    auto cmd = std::make_unique<EditRichCellCommand>(
        _docModel,
        this,
        _widgetEditNodeId,
        _widgetEditCharOffset,
        _widgetEditRow,
        _widgetEditCol,
        _widgetEditOldCellContent,
        newContent,
        cursorPos,
        cursorPos,
        std::move(description));

    double scrollPosNew = -1.0;
    auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
    if (adj) scrollPosNew = adj->get_value();
    cmd->setOldScrollPos(_widgetEditOldScrollPos);
    cmd->setNewScrollPos(scrollPosNew);

    _commandManager.addCommandToStack(std::move(cmd));

    // Update baseline so subsequent edits in the same cell track from the new state.
    _widgetEditOldCellContent = newContent;

    // Ensure the session is active for subsequent edits (it may have been
    // cancelled before a bulk operation like paste).
    if (_richCellSession && !_richCellSession->isActive() && cellBuffer) {
        _richCellSession->begin(_widgetEditNodeId, cellBuffer);
    }

    spdlog::info("CtCommandBridge: commitRichCellFormatChange — EditRichCellCommand added");
}

// Decompose a GTK tag name like "weight_heavy" into (attribute, value) pair.
// Returns ("", "") for unknown / non-formatting tags.
static std::pair<std::string, std::string> decomposeTagName(const std::string& tagName)
{
    if (str::startswith(tagName, CtConst::TAG_WEIGHT_PREFIX))        return {CtConst::TAG_WEIGHT,        tagName.substr(7)};
    if (str::startswith(tagName, CtConst::TAG_FOREGROUND_PREFIX))    return {CtConst::TAG_FOREGROUND,    tagName.substr(11)};
    if (str::startswith(tagName, CtConst::TAG_BACKGROUND_PREFIX))    return {CtConst::TAG_BACKGROUND,    tagName.substr(11)};
    if (str::startswith(tagName, CtConst::TAG_SCALE_PREFIX))         return {CtConst::TAG_SCALE,         tagName.substr(6)};
    if (str::startswith(tagName, CtConst::TAG_JUSTIFICATION_PREFIX)) return {CtConst::TAG_JUSTIFICATION, tagName.substr(14)};
    if (str::startswith(tagName, CtConst::TAG_STYLE_PREFIX))         return {CtConst::TAG_STYLE,         tagName.substr(6)};
    if (str::startswith(tagName, CtConst::TAG_UNDERLINE_PREFIX))     return {CtConst::TAG_UNDERLINE,     tagName.substr(10)};
    if (str::startswith(tagName, CtConst::TAG_STRIKETHROUGH_PREFIX)) return {CtConst::TAG_STRIKETHROUGH, tagName.substr(14)};
    if (str::startswith(tagName, CtConst::TAG_INDENT_PREFIX))        return {CtConst::TAG_INDENT,        tagName.substr(7)};
    if (str::startswith(tagName, CtConst::TAG_FAMILY_PREFIX))        return {CtConst::TAG_FAMILY,        tagName.substr(7)};
    if (str::startswith(tagName, CtConst::TAG_LINK_PREFIX))          return {CtConst::TAG_LINK,          tagName.substr(5)};
    return {"", ""};
}

void CtCommandBridge::beginFormatChange(gint64 nodeId, const std::string& formatType)
{
    _lastSyncedNodeId = -1;  // format change modifies buffer outside delta tracking
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

    auto buffer = _pMainWin->curr_buffer();
    if (!buffer) {
        spdlog::error("CtCommandBridge: no current buffer for format change");
        return;
    }

    _currentOp = BridgeOp::CapturingFormat;
    _formatChangeNodeId = nodeId;
    _captureFormatType = formatType;
    _formatChangeOldCursorPos = buffer->get_insert()->get_iter().get_offset();

    // Start tag-signal capture — no XML snapshot needed
    _editSession->startTagCapture(buffer);
}

void CtCommandBridge::endFormatChange()
{
    if (!_active || !_pMainWin || _currentOp != BridgeOp::CapturingFormat) {
        return;
    }

    // Stop tag capture and drain changes before anything else
    auto tagChanges = _editSession->drainAndCoalesceTagChanges();
    _editSession->stopTagCapture();

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

    _formatChangeNewCursorPos = buffer->get_insert()->get_iter().get_offset();
    // Clear op BEFORE executeCommand so observer notifications flow through normally
    _currentOp = BridgeOp::None;

    if (tagChanges.empty()) {
        // No-op (e.g. toggling off a format that wasn't applied, or applying same justify)
        spdlog::debug("CtCommandBridge: endFormatChange — no tag changes, skipping command");
        return;
    }

    // Build a CompoundCommand wrapping one sub-command per coalesced tag change.
    // The compound description uses the human-friendly formatType (e.g. "bold").
    std::string desc = "[" + std::to_string(_formatChangeNodeId)
                       + "] Format (" + _captureFormatType + ")";
    auto compound = std::make_unique<CompoundCommand>(desc);
    compound->setNodeId(_formatChangeNodeId);
    compound->setDocumentModel(_docModel);
    compound->setOldCursorPos(_formatChangeOldCursorPos);
    compound->setNewCursorPos(_formatChangeNewCursorPos);

    for (const auto& tc : tagChanges) {
        auto [attr, val] = decomposeTagName(tc.tagName);
        if (attr.empty()) {
            spdlog::warn("CtCommandBridge: endFormatChange — unknown tag '{}', skipping", tc.tagName);
            continue;
        }
        int len = tc.end - tc.start;
        if (tc.isApply) {
            compound->addCommand(std::make_unique<ApplyFormatCommand>(
                _docModel, _formatChangeNodeId, tc.start, len, attr, val, -1, -1));
        } else {
            compound->addCommand(std::make_unique<RemoveFormatCommand>(
                _docModel, _formatChangeNodeId, tc.start, len, attr, -1, -1));
        }
    }

    if (compound->isEmpty()) {
        return;
    }

    spdlog::info("CtCommandBridge: endFormatChange — {} tag change(s), executing command",
                 tagChanges.size());
    executeCommand(std::move(compound));
}

// Apply formatting via model-first path
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
    auto cmd = std::make_unique<ApplyFormatCommand>(
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
                newNode->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
                _docModel->addNode(newNode, 0);
            } else if (buffer->get_modified()) {
                node->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
            }
        }
        _skipNextModelSync = false;

        {
            auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
            _sessionScrollPosOld = adj ? adj->get_value() : -1.0;
        }

        _currentOp = BridgeOp::CapturingPaste;
        _captureNodeId = nodeId;
        _editSession->begin(nodeId, buffer, &treeIter);
    } else {
        // XML snapshot path (default, safe for all paste types)
        beginXmlCapture(BridgeOp::CapturingPaste, nodeId);
    }
}

void CtCommandBridge::endPaste()
{
    if (!_active || !_pMainWin || _currentOp != BridgeOp::CapturingPaste) return;

    gint64 pasteNodeId = _captureNodeId;

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
                cc->setDescription("[" + std::to_string(_captureNodeId) + "] Paste clipboard");
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

    // Re-apply cell borders on all rich tables in the node. Pasting content
    // can shift widget positions, and GTK3 may fail to repaint the border
    // windows of embedded text views after the re-layout.
    _refreshRichTableBorders(pasteNodeId);
}

void CtCommandBridge::_refreshRichTableBorders(gint64 nodeId)
{
    auto& treeStore = _pMainWin->get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) return;
    for (auto* w : treeIter.get_anchored_widgets()) {
        if (w->get_type() == CtAnchWidgType::TableRich) {
            static_cast<CtTableRich*>(w)->refreshCellBorders();
        }
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
            newNode->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
            _docModel->addNode(newNode, 0);
        } else if (buffer->get_modified()) {
            node->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
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
    _editSession->begin(nodeId, buffer, &treeIter);
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
            cc->setDescription("[" + std::to_string(_captureNodeId) + "] Cut clipboard");
            double scrollPosNew = -1.0;
            auto adj = _pMainWin->getScrolledwindowText().get_vadjustment();
            if (adj) scrollPosNew = adj->get_value();
            cc->setOldScrollPos(_sessionScrollPosOld);
            cc->setNewScrollPos(scrollPosNew);
        }
        spdlog::info("CtCommandBridge: cut command created (delta), adding to undo stack");
        addCommandToStack(std::move(cmd));
    }

    gint64 cutNodeId = _captureNodeId;
    _currentOp = BridgeOp::None;
    _captureNodeId = 0;
    _refreshRichTableBorders(cutNodeId);
}

void CtCommandBridge::beginXmlCapture(BridgeOp op, gint64 nodeId)
{
    _lastSyncedNodeId = -1;  // XML capture modifies buffer outside delta tracking
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

    // Lazy: ensure node is in model before capturing initial state.
    if (!_docModel->getNodeById(nodeId) && treeIter) {
        auto newNode = std::make_shared<CtNodeModel>(nodeId);
        newNode->setName(treeIter.get_node_name());
        newNode->setSyntax(treeIter.get_node_syntax_highlighting());
        newNode->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
        _docModel->addNode(newNode, 0);
    }

    _currentOp = op;
    _captureNodeId = nodeId;
    _captureInitialContent = buildContentFromBuffer(buffer, treeIter.get_anchored_widgets());
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

    CtNodeContent finalContent = buildContentFromBuffer(buffer, treeIter.get_anchored_widgets());
    _captureNewCursorPos = buffer->property_cursor_position();

    if (_captureInitialContent == finalContent) {
        _currentOp = BridgeOp::None;
        return;
    }

    auto cmd = std::make_unique<TextEditCommand>(
        _docModel,
        _captureNodeId,
        _captureInitialContent,
        finalContent,
        _captureOldCursorPos,
        _captureNewCursorPos,
        description
    );

    spdlog::info("CtCommandBridge: {} command created, adding to undo stack", description);
    addCommandToStack(std::move(cmd));

    // Update document model
    auto node = _docModel->getNodeById(_captureNodeId);
    if (node) {
        node->setContent(finalContent);
        _docModel->notifyNodeChanged(_captureNodeId);
    }

    _currentOp = BridgeOp::None;
    _captureNodeId = 0;
    _captureInitialContent = CtNodeContent{};
}

void CtCommandBridge::commitWidgetModification(
    gint64 nodeId,
    int charOffset,
    const CtWidgetDesc& oldDesc,
    const CtWidgetDesc& newDesc,
    const std::string& description)
{
    // Capture cursor and scroll position so undo/redo restores them
    int cursorPos = -1;
    double scrollPos = -1.0;
    if (_pMainWin && !_pMainWin->no_gui()) {
        if (auto buf = _pMainWin->curr_buffer()) {
            cursorPos = buf->get_insert()->get_iter().get_offset();
        }
        scrollPos = _pMainWin->getScrolledwindowText().get_vadjustment()->get_value();
    }

    auto cmd = std::make_unique<ModifyWidgetDeltaCommand>(
        _docModel, nodeId, charOffset, oldDesc, newDesc, description,
        cursorPos, cursorPos);
    cmd->setOldScrollPos(scrollPos);
    cmd->setNewScrollPos(scrollPos);
    addCommandToStack(std::move(cmd));
    auto node = _docModel->getNodeById(nodeId);
    if (node) {
        node->getContent().replaceWidget(charOffset, newDesc);
    }
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

    // Create and add node to model, syncing all properties from GTK
    auto nodeModel = std::make_shared<CtNodeModel>(nodeId);
    nodeModel->setName(treeIter.get_node_name());
    nodeModel->setSyntax(treeIter.get_node_syntax_highlighting());
    nodeModel->setTags(treeIter.get_node_tags());
    nodeModel->setReadOnly(treeIter.get_node_read_only());
    nodeModel->setBold(treeIter.get_node_is_bold());
    nodeModel->setCustomIconId(treeIter.get_node_custom_icon_id());
    nodeModel->setForegroundRgb24(treeIter.get_node_foreground());
    nodeModel->setExcludedFromSearch(treeIter.get_node_is_excluded_from_search());
    nodeModel->setChildrenExcludedFromSearch(treeIter.get_node_children_are_excluded_from_search());
    nodeModel->setCreationTime(treeIter.get_node_creating_time());
    nodeModel->setLastSaveTime(treeIter.get_node_modification_time());
    nodeModel->setSharedMasterId(treeIter.get_node_shared_master_id());
    nodeModel->setSequence(treeIter.get_node_sequence());
    auto buffer = treeIter.get_node_text_buffer();
    if (buffer) {
        nodeModel->setContent(buildContentFromBuffer(buffer, treeIter.get_anchored_widgets()));
    }

    _docModel->addNode(nodeModel, parentId);
    spdlog::debug("CtCommandBridge: registered new node {} '{}' (parent: {})",
                  nodeId, treeIter.get_node_name().c_str(), parentId);
}

void CtCommandBridge::updateBufferFromXml(Glib::RefPtr<Gtk::TextBuffer> buffer, const Glib::ustring& xml, const std::string& syntax, const CtTreeIter* treeIter)
{
    // XML-based buffer updates are needed to properly restore widgets during undo/redo
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
        spdlog::error("CtCommandBridge: XML deserialization failed (Glib::Error): {}", std::string{e.what()});
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

    // applyRichCellInPlace already updated the live cell widget; skip the
    // full rebuild path so we don't tear down and recreate the focused cell
    // text view (which triggers a GTK assertion).
    if (_bridge->_skipNextRebuildNodeId == nodeId) {
        spdlog::debug("BridgeObserver::onNodeChanged: skipping rebuild for node {} (in-place rich cell update)", nodeId);
        // Still consume pending cursor/scroll so they don't get lost or applied
        // to a later, unrelated notification.
        if (_bridge->_pendingCursorPos >= 0) {
            auto treeIter = _bridge->_pMainWin->get_tree_store().get_node_from_node_id(nodeId);
            if (treeIter) {
                auto buf = treeIter.get_node_text_buffer();
                if (buf) {
                    int cursor_offset = _bridge->_pendingCursorPos;
                    int max_offset = buf->get_char_count();
                    auto restore_iter = buf->get_iter_at_offset(std::min(cursor_offset, max_offset));
                    buf->place_cursor(restore_iter);
                }
            }
            _bridge->_pendingCursorPos = -1;
        }
        if (_bridge->_pendingScrollPos >= 0) {
            auto adj = _bridge->_pMainWin->getScrolledwindowText().get_vadjustment();
            if (adj) {
                adj->set_value(_bridge->_pendingScrollPos);
            }
            _bridge->_pendingScrollPos = -1.0;
        }
        return;
    }

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
            // Restore scroll position synchronously to prevent a visible flash
            // to the top of the node.  Widgets may not be fully laid out yet so
            // this value gets clamped — the signal_changed handler below and the
            // deferred idle callback in undo()/redo() will re-apply it as widgets
            // are sized and the vadjustment upper increases.
            if (_bridge->_pendingScrollPos >= 0) {
                auto adj = _bridge->_pMainWin->getScrolledwindowText().get_vadjustment();
                if (adj) {
                    adj->set_value(_bridge->_pendingScrollPos);

                    // Keep restoring scroll whenever vadjustment bounds change
                    // (i.e. as widgets are laid out and the content height grows).
                    // This fires during GTK's layout phase, BEFORE the draw, so
                    // the user never sees a frame at the wrong scroll position.
                    double targetScroll = _bridge->_pendingScrollPos;
                    _bridge->_scrollFixupConnection.disconnect();
                    _bridge->_scrollFixupConnection = adj->signal_changed().connect(
                        [adj, targetScroll]() {
                            double maxVal = adj->get_upper() - adj->get_page_size();
                            if (maxVal > 0) {
                                adj->set_value(std::min(targetScroll, maxVal));
                            }
                        });
                }
                _bridge->_pendingScrollPos = -1.0;
            }
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
    // Suspend user_active for the duration of the programmatic rebuild so
    // signal handlers (focus-out, motion, button-press) on cells about to be
    // destroyed bail out early instead of touching marks on buffers that
    // are mid-teardown.
    bool const prevUserActive = _bridge->_pMainWin->user_active();
    _bridge->_pMainWin->user_active() = false;
    struct UserActiveRestore {
        CtMainWin* win; bool prev;
        ~UserActiveRestore() { win->user_active() = prev; }
    } userActiveRestore{_bridge->_pMainWin, prevUserActive};

    // Grab focus back to the main text view before deleting widgets — if a
    // rich cell's text view still has focus, destroying it triggers GTK to
    // access the cell buffer's insert mark whose internal line pointer is
    // already NULL, causing an assertion crash.
    // Skip when no_gui=true (headless tests): no rich cells have focus, and
    // grab_focus() on a hidden window can hang waiting for X11 focus events.
    if (attachToView && !_bridge->_pMainWin->no_gui()) {
        _bridge->_pMainWin->get_text_view().mm().grab_focus();
    }

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
    } catch (const std::exception& e) {
        spdlog::error("BridgeObserver::buildBufferForNode: exception in buildBufferFromContent for node {}: {}", nodeId, e.what());
        buf->set_text("(Content reconstruction failed due to corrupt data)");
    } catch (...) {
        spdlog::error("BridgeObserver::buildBufferForNode: unknown exception in buildBufferFromContent for node {}", nodeId);
        buf->set_text("(Content reconstruction failed due to corrupt data)");
    }
    CT_SOURCE_BUFFER_END_NOT_UNDOABLE(pGtkSourceBuffer);

    if (!new_widgets.empty()) {
        Gtk::TextView* pView = attachToView ? &_bridge->_pMainWin->get_text_view().mm() : nullptr;
        _bridge->_pMainWin->get_tree_store().addAnchoredWidgets(iter, new_widgets, pView);

        // Apply current zoom level to the freshly-created widgets. Otherwise
        // undo/redo that rebuilds the buffer would reset images and tables to
        // their unzoomed size until the user navigates away and back.
        if (attachToView && iter.get_node_is_rich_text()) {
            const double scaleFactor = _bridge->_pMainWin->get_rt_zoom_scale_factor();
            if (scaleFactor != 1.0) {
                for (auto* widget : new_widgets) {
                    const auto wtype = widget->get_type();
                    if (wtype == CtAnchWidgType::ImagePng or wtype == CtAnchWidgType::ImageLatex) {
                        static_cast<CtImage*>(widget)->apply_zoom(scaleFactor);
                    }
                    else if (wtype == CtAnchWidgType::CodeBox) {
                        static_cast<CtCodebox*>(widget)->apply_zoom(scaleFactor);
                    }
                    else if (wtype == CtAnchWidgType::TableHeavy) {
                        static_cast<CtTableHeavy*>(widget)->apply_zoom(scaleFactor);
                    }
                    else if (wtype == CtAnchWidgType::TableLight) {
                        static_cast<CtTableLight*>(widget)->apply_zoom(scaleFactor);
                    }
                    else if (wtype == CtAnchWidgType::TableRich) {
                        auto* pTable = static_cast<CtTableRich*>(widget);
                        pTable->apply_zoom(scaleFactor);
                        for (size_t r = 0; r < pTable->get_num_rows(); ++r) {
                            for (size_t c = 0; c < pTable->get_num_columns(); ++c) {
                                for (auto* emb : pTable->getRichCell(r, c)->getEmbeddedWidgets()) {
                                    const auto embType = emb->get_type();
                                    if (embType == CtAnchWidgType::ImagePng or embType == CtAnchWidgType::ImageLatex) {
                                        static_cast<CtImage*>(emb)->apply_zoom(scaleFactor);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

}

void CtCommandBridge::BridgeObserver::onNodeAdded(gint64 nodeId, gint64 parentId)
{
    if (!_bridge || !_bridge->_pMainWin) return;

    spdlog::debug("BridgeObserver: node {} added to parent {}", nodeId, parentId);

    auto node = _bridge->_docModel->getNodeById(nodeId);
    if (!node) {
        spdlog::error("BridgeObserver: cannot add node {}, not found in model", nodeId);
        return;
    }

    CtNodeData nodeData;
    nodeData.nodeId           = nodeId;
    nodeData.sharedNodesMasterId = node->getSharedMasterId();
    nodeData.sequence         = node->getSequence();
    nodeData.name             = node->getName();
    nodeData.syntax           = node->getSyntax();
    nodeData.tags             = node->getTags();
    nodeData.isReadOnly       = node->isReadOnly();
    nodeData.isBold           = node->isBold();
    nodeData.customIconId     = node->getCustomIconId();
    nodeData.foregroundRgb24  = node->getForegroundRgb24();
    nodeData.excludeMeFromSearch       = node->isExcludedFromSearch();
    nodeData.excludeChildrenFromSearch = node->areChildrenExcludedFromSearch();
    nodeData.tsCreation       = node->getCreationTime();
    nodeData.tsLastSave       = node->getLastSaveTime();

    // Create text buffer from model content so the node is immediately usable
    // without falling back to (not-yet-saved) storage.
    if (nodeData.sharedNodesMasterId <= 0) {
        nodeData.pTextBuffer = _bridge->_pMainWin->get_new_text_buffer();
        const auto& content = node->getContent();
        if (!content.isEmpty()) {
            nodeData.anchoredWidgets = buildBufferFromContent(
                content, nodeData.pTextBuffer, _bridge->_pMainWin);
        }
    }

    auto& treeStore = _bridge->_pMainWin->get_tree_store();

    // Determine GTK parent iter
    Gtk::TreeModel::iterator* parentIter = nullptr;
    Gtk::TreeModel::iterator parentGtkIter;
    if (parentId > 0) {  // 0 is the virtual root
        auto parentCtIter = treeStore.get_node_from_node_id(parentId);
        if (parentCtIter) {
            parentGtkIter = static_cast<Gtk::TreeModel::iterator>(parentCtIter);
            parentIter = &parentGtkIter;
        }
    }

    // Honor position from model: find where this node sits among its siblings
    auto modelNode = node;
    auto modelParent = modelNode->getParent();
    int position = -1;
    if (modelParent) {
        const auto& siblings = modelParent->getChildren();
        for (int i = 0; i < static_cast<int>(siblings.size()); ++i) {
            if (siblings[i]->getNodeId() == nodeId) { position = i; break; }
        }
    }

    Gtk::TreeModel::iterator newGtkIter;
    if (position == 0) {
        // Prepend as first child
        auto pGtkStore = treeStore.get_store();
        Gtk::TreeModel::iterator prependIter;
        if (parentIter) {
            prependIter = pGtkStore->prepend((*parentIter)->children());
        } else {
            prependIter = pGtkStore->prepend(pGtkStore->children());
        }
        treeStore.update_node_data(prependIter, nodeData);
        treeStore.update_node_aux_icon(prependIter);
        newGtkIter = prependIter;
    } else if (position > 0 && modelParent) {
        // Insert after (position-1)th sibling in GTK
        const auto& siblings = modelParent->getChildren();
        gint64 prevSibId = siblings[position - 1]->getNodeId();
        CtTreeIter prevSibGtk = treeStore.get_node_from_node_id(prevSibId);
        if (prevSibGtk) {
            auto pGtkStore = treeStore.get_store();
            auto insertedIter = pGtkStore->insert_after(static_cast<Gtk::TreeModel::iterator>(prevSibGtk));
            treeStore.update_node_data(insertedIter, nodeData);
            treeStore.update_node_aux_icon(insertedIter);
            newGtkIter = insertedIter;
        } else {
            newGtkIter = treeStore.append_node(&nodeData, parentIter);
        }
    } else {
        newGtkIter = treeStore.append_node(&nodeData, parentIter);
    }

    // Queue a pending-new so storage knows about this node
    if (newGtkIter) {
        treeStore.to_ct_tree_iter(newGtkIter).pending_new_db_node();
    }

    spdlog::debug("BridgeObserver: added node {} to GTK tree at position {}", nodeId, position);
}

void CtCommandBridge::BridgeObserver::onNodeDeleted(gint64 nodeId)
{
    if (!_bridge || !_bridge->_pMainWin) return;

    spdlog::debug("BridgeObserver: node {} deleted", nodeId);

    auto& win      = *_bridge->_pMainWin;
    auto& treeStore = win.get_tree_store();

    // Remove from navigation history
    for (auto it = win._visitedNodes.begin(); it != win._visitedNodes.end(); ) {
        if (*it == nodeId) {
            it = win._visitedNodes.erase(it);
            if (win._visitedNodesIdx > 0) --win._visitedNodesIdx;
        } else { ++it; }
    }

    // Remove bookmark (silent — DeleteNodeCommand already updated the menu before pushing)
    bool wasBookmarked = treeStore.bookmarks_remove(nodeId);
    (void)wasBookmarked;  // menu refresh handled by the command after full subtree removal

    auto nodeIter = treeStore.get_node_from_node_id(nodeId);
    if (!nodeIter) {
        spdlog::warn("BridgeObserver: node {} not found in GTK tree for deletion", nodeId);
        return;
    }

    // If this is the currently selected node, pre-select a neighbour and clear
    // _prevTreeIter before erasing — otherwise the cursor-changed handler fires
    // on the stale (dangling) iterator and segfaults.
    CtTreeIter currIter = win.curr_tree_iter();
    if (currIter && currIter.get_node_id() == nodeId) {
        Gtk::TreeModel::iterator nextSel = --static_cast<Gtk::TreeModel::iterator>(nodeIter);
        if (!nextSel) nextSel = ++static_cast<Gtk::TreeModel::iterator>(nodeIter);
        if (!nextSel) nextSel = static_cast<Gtk::TreeModel::iterator>(nodeIter)->parent();

        win.resetPrevTreeIter();
        if (nextSel) {
            win.get_tree_view().set_cursor_safe(nextSel);
        } else {
            // Tree will be empty after this deletion
            win.curr_buffer()->set_text("");
            win.window_header_update();
            win.update_selected_node_statusbar_info();
            win.get_text_view().mm().set_sensitive(false);
        }
    }

    // Remove from GTK tree (children already removed by prior bottom-up notifications)
    auto gtkStore = treeStore.get_store();
    if (gtkStore) {
        gtkStore->erase(static_cast<Gtk::TreeModel::iterator>(nodeIter));
        spdlog::debug("BridgeObserver: removed node {} from GTK tree", nodeId);
    }

    // Keep storage pending state consistent: remove any pending write for this
    // node and schedule it for DB removal (covers both forward delete and
    // undo-of-add; pending_new_db_node cancels the rm_set entry on re-add).
    treeStore.pending_rm_db_nodes({nodeId});
}

void CtCommandBridge::BridgeObserver::onNodeMoved(gint64 nodeId, gint64 newParentId, int newPosition)
{
    if (!_bridge || !_bridge->_pMainWin) return;

    spdlog::debug("BridgeObserver: node {} moved to parent {} at position {}",
                  nodeId, newParentId, newPosition);

    auto& treeStore = _bridge->_pMainWin->get_tree_store();

    // Snapshot expanded paths by nodeId before erasing
    std::set<gint64> expandedIds;
    auto& treeView = _bridge->_pMainWin->get_tree_view();
    treeStore.get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& it) {
        if (treeView.row_expanded(treeStore.get_store()->get_path(it))) {
            CtTreeIter ci = treeStore.to_ct_tree_iter(it);
            if (ci) expandedIds.insert(ci.get_node_id());
        }
        return false;
    });

    auto nodeIter = treeStore.get_node_from_node_id(nodeId);
    if (!nodeIter) {
        spdlog::error("BridgeObserver: cannot move node {}, not found in GTK tree", nodeId);
        return;
    }
    auto node = _bridge->_docModel->getNodeById(nodeId);
    if (!node) {
        spdlog::error("BridgeObserver: cannot move node {}, not found in model", nodeId);
        return;
    }

    // Save all text buffers AND anchored-widget lists from the GTK subtree BEFORE
    // erasing it.  GTK implicitly destroys children when the parent row is erased,
    // so we must harvest both to avoid null-buffer rows and missing widget references
    // (e.g. CtTableRich cells showing as broken placeholders) after re-insert.
    struct SavedNodeState {
        Glib::RefPtr<Gtk::TextBuffer>    buffer;
        std::list<CtAnchoredWidget*>     widgets;
    };
    std::unordered_map<gint64, SavedNodeState> savedState;
    std::function<void(const Gtk::TreeModel::iterator&)> collectState;
    collectState = [&](const Gtk::TreeModel::iterator& it) {
        CtTreeIter ci = treeStore.to_ct_tree_iter(it);
        if (ci) {
            SavedNodeState s;
            s.buffer  = ci.get_node_text_buffer();
            s.widgets = ci.get_anchored_widgets_fast('n'); // 'n' = no sort, just fetch
            savedState[ci.get_node_id()] = std::move(s);
        }
        for (const auto& child : it->children())
            collectState(child);
    };
    collectState(static_cast<Gtk::TreeModel::iterator>(nodeIter));

    // Build CtNodeData helper — restores saved buffer and widget list for the row.
    auto makeNodeData = [&](const std::shared_ptr<CtNodeModel>& n) {
        CtNodeData d;
        d.nodeId              = n->getNodeId();
        d.sharedNodesMasterId = n->getSharedMasterId();
        d.sequence            = n->getSequence();
        d.name                = n->getName();
        d.syntax              = n->getSyntax();
        d.tags                = n->getTags();
        d.isReadOnly          = n->isReadOnly();
        d.isBold              = n->isBold();
        d.customIconId        = n->getCustomIconId();
        d.foregroundRgb24     = n->getForegroundRgb24();
        d.excludeMeFromSearch       = n->isExcludedFromSearch();
        d.excludeChildrenFromSearch = n->areChildrenExcludedFromSearch();
        d.tsCreation          = n->getCreationTime();
        d.tsLastSave          = n->getLastSaveTime();
        auto it = savedState.find(n->getNodeId());
        if (it != savedState.end()) {
            d.pTextBuffer    = it->second.buffer;
            d.anchoredWidgets = it->second.widgets;
        } else if (d.sharedNodesMasterId <= 0) {
            // Node was never visited — rebuild buffer+widgets from model content.
            d.pTextBuffer = _bridge->_pMainWin->get_new_text_buffer();
            const auto& content = n->getContent();
            if (!content.isEmpty())
                d.anchoredWidgets = buildBufferFromContent(content, d.pTextBuffer, _bridge->_pMainWin);
        }
        return d;
    };

    // Erase old position (children erased implicitly by GTK TreeStore).
    // Reset _prevTreeIter first so the cursor-changed signal fired by the erase
    // doesn't dereference a dangling iterator (same fix as onNodeDeleted).
    auto gtkStore = treeStore.get_store();
    auto& win = *_bridge->_pMainWin;
    CtTreeIter currSel = win.curr_tree_iter();
    if (currSel && currSel.get_node_id() == nodeId) {
        win.resetPrevTreeIter();
    }
    gtkStore->erase(static_cast<Gtk::TreeModel::iterator>(nodeIter));

    // Determine new parent iter
    Gtk::TreeModel::iterator* newParentGtkPtr = nullptr;
    Gtk::TreeModel::iterator newParentGtkIter;
    if (newParentId > 0) {
        auto parentCtIter = treeStore.get_node_from_node_id(newParentId);
        if (parentCtIter) {
            newParentGtkIter = static_cast<Gtk::TreeModel::iterator>(parentCtIter);
            newParentGtkPtr  = &newParentGtkIter;
        }
    }

    // Insert at correct position
    auto modelParent = node->getParent();
    int pos = -1;
    if (modelParent) {
        const auto& sibs = modelParent->getChildren();
        for (int i = 0; i < static_cast<int>(sibs.size()); ++i) {
            if (sibs[i]->getNodeId() == nodeId) { pos = i; break; }
        }
    }

    CtNodeData topData = makeNodeData(node);
    Gtk::TreeModel::iterator newGtkIter;
    if (pos == 0) {
        auto it = newParentGtkPtr ? gtkStore->prepend((*newParentGtkPtr)->children())
                                  : gtkStore->prepend(gtkStore->children());
        treeStore.update_node_data(it, topData);
        treeStore.update_node_aux_icon(it);
        newGtkIter = it;
    } else if (pos > 0 && modelParent) {
        gint64 prevId = modelParent->getChildren()[pos - 1]->getNodeId();
        CtTreeIter prevGtk = treeStore.get_node_from_node_id(prevId);
        if (prevGtk) {
            auto it = gtkStore->insert_after(static_cast<Gtk::TreeModel::iterator>(prevGtk));
            treeStore.update_node_data(it, topData);
            treeStore.update_node_aux_icon(it);
            newGtkIter = it;
        } else {
            newGtkIter = treeStore.append_node(&topData, newParentGtkPtr);
        }
    } else {
        newGtkIter = treeStore.append_node(&topData, newParentGtkPtr);
    }

    // Recursively rebuild children from model (buffers restored from savedBuffers)
    std::function<void(gint64, Gtk::TreeModel::iterator*)> rebuildChildren;
    rebuildChildren = [&](gint64 pid, Gtk::TreeModel::iterator* parentGtk) {
        auto pn = _bridge->_docModel->getNodeById(pid);
        if (!pn) return;
        for (const auto& child : pn->getChildren()) {
            CtNodeData cd = makeNodeData(child);
            auto childCtIter = treeStore.append_node(&cd, parentGtk);
            auto childGtk = static_cast<Gtk::TreeModel::iterator>(childCtIter);
            rebuildChildren(child->getNodeId(), &childGtk);
        }
    };
    auto newGtkIterForChildren = newGtkIter;
    rebuildChildren(nodeId, &newGtkIterForChildren);

    // Mark the moved node for DB hierarchy update — without this, the DB still
    // records the old father_id and _remove_db_node_with_children will cascade-
    // delete this node if the old parent is later removed.
    treeStore.to_ct_tree_iter(newGtkIter).pending_edit_db_node_hier();

    // Re-apply expansion state
    treeStore.get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& it) {
        CtTreeIter ci = treeStore.to_ct_tree_iter(it);
        if (ci && expandedIds.count(ci.get_node_id())) {
            treeView.expand_row(treeStore.get_store()->get_path(it), false);
        }
        return false;
    });

    // Fix sequences at both old parent (newParentId is already the new one)
    treeStore.nodes_sequences_fix(Gtk::TreeModel::iterator(), true);

    // Keep the moved node selected
    CtTreeIter movedIter = treeStore.get_node_from_node_id(nodeId);
    if (movedIter) {
        treeView.set_cursor_safe(movedIter);
    }

    _bridge->_pMainWin->update_window_save_needed();
    spdlog::debug("BridgeObserver: moved node {} to parent {} at position {}",
                  nodeId, newParentId, pos);
}

void CtCommandBridge::BridgeObserver::onTreeStructureChanged()
{
    if (!_bridge || !_bridge->_pMainWin) return;
    spdlog::debug("BridgeObserver: tree structure changed — full rebuild");

    auto& win       = *_bridge->_pMainWin;
    auto& treeStore  = win.get_tree_store();
    auto& treeView   = win.get_tree_view();
    auto  gtkStore   = treeStore.get_store();

    // Snapshot expanded node ids and currently selected node
    std::set<gint64> expandedIds;
    gint64 selectedId = -1;
    {
        CtTreeIter sel = win.curr_tree_iter();
        if (sel) selectedId = sel.get_node_id();
    }
    gtkStore->foreach_iter([&](const Gtk::TreeModel::iterator& it) {
        if (treeView.row_expanded(gtkStore->get_path(it))) {
            CtTreeIter ci = treeStore.to_ct_tree_iter(it);
            if (ci) expandedIds.insert(ci.get_node_id());
        }
        return false;
    });

    // Clear GTK tree
    win.resetPrevTreeIter();
    gtkStore->clear();

    // Rebuild from model depth-first
    std::function<void(const std::shared_ptr<CtNodeModel>&, Gtk::TreeModel::iterator*)> rebuild;
    rebuild = [&](const std::shared_ptr<CtNodeModel>& node, Gtk::TreeModel::iterator* parentGtk) {
        CtNodeData d;
        d.nodeId              = node->getNodeId();
        d.sharedNodesMasterId = node->getSharedMasterId();
        d.sequence            = node->getSequence();
        d.name                = node->getName();
        d.syntax              = node->getSyntax();
        d.tags                = node->getTags();
        d.isReadOnly          = node->isReadOnly();
        d.isBold              = node->isBold();
        d.customIconId        = node->getCustomIconId();
        d.foregroundRgb24     = node->getForegroundRgb24();
        d.excludeMeFromSearch       = node->isExcludedFromSearch();
        d.excludeChildrenFromSearch = node->areChildrenExcludedFromSearch();
        d.tsCreation          = node->getCreationTime();
        d.tsLastSave          = node->getLastSaveTime();
        auto gtkIter = treeStore.append_node(&d, parentGtk);
        auto gtkIterForChildren = static_cast<Gtk::TreeModel::iterator>(gtkIter);
        for (const auto& child : node->getChildren()) {
            rebuild(child, &gtkIterForChildren);
        }
    };

    auto root = _bridge->_docModel->getRootNode();
    if (root) {
        for (const auto& child : root->getChildren()) {
            rebuild(child, nullptr);
        }
    }

    // Re-apply expansion
    gtkStore->foreach_iter([&](const Gtk::TreeModel::iterator& it) {
        CtTreeIter ci = treeStore.to_ct_tree_iter(it);
        if (ci && expandedIds.count(ci.get_node_id())) {
            treeView.expand_row(gtkStore->get_path(it), false);
        }
        return false;
    });

    // Restore selection
    if (selectedId >= 0) {
        CtTreeIter sel = treeStore.get_node_from_node_id(selectedId);
        if (sel) treeView.set_cursor_safe(sel);
    }

    win.update_window_save_needed();
}

void CtCommandBridge::BridgeObserver::onNodePropertiesChanged(
        gint64 nodeId, const CtNodeProps& oldProps, const CtNodeProps& newProps)
{
    if (!_bridge || !_bridge->_pMainWin) return;

    auto& win       = *_bridge->_pMainWin;
    auto& treeStore  = win.get_tree_store();
    CtTreeIter treeIter = treeStore.get_node_from_node_id(nodeId);
    if (!treeIter) {
        spdlog::error("BridgeObserver::onNodePropertiesChanged: node {} not in GTK tree", nodeId);
        return;
    }

    bool prevUA = win.user_active();
    win.user_active() = false;
    struct UAGuard { bool& ref; bool prev; ~UAGuard() { ref = prev; } } ua{win.user_active(), prevUA};

    // Update all GTK columns from newProps
    const auto& cols = treeStore.get_columns();
    auto gtkIter = static_cast<Gtk::TreeModel::iterator>(treeIter);
    gtkIter->set_value(cols.colNodeName, newProps.name);
    gtkIter->set_value(cols.colSyntaxHighlighting, newProps.syntax);
    gtkIter->set_value(cols.colNodeTags, newProps.tags);
    gtkIter->set_value(cols.colNodeIsReadOnly, newProps.isReadOnly);
    gtkIter->set_value(cols.colNodeIsExcludedFromSearch, newProps.excludeMeFromSearch);
    gtkIter->set_value(cols.colNodeChildrenAreExcludedFromSearch, newProps.excludeChildrenFromSearch);
    gtkIter->set_value(cols.colCustomIconId, static_cast<guint16>(newProps.customIconId));
    gtkIter->set_value(cols.colForeground, newProps.foregroundRgb24);
    gtkIter->set_value(cols.colWeight,
        CtTreeIter::get_pango_weight_from_is_bold(newProps.isBold));
    gtkIter->set_value(cols.colTsCreation, newProps.tsCreation);
    gtkIter->set_value(cols.colTsLastSave, newProps.tsLastSave);

    // Syntax change: rebuild buffer
    if (oldProps.syntax != newProps.syntax) {
        win.switch_buffer_text_source(
            treeIter.get_node_text_buffer(), treeIter,
            newProps.syntax, oldProps.syntax);
    }

    // Refresh main icon when syntax or custom icon changed
    if (oldProps.syntax != newProps.syntax || oldProps.customIconId != newProps.customIconId) {
        treeStore.update_node_icon(treeIter);
    }

    // Update aux icon (read-only, custom icon, etc.)
    treeStore.update_node_aux_icon(treeIter);

    // If this is the currently-displayed node, refresh header/statusbar/textview
    CtTreeIter currIter = win.curr_tree_iter();
    if (currIter && currIter.get_node_id() == nodeId) {
        win.window_header_update();
        win.window_header_update_lock_icon(newProps.isReadOnly);
        win.window_header_update_ghost_icon(
            newProps.excludeMeFromSearch || newProps.excludeChildrenFromSearch);
        win.update_selected_node_statusbar_info();
        win.get_text_view().mm().set_editable(!newProps.isReadOnly);
    }

    // Refresh bookmark menu if this node is bookmarked
    if (treeStore.is_node_bookmarked(nodeId)) {
        win.menu_set_bookmark_menu_items();
    }

    treeIter.pending_edit_db_node_prop();
    win.update_window_save_needed(CtSaveNeededUpdType::npro);
}
