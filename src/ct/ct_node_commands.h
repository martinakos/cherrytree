/*
 * ct_node_commands.h
 *
 * Node-operation commands for the undo/redo system.
 * Covers add, delete, move, and property-edit operations on tree nodes.
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
#include <memory>
#include <vector>
#include <utility>

class CtMainWin;  // for DeleteNodeCommand bookmark/nav restore

// Captures the shared-master promotion that happens when a master node is deleted
// while other group members survive.  Stored in DeleteNodeCommand for undo.
struct SharedGroupPromotion {
    gint64 oldMasterId{0};                              // the deleted master
    gint64 newMasterId{0};                              // promoted member (0 if whole group deleted)
    CtNodeProps oldMasterProps;
    CtNodeContent oldMasterContent;
    CtNodeProps newMasterPriorProps;                    // new master's props before promotion
    CtNodeContent newMasterPriorContent;                // new master's content before promotion
    std::vector<std::pair<gint64,gint64>> rePointed;   // (memberId, originalSharedMasterId)
};

// ─── EditNodePropertiesCommand ────────────────────────────────────────────────
// Undo/redo a change to a node's metadata (name, syntax, tags, read-only, etc.).
// Used by node_edit, node_toggle_read_only, node_inherit_syntax (one per child,
// batched in a CompoundCommand).
class EditNodePropertiesCommand : public CtCommand {
public:
    EditNodePropertiesCommand(CtDocumentModel* model,
                              gint64 nodeId,
                              const CtNodeProps& oldProps,
                              const CtNodeProps& newProps);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    gint64 getNodeId() const override { return _nodeId; }

private:
    CtDocumentModel* _model;
    gint64 _nodeId;
    CtNodeProps _oldProps;
    CtNodeProps _newProps;
};

// ─── AddNodeCommand ───────────────────────────────────────────────────────────
// Undo/redo adding a single node (possibly shared) to the tree.
// execute() creates and attaches the node; undo() removes it.
class AddNodeCommand : public CtCommand {
public:
    AddNodeCommand(CtDocumentModel* model,
                   gint64 nodeId,
                   gint64 parentId,
                   int position,
                   const CtNodeProps& props,
                   const CtNodeContent& initialContent,
                   gint64 sharedMasterId = 0);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    gint64 getNodeId() const override { return _nodeId; }

private:
    CtDocumentModel* _model;
    gint64 _nodeId;
    gint64 _parentId;
    int _position;
    CtNodeProps _props;
    CtNodeContent _initialContent;
    gint64 _sharedMasterId;
};

// ─── DeleteNodeCommand ────────────────────────────────────────────────────────
// Undo/redo deleting a node (and its subtree).
// execute() removes the subtree from the model (observer syncs GTK).
// undo() restores the snapshot with original node ids intact.
class DeleteNodeCommand : public CtCommand {
public:
    DeleteNodeCommand(CtDocumentModel* model,
                      CtMainWin* pMainWin,
                      const SubtreeSnapshot& snap,
                      std::vector<SharedGroupPromotion> promotions,
                      std::vector<gint64> bookmarkedIds,
                      std::vector<gint64> visitedSnapshot,
                      size_t visitedIdx,
                      gint64 nextSelectedId);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    gint64 getNodeId() const override {
        return _snap.entries.empty() ? -1 : _snap.entries.front().nodeId;
    }

private:
    CtDocumentModel* _model;
    CtMainWin* _pMainWin;
    SubtreeSnapshot _snap;
    std::vector<SharedGroupPromotion> _promotions;
    std::vector<gint64> _bookmarkedIds;   // which deleted nodes were bookmarked
    std::vector<gint64> _visitedSnapshot; // nav history at time of deletion
    size_t _visitedIdx;
    gint64 _nextSelectedId; // node to select after deletion (may be -1)
};

// ─── MoveNodeCommand ──────────────────────────────────────────────────────────
// Undo/redo moving a node to a new parent / position.
// execute() and undo() are symmetric moveNode() calls.
class MoveNodeCommand : public CtCommand {
public:
    MoveNodeCommand(CtDocumentModel* model,
                    gint64 nodeId,
                    gint64 oldParentId,
                    int oldPosition,
                    gint64 newParentId,
                    int newPosition);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;
    gint64 getNodeId() const override { return _nodeId; }

private:
    CtDocumentModel* _model;
    gint64 _nodeId;
    gint64 _oldParentId;
    int _oldPosition;
    gint64 _newParentId;
    int _newPosition;
};
