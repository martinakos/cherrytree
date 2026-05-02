/*
 * ct_document_model.h
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

#include <memory>
#include <vector>
#include <set>
#include <unordered_map>
#include <glibmm/ustring.h>
#include "ct_node_content.h"

// Forward declarations
class CtDocumentObserver;

// Plain-data node properties — no GTK pointers, safe to copy for undo/redo
struct CtNodeProps {
    Glib::ustring name;
    std::string syntax{"custom-colors"};
    Glib::ustring tags;
    bool isReadOnly{false};
    bool isBold{false};
    guint32 customIconId{0};
    std::string foregroundRgb24;
    bool excludeMeFromSearch{false};
    bool excludeChildrenFromSearch{false};
    gint64 tsCreation{0};
    gint64 tsLastSave{0};
};

// Depth-first snapshot of a subtree; used by DeleteNodeCommand for undo
struct SubtreeSnapshot {
    struct Entry {
        gint64 nodeId{0};
        gint64 parentId{0};   // 0 = root
        int    position{-1};
        gint64 sharedMasterId{0};
        gint64 sequence{-1};
        CtNodeProps props;
        CtNodeContent content; // empty for shared non-masters
    };
    std::vector<Entry> entries; // depth-first order; restore in this order
};

// Represents a single node's properties and content
// This is the model representation - no GTK dependencies
class CtNodeModel {
public:
    CtNodeModel(gint64 nodeId);
    ~CtNodeModel() = default;

    // Node identification
    gint64 getNodeId() const { return _nodeId; }
    gint64 getSharedMasterId() const { return _sharedNodesMasterId; }
    void setSharedMasterId(gint64 masterId) { _sharedNodesMasterId = masterId; }

    // Node properties
    Glib::ustring getName() const { return _name; }
    void setName(const Glib::ustring& name) { _name = name; }

    Glib::ustring getTags() const { return _tags; }
    void setTags(const Glib::ustring& tags) { _tags = tags; }

    std::string getSyntax() const { return _syntax; }
    void setSyntax(const std::string& syntax) { _syntax = syntax; }

    bool isReadOnly() const { return _isReadOnly; }
    void setReadOnly(bool readOnly) { _isReadOnly = readOnly; }

    bool isBold() const { return _isBold; }
    void setBold(bool bold) { _isBold = bold; }

    guint32 getCustomIconId() const { return _customIconId; }
    void setCustomIconId(guint32 iconId) { _customIconId = iconId; }

    std::string getForegroundRgb24() const { return _foregroundRgb24; }
    void setForegroundRgb24(const std::string& color) { _foregroundRgb24 = color; }

    bool isExcludedFromSearch() const { return _excludeMeFromSearch; }
    void setExcludedFromSearch(bool excluded) { _excludeMeFromSearch = excluded; }

    bool areChildrenExcludedFromSearch() const { return _excludeChildrenFromSearch; }
    void setChildrenExcludedFromSearch(bool excluded) { _excludeChildrenFromSearch = excluded; }

    gint64 getCreationTime() const { return _tsCreation; }
    void setCreationTime(gint64 timestamp) { _tsCreation = timestamp; }

    gint64 getLastSaveTime() const { return _tsLastSave; }
    void setLastSaveTime(gint64 timestamp) { _tsLastSave = timestamp; }

    gint64 getSequence() const { return _sequence; }
    void setSequence(gint64 seq) { _sequence = seq; }

    // Node content (stored as structured model)
    const CtNodeContent& getContent() const { return _content; }
    CtNodeContent& getContent() { return _content; }
    void setContent(const CtNodeContent& content) { _content = content; }

    // Tree structure
    CtNodeModel* getParent() const { return _parent; }
    void setParent(CtNodeModel* parent) { _parent = parent; }

    const std::vector<std::shared_ptr<CtNodeModel>>& getChildren() const { return _children; }
    void clearChildren() { _children.clear(); }
    void addChild(std::shared_ptr<CtNodeModel> child);
    void insertChild(size_t index, std::shared_ptr<CtNodeModel> child);
    void removeChild(std::shared_ptr<CtNodeModel> child);
    void removeChildAt(size_t index);
    int getChildIndex(const std::shared_ptr<CtNodeModel>& child) const;

    // Bulk property capture/restore (for undo/redo)
    CtNodeProps captureProps() const;
    void applyProps(const CtNodeProps& p);

    // Helper methods
    bool isRichText() const { return _syntax == "custom-colors"; }
    bool isPlainText() const { return _syntax == "plain-text"; }
    bool isCode() const { return !isRichText() && !isPlainText(); }

private:
    // Node identification
    gint64 _nodeId;
    gint64 _sharedNodesMasterId{0};
    gint64 _sequence{-1};

    // Node properties
    Glib::ustring _name;
    std::string _syntax{"custom-colors"};  // Default to rich text
    Glib::ustring _tags;
    bool _isReadOnly{false};
    guint32 _customIconId{0};
    bool _isBold{false};
    bool _excludeMeFromSearch{false};
    bool _excludeChildrenFromSearch{false};
    std::string _foregroundRgb24;
    gint64 _tsCreation{0};
    gint64 _tsLastSave{0};

    // Structured content model
    CtNodeContent _content;

    // Tree structure
    CtNodeModel* _parent{nullptr};
    std::vector<std::shared_ptr<CtNodeModel>> _children;
};

// Observer interface for document changes
class CtDocumentObserver {
public:
    virtual ~CtDocumentObserver() = default;

    // Node content changed (text, widgets, etc.)
    virtual void onNodeChanged(gint64 nodeId) = 0;

    // Node added to tree
    virtual void onNodeAdded(gint64 nodeId, gint64 parentId) = 0;

    // Node deleted from tree
    virtual void onNodeDeleted(gint64 nodeId) = 0;

    // Node moved in tree
    virtual void onNodeMoved(gint64 nodeId, gint64 newParentId, int newPosition) = 0;

    // Tree structure changed significantly (e.g., mass operations)
    virtual void onTreeStructureChanged() = 0;

    // Node properties changed (name, syntax, tags, read-only, bold, icon, color, exclude flags)
    // Separate from onNodeChanged so property-only changes don't trigger a buffer rebuild.
    virtual void onNodePropertiesChanged(gint64 nodeId,
                                         const CtNodeProps& oldProps,
                                         const CtNodeProps& newProps) {}
};

// The document model - represents the entire document tree
// This is separate from GTK views and can be tested independently
class CtDocumentModel {
public:
    CtDocumentModel();
    ~CtDocumentModel();

    // Observer management
    void addObserver(CtDocumentObserver* observer);
    void removeObserver(CtDocumentObserver* observer);

    // Node access
    std::shared_ptr<CtNodeModel> getNodeById(gint64 nodeId) const;
    std::shared_ptr<CtNodeModel> getRootNode() const { return _rootNode; }

    // Node operations
    std::shared_ptr<CtNodeModel> createNode(gint64 nodeId);
    bool addNode(std::shared_ptr<CtNodeModel> node, gint64 parentId, int position = -1);
    bool removeNode(gint64 nodeId);
    bool removeNodeWithChildren(gint64 nodeId);   // recursive, notifies bottom-up
    bool moveNode(gint64 nodeId, gint64 newParentId, int newPosition);

    // Property and content mutations (called by node commands)
    bool updateNodeProperties(gint64 nodeId, const CtNodeProps& p);
    bool setNodeContent(gint64 nodeId, const CtNodeContent& c, bool notify = true);

    // Subtree snapshot/restore for delete-undo (identity-preserving)
    SubtreeSnapshot snapshotSubtree(gint64 nodeId) const;
    bool restoreSubtree(const SubtreeSnapshot& snap);

    // Notification methods (called by commands)
    void notifyNodeChanged(gint64 nodeId);
    void notifyNodeAdded(gint64 nodeId, gint64 parentId);
    void notifyNodeDeleted(gint64 nodeId);
    void notifyNodeMoved(gint64 nodeId, gint64 newParentId, int newPosition);
    void notifyTreeStructureChanged();
    void notifyNodePropertiesChanged(gint64 nodeId, const CtNodeProps& oldP, const CtNodeProps& newP);

    // Notification suppression for batching updates
    // Used by CompoundCommand to avoid intermediate buffer rebuilds during undo/redo
    void suppressNotifications(bool suppress);

    // Utility
    void clear();
    void reset(); // Like clear() but preserves root node
    gint64 generateUniqueNodeId();

private:
    // Root node (virtual, not displayed)
    std::shared_ptr<CtNodeModel> _rootNode;

    // Fast lookup by node ID
    std::unordered_map<gint64, std::shared_ptr<CtNodeModel>> _nodeMap;

    // Observers
    std::vector<CtDocumentObserver*> _observers;

    // For generating unique node IDs
    gint64 _nextNodeId{1};

    // Notification suppression
    int _suppressionDepth{0};  // Depth counter for nesting
    std::set<gint64> _pendingChangedNodes;
};
