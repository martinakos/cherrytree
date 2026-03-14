/*
 * ct_document_model.cc
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

#include "ct_document_model.h"
#include "ct_logging.h"
#include <algorithm>
#include <libxml++/libxml++.h>

// CtNodeModel implementation

CtNodeModel::CtNodeModel(gint64 nodeId)
    : _nodeId(nodeId)
{
}

// Get content as XML - generates from structured content on demand
Glib::ustring CtNodeModel::getContentXml() const
{
    // Generate XML from structured content
    Glib::ustring xml = _content.toXml();

    // Wrap with XML declaration and <node> root element to match format from getBufferContentAsXml()
    if (!xml.empty()) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<node>" + xml + "</node>\n";
    }

    return xml;
}

// Parse XML into structured content
void CtNodeModel::setContentXml(const Glib::ustring& xml)
{
    // Parse XML into structured model
    _content = CtNodeContent::fromXml(xml, nullptr);

    spdlog::debug("Set content for node {} from XML ({} bytes)", _nodeId, xml.size());
}

void CtNodeModel::addChild(std::shared_ptr<CtNodeModel> child)
{
    if (!child) {
        spdlog::warn("Attempted to add null child to node {}", _nodeId);
        return;
    }

    child->setParent(this);
    _children.push_back(child);
}

void CtNodeModel::insertChild(size_t index, std::shared_ptr<CtNodeModel> child)
{
    if (!child) {
        spdlog::warn("Attempted to insert null child to node {}", _nodeId);
        return;
    }

    if (index > _children.size()) {
        index = _children.size();
    }

    child->setParent(this);
    _children.insert(_children.begin() + index, child);
}

void CtNodeModel::removeChild(std::shared_ptr<CtNodeModel> child)
{
    if (!child) {
        return;
    }

    auto it = std::find(_children.begin(), _children.end(), child);
    if (it != _children.end()) {
        (*it)->setParent(nullptr);
        _children.erase(it);
    }
}

void CtNodeModel::removeChildAt(size_t index)
{
    if (index >= _children.size()) {
        spdlog::warn("Attempted to remove child at invalid index {} from node {}", index, _nodeId);
        return;
    }

    _children[index]->setParent(nullptr);
    _children.erase(_children.begin() + index);
}

int CtNodeModel::getChildIndex(const std::shared_ptr<CtNodeModel>& child) const
{
    auto it = std::find(_children.begin(), _children.end(), child);
    if (it == _children.end()) {
        return -1;
    }
    return std::distance(_children.begin(), it);
}

// CtDocumentModel implementation

CtDocumentModel::CtDocumentModel()
{
    // Create a virtual root node (ID 0, not displayed)
    _rootNode = std::make_shared<CtNodeModel>(0);
    _rootNode->setName("__ROOT__");
    _nodeMap[0] = _rootNode;
    _nextNodeId = 1;
}

CtDocumentModel::~CtDocumentModel()
{
    clear();
}

void CtDocumentModel::addObserver(CtDocumentObserver* observer)
{
    if (!observer) {
        return;
    }

    // Avoid duplicates
    auto it = std::find(_observers.begin(), _observers.end(), observer);
    if (it == _observers.end()) {
        _observers.push_back(observer);
        spdlog::debug("Observer added to document model");
    }
}

void CtDocumentModel::removeObserver(CtDocumentObserver* observer)
{
    if (!observer) {
        return;
    }

    auto it = std::find(_observers.begin(), _observers.end(), observer);
    if (it != _observers.end()) {
        _observers.erase(it);
        spdlog::debug("Observer removed from document model");
    }
}

std::shared_ptr<CtNodeModel> CtDocumentModel::getNodeById(gint64 nodeId) const
{
    auto it = _nodeMap.find(nodeId);
    if (it != _nodeMap.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<CtNodeModel> CtDocumentModel::createNode(gint64 nodeId)
{
    // If nodeId is 0, generate a unique one
    if (nodeId == 0) {
        nodeId = generateUniqueNodeId();
    }

    // Check if node already exists
    if (_nodeMap.find(nodeId) != _nodeMap.end()) {
        spdlog::error("Attempted to create node with duplicate ID {}", nodeId);
        return nullptr;
    }

    auto node = std::make_shared<CtNodeModel>(nodeId);
    _nodeMap[nodeId] = node;

    spdlog::debug("Created node with ID {}", nodeId);
    return node;
}

bool CtDocumentModel::addNode(std::shared_ptr<CtNodeModel> node, gint64 parentId, int position)
{
    if (!node) {
        spdlog::error("Attempted to add null node");
        return false;
    }

    auto parent = getNodeById(parentId);
    if (!parent) {
        spdlog::error("Attempted to add node to non-existent parent {}", parentId);
        return false;
    }

    // Add to node map if not already there
    if (_nodeMap.find(node->getNodeId()) == _nodeMap.end()) {
        _nodeMap[node->getNodeId()] = node;
    }

    // Add to parent's children
    if (position < 0 || position >= static_cast<int>(parent->getChildren().size())) {
        parent->addChild(node);
    } else {
        parent->insertChild(position, node);
    }

    spdlog::debug("Added node {} to parent {}", node->getNodeId(), parentId);
    return true;
}

bool CtDocumentModel::removeNode(gint64 nodeId)
{
    auto node = getNodeById(nodeId);
    if (!node) {
        spdlog::error("Attempted to remove non-existent node {}", nodeId);
        return false;
    }

    // Can't remove root node
    if (nodeId == 0) {
        spdlog::error("Attempted to remove root node");
        return false;
    }

    // Remove from parent
    auto parent = node->getParent();
    if (parent) {
        parent->removeChild(node);
    }

    // Remove from map
    _nodeMap.erase(nodeId);

    // Note: children are not automatically removed
    // Caller must handle recursion if needed

    spdlog::debug("Removed node {}", nodeId);
    return true;
}

bool CtDocumentModel::moveNode(gint64 nodeId, gint64 newParentId, int newPosition)
{
    auto node = getNodeById(nodeId);
    if (!node) {
        spdlog::error("Attempted to move non-existent node {}", nodeId);
        return false;
    }

    auto newParent = getNodeById(newParentId);
    if (!newParent) {
        spdlog::error("Attempted to move node to non-existent parent {}", newParentId);
        return false;
    }

    // Can't move root node
    if (nodeId == 0) {
        spdlog::error("Attempted to move root node");
        return false;
    }

    // Remove from old parent
    auto oldParent = node->getParent();
    if (oldParent) {
        oldParent->removeChild(node);
    }

    // Add to new parent
    if (newPosition < 0 || newPosition >= static_cast<int>(newParent->getChildren().size())) {
        newParent->addChild(node);
    } else {
        newParent->insertChild(newPosition, node);
    }

    spdlog::debug("Moved node {} to parent {} at position {}", nodeId, newParentId, newPosition);
    return true;
}

void CtDocumentModel::notifyNodeChanged(gint64 nodeId)
{
    if (_suppressionDepth > 0) {
        // Notifications are suppressed, add to pending set
        _pendingChangedNodes.insert(nodeId);
        spdlog::debug("Notification suppressed (depth {}): node {} changed (pending)", _suppressionDepth, nodeId);
        return;
    }

    spdlog::debug("Notifying observers: node {} changed", nodeId);
    for (auto observer : _observers) {
        observer->onNodeChanged(nodeId);
    }
}

void CtDocumentModel::notifyNodeAdded(gint64 nodeId, gint64 parentId)
{
    spdlog::debug("Notifying observers: node {} added to parent {}", nodeId, parentId);
    for (auto observer : _observers) {
        observer->onNodeAdded(nodeId, parentId);
    }
}

void CtDocumentModel::notifyNodeDeleted(gint64 nodeId)
{
    spdlog::debug("Notifying observers: node {} deleted", nodeId);
    for (auto observer : _observers) {
        observer->onNodeDeleted(nodeId);
    }
}

void CtDocumentModel::notifyNodeMoved(gint64 nodeId, gint64 newParentId, int newPosition)
{
    spdlog::debug("Notifying observers: node {} moved to parent {} at position {}", nodeId, newParentId, newPosition);
    for (auto observer : _observers) {
        observer->onNodeMoved(nodeId, newParentId, newPosition);
    }
}

void CtDocumentModel::notifyTreeStructureChanged()
{
    spdlog::debug("Notifying observers: tree structure changed");
    for (auto observer : _observers) {
        observer->onTreeStructureChanged();
    }
}

void CtDocumentModel::suppressNotifications(bool suppress)
{
    if (suppress) {
        ++_suppressionDepth;
        spdlog::debug("Notification suppression enabled (depth: {})", _suppressionDepth);
    } else {
        if (_suppressionDepth > 0) {
            --_suppressionDepth;
            spdlog::debug("Notification suppression disabled (depth: {})", _suppressionDepth);

            // If we've returned to depth 0, fire all pending notifications
            if (_suppressionDepth == 0 && !_pendingChangedNodes.empty()) {
                spdlog::debug("Firing {} pending node change notifications", _pendingChangedNodes.size());
                for (gint64 nodeId : _pendingChangedNodes) {
                    for (auto observer : _observers) {
                        observer->onNodeChanged(nodeId);
                    }
                }
                _pendingChangedNodes.clear();
            }
        } else {
            spdlog::warn("Attempt to disable notification suppression when not suppressed");
        }
    }
}

void CtDocumentModel::clear()
{
    spdlog::debug("Clearing document model");
    _nodeMap.clear();
    _rootNode.reset();
    _nextNodeId = 1;
}

void CtDocumentModel::reset()
{
    spdlog::debug("Resetting document model (preserving root)");

    // Remove all children from root
    if (_rootNode) {
        _rootNode->clearChildren();
    }

    // Clear node map except root
    _nodeMap.clear();

    // Re-add root to map
    if (!_rootNode) {
        _rootNode = std::make_shared<CtNodeModel>(0);
        _rootNode->setName("__ROOT__");
    }
    _nodeMap[0] = _rootNode;

    _nextNodeId = 1;
}

gint64 CtDocumentModel::generateUniqueNodeId()
{
    // Keep incrementing until we find an unused ID
    while (_nodeMap.find(_nextNodeId) != _nodeMap.end()) {
        _nextNodeId++;
    }
    return _nextNodeId++;
}
