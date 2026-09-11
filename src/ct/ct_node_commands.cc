/*
 * ct_node_commands.cc
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

#include "ct_node_commands.h"
#include "ct_main_win.h"
#include "ct_treestore.h"
#include "ct_logging.h"

// ─── Conversions ──────────────────────────────────────────────────────────────

CtNodeProps nodePropsFromIter(const CtTreeIter& iter)
{
    CtNodeProps p;
    p.name                    = iter.get_node_name();
    p.syntax                  = iter.get_node_syntax_highlighting();
    p.tags                    = iter.get_node_tags();
    p.isReadOnly               = iter.get_node_read_only();
    p.isBold                  = iter.get_node_is_bold();
    p.customIconId             = iter.get_node_custom_icon_id();
    p.foregroundRgb24          = iter.get_node_foreground();
    p.excludeMeFromSearch      = iter.get_node_is_excluded_from_search();
    p.excludeChildrenFromSearch = iter.get_node_children_are_excluded_from_search();
    p.lineWrap                 = iter.get_node_line_wrap();
    p.tsCreation               = iter.get_node_creating_time();
    p.tsLastSave               = iter.get_node_modification_time();
    return p;
}

CtNodeProps nodePropsFromData(const CtNodeData& d)
{
    CtNodeProps p;
    p.name                    = d.name;
    p.syntax                  = d.syntax;
    p.tags                    = d.tags;
    p.isReadOnly               = d.isReadOnly;
    p.isBold                  = d.isBold;
    p.customIconId             = d.customIconId;
    p.foregroundRgb24          = d.foregroundRgb24;
    p.excludeMeFromSearch      = d.excludeMeFromSearch;
    p.excludeChildrenFromSearch = d.excludeChildrenFromSearch;
    p.lineWrap                 = d.lineWrap;
    p.tsCreation               = d.tsCreation;
    p.tsLastSave               = d.tsLastSave;
    return p;
}

void nodeDataFromModel(const CtNodeModel& node, CtNodeData& d)
{
    d.nodeId              = node.getNodeId();
    d.sharedNodesMasterId = node.getSharedMasterId();
    d.sequence            = node.getSequence();
    d.name                = node.getName();
    d.syntax              = node.getSyntax();
    d.tags                = node.getTags();
    d.isReadOnly          = node.isReadOnly();
    d.isBold              = node.isBold();
    d.customIconId        = node.getCustomIconId();
    d.foregroundRgb24     = node.getForegroundRgb24();
    d.excludeMeFromSearch       = node.isExcludedFromSearch();
    d.excludeChildrenFromSearch = node.areChildrenExcludedFromSearch();
    d.lineWrap            = node.isLineWrap();
    d.tsCreation          = node.getCreationTime();
    d.tsLastSave          = node.getLastSaveTime();
    d.drawingCanvases     = node.getDrawingCanvases();
}

// ─── EditNodePropertiesCommand ────────────────────────────────────────────────

EditNodePropertiesCommand::EditNodePropertiesCommand(CtDocumentModel* model,
                                                     gint64 nodeId,
                                                     const CtNodeProps& oldProps,
                                                     const CtNodeProps& newProps)
    : _model(model), _nodeId(nodeId), _oldProps(oldProps), _newProps(newProps)
{}

void EditNodePropertiesCommand::execute()
{
    auto node = _model->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("EditNodePropertiesCommand::execute — node {} not found", _nodeId);
        return;
    }
    node->applyProps(_newProps);
    _model->notifyNodePropertiesChanged(_nodeId, _oldProps, _newProps);
}

void EditNodePropertiesCommand::undo()
{
    auto node = _model->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("EditNodePropertiesCommand::undo — node {} not found", _nodeId);
        return;
    }
    node->applyProps(_oldProps);
    _model->notifyNodePropertiesChanged(_nodeId, _newProps, _oldProps);
}

std::string EditNodePropertiesCommand::getDescription() const
{
    return "[" + std::to_string(_nodeId) + "] Edit node properties";
}

// ─── AddNodeCommand ───────────────────────────────────────────────────────────

AddNodeCommand::AddNodeCommand(CtDocumentModel* model,
                               gint64 nodeId,
                               gint64 parentId,
                               int position,
                               const CtNodeProps& props,
                               const CtNodeContent& initialContent,
                               gint64 sharedMasterId,
                               std::vector<CtDrawingCanvas> drawingCanvases)
    : _model(model), _nodeId(nodeId), _parentId(parentId), _position(position),
      _props(props), _initialContent(initialContent), _sharedMasterId(sharedMasterId),
      _drawingCanvases(std::move(drawingCanvases))
{}

void AddNodeCommand::execute()
{
    // Re-use existing node if it was previously created (redo path)
    if (!_model->getNodeById(_nodeId)) {
        auto node = _model->createNode(_nodeId);
        if (!node) {
            spdlog::error("AddNodeCommand::execute — createNode({}) failed", _nodeId);
            return;
        }
        node->applyProps(_props);
        node->setContent(_initialContent);
        node->setSharedMasterId(_sharedMasterId);
        node->getDrawingCanvasesMut() = _drawingCanvases;
    }
    auto node = _model->getNodeById(_nodeId);
    if (!node) return;

    _model->addNode(node, _parentId, _position);
    _model->notifyNodeAdded(_nodeId, _parentId);
}

void AddNodeCommand::undo()
{
    _model->removeNode(_nodeId);
    _model->notifyNodeDeleted(_nodeId);
}

std::string AddNodeCommand::getDescription() const
{
    return "[" + std::to_string(_nodeId) + "] Add node";
}

// ─── DeleteNodeCommand ────────────────────────────────────────────────────────

DeleteNodeCommand::DeleteNodeCommand(CtDocumentModel* model,
                                     CtMainWin* pMainWin,
                                     const SubtreeSnapshot& snap,
                                     std::vector<SharedGroupPromotion> promotions,
                                     std::vector<gint64> bookmarkedIds,
                                     std::vector<gint64> visitedSnapshot,
                                     size_t visitedIdx,
                                     gint64 nextSelectedId)
    : _model(model), _pMainWin(pMainWin), _snap(snap),
      _promotions(std::move(promotions)),
      _bookmarkedIds(std::move(bookmarkedIds)),
      _visitedSnapshot(std::move(visitedSnapshot)),
      _visitedIdx(visitedIdx),
      _nextSelectedId(nextSelectedId)
{}

void DeleteNodeCommand::execute()
{
    if (_snap.entries.empty()) return;

    // 1. Apply shared-master promotions before removing nodes
    for (const auto& promo : _promotions) {
        if (promo.newMasterId == 0) continue;
        auto newMaster = _model->getNodeById(promo.newMasterId);
        if (newMaster) {
            newMaster->applyProps(promo.oldMasterProps);
            newMaster->setContent(promo.oldMasterContent);
            newMaster->setSharedMasterId(0);
            _model->notifyNodePropertiesChanged(promo.newMasterId, promo.newMasterPriorProps, promo.oldMasterProps);
            _model->notifyNodeChanged(promo.newMasterId);
        }
        for (const auto& [memberId, origMaster] : promo.rePointed) {
            auto member = _model->getNodeById(memberId);
            if (member) member->setSharedMasterId(promo.newMasterId);
        }
    }

    // 2. Remove the subtree (removeNodeWithChildren fires onNodeDeleted bottom-up)
    gint64 topId = _snap.entries.front().nodeId;

    // Select next node before removal so GTK doesn't auto-select a deleted node
    if (_nextSelectedId > 0 && _pMainWin) {
        auto& ts = _pMainWin->get_tree_store();
        CtTreeIter nextIter = ts.get_node_from_node_id(_nextSelectedId);
        if (nextIter) {
            _pMainWin->get_tree_view().set_cursor_safe(nextIter);
        }
    } else if (_pMainWin) {
        _pMainWin->curr_buffer()->set_text("");
        _pMainWin->window_header_update();
        _pMainWin->update_selected_node_statusbar_info();
        _pMainWin->get_text_view().mm().set_sensitive(false);
    }

    _model->removeNodeWithChildren(topId);
    // Observer (onNodeDeleted) handles GTK remove + nav history trim
    // Observer also handles bookmark removal (added in M5)

    if (_pMainWin) {
        _pMainWin->update_window_save_needed();
    }
}

void DeleteNodeCommand::undo()
{
    if (_snap.entries.empty()) return;

    // 1. Restore subtree (fires onNodeAdded for each entry, observer adds to GTK tree)
    _model->restoreSubtree(_snap);

    // 2. Restore bookmarks
    if (_pMainWin) {
        auto& ts = _pMainWin->get_tree_store();
        bool anyBookmark = false;
        for (gint64 bId : _bookmarkedIds) {
            if (ts.bookmarks_add(bId)) anyBookmark = true;
        }
        if (anyBookmark) {
            _pMainWin->menu_set_bookmark_menu_items();
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::book);
        }

        // 3. Restore navigation history
        _pMainWin->_visitedNodes  = std::deque<gint64>(_visitedSnapshot.begin(), _visitedSnapshot.end());
        _pMainWin->_visitedNodesIdx = _visitedIdx;

        // 4. Select the restored root node
        gint64 topId = _snap.entries.front().nodeId;
        CtTreeIter restoredIter = ts.get_node_from_node_id(topId);
        if (restoredIter) {
            _pMainWin->get_tree_view().set_cursor_safe(restoredIter);
            _pMainWin->get_text_view().mm().grab_focus();
        }
        _pMainWin->update_window_save_needed();
    }

    // 5. Reverse promotions in reverse order
    for (int i = static_cast<int>(_promotions.size()) - 1; i >= 0; --i) {
        const auto& promo = _promotions[i];
        if (promo.newMasterId == 0) continue;
        auto newMaster = _model->getNodeById(promo.newMasterId);
        if (newMaster) {
            newMaster->applyProps(promo.newMasterPriorProps);
            newMaster->setContent(promo.newMasterPriorContent);
            newMaster->setSharedMasterId(promo.oldMasterId);
            _model->notifyNodePropertiesChanged(promo.newMasterId, promo.oldMasterProps, promo.newMasterPriorProps);
            _model->notifyNodeChanged(promo.newMasterId);
        }
        for (const auto& [memberId, origMaster] : promo.rePointed) {
            auto member = _model->getNodeById(memberId);
            if (member) member->setSharedMasterId(origMaster);
        }
    }
}

std::string DeleteNodeCommand::getDescription() const
{
    return "[" + std::to_string(_snap.entries.empty() ? -1 : _snap.entries.front().nodeId) + "] Delete node";
}

// ─── MoveNodeCommand ──────────────────────────────────────────────────────────

MoveNodeCommand::MoveNodeCommand(CtDocumentModel* model,
                                 gint64 nodeId,
                                 gint64 oldParentId,
                                 int oldPosition,
                                 gint64 newParentId,
                                 int newPosition)
    : _model(model), _nodeId(nodeId),
      _oldParentId(oldParentId), _oldPosition(oldPosition),
      _newParentId(newParentId), _newPosition(newPosition)
{}

void MoveNodeCommand::execute()
{
    if (_model->moveNode(_nodeId, _newParentId, _newPosition))
        _model->notifyNodeMoved(_nodeId, _newParentId, _newPosition);
}

void MoveNodeCommand::undo()
{
    if (_model->moveNode(_nodeId, _oldParentId, _oldPosition))
        _model->notifyNodeMoved(_nodeId, _oldParentId, _oldPosition);
}

std::string MoveNodeCommand::getDescription() const
{
    return "[" + std::to_string(_nodeId) + "] Move node";
}
