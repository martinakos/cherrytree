/*
 * tests_document_model.cpp
 *
 * GTK-free unit tests for CtDocumentModel new mutation methods
 * (Milestone 1 of node-operation undo/redo).
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
#include "ct_node_commands.h"
#include "ct_command.h"
#include "gtest/gtest.h"
#include <vector>

// ─── Test observer that records which callbacks were fired ────────────────────

struct ObserverLog : public CtDocumentObserver {
    struct Event { std::string type; gint64 id{0}; gint64 extra{0}; int pos{-1}; };
    std::vector<Event> events;

    void onNodeChanged(gint64 nodeId) override {
        events.push_back({"changed", nodeId, 0, -1});
    }
    void onNodeAdded(gint64 nodeId, gint64 parentId) override {
        events.push_back({"added", nodeId, parentId, -1});
    }
    void onNodeDeleted(gint64 nodeId) override {
        events.push_back({"deleted", nodeId, 0, -1});
    }
    void onNodeMoved(gint64 nodeId, gint64 newParentId, int newPos) override {
        events.push_back({"moved", nodeId, newParentId, newPos});
    }
    void onTreeStructureChanged() override {
        events.push_back({"structure", 0, 0, -1});
    }
    void onNodePropertiesChanged(gint64 nodeId, const CtNodeProps&, const CtNodeProps&) override {
        events.push_back({"props", nodeId, 0, -1});
    }

    bool hasType(const std::string& t) const {
        for (const auto& e : events) if (e.type == t) return true;
        return false;
    }
    int countType(const std::string& t) const {
        int n = 0;
        for (const auto& e : events) if (e.type == t) ++n;
        return n;
    }
    void clear() { events.clear(); }
};

// ─── Helper: build a 3-level tree (root → A → B → C) ─────────────────────────

static void build3LevelTree(CtDocumentModel& model)
{
    // root (id 0) is the virtual root created by CtDocumentModel
    auto a = model.createNode(1);
    a->setName("A");
    model.addNode(a, /*parentId=*/0, -1);

    auto b = model.createNode(2);
    b->setName("B");
    model.addNode(b, /*parentId=*/1, -1);

    auto c = model.createNode(3);
    c->setName("C");
    model.addNode(c, /*parentId=*/2, -1);
}

// ─── Test 1: removeNodeWithChildren fires bottom-up delete notifications ──────

TEST(CtDocumentModelTest, RemoveNodeWithChildren_Recursive)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    build3LevelTree(model);  // root → 1 → 2 → 3
    log.clear();

    bool ok = model.removeNodeWithChildren(/*nodeId=*/1);
    EXPECT_TRUE(ok);

    // All three nodes must be gone
    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));

    // Notifications: C deleted first, then B, then A (post-order)
    ASSERT_EQ(3, log.countType("deleted"));
    EXPECT_EQ(3, log.events[0].id);  // C
    EXPECT_EQ(2, log.events[1].id);  // B
    EXPECT_EQ(1, log.events[2].id);  // A
}

// ─── Test 2: snapshotSubtree + restoreSubtree preserves node ids ──────────────

TEST(CtDocumentModelTest, SnapshotAndRestoreSubtree_PreservesIds)
{
    CtDocumentModel model;
    build3LevelTree(model);  // root → 1 → 2 → 3

    SubtreeSnapshot snap = model.snapshotSubtree(1);

    // Remove the subtree
    model.removeNodeWithChildren(1);
    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));

    // Restore
    ObserverLog log;
    model.addObserver(&log);
    bool ok = model.restoreSubtree(snap);
    EXPECT_TRUE(ok);

    // All nodes back with original ids
    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_NE(nullptr, model.getNodeById(2));
    EXPECT_NE(nullptr, model.getNodeById(3));

    // Names preserved
    EXPECT_EQ(Glib::ustring("A"), model.getNodeById(1)->getName());
    EXPECT_EQ(Glib::ustring("B"), model.getNodeById(2)->getName());
    EXPECT_EQ(Glib::ustring("C"), model.getNodeById(3)->getName());

    // Added notifications fired (one per node)
    EXPECT_EQ(3, log.countType("added"));
}

// ─── Test 3: restoreSubtree preserves original tree positions ─────────────────

TEST(CtDocumentModelTest, RestoreSubtree_PreservesPosition)
{
    CtDocumentModel model;

    // root → [first(4), target(1 → 2 → 3), last(5)]
    auto first = model.createNode(4);
    first->setName("first");
    model.addNode(first, 0, -1);

    build3LevelTree(model);  // adds 1,2,3 after 4

    auto last = model.createNode(5);
    last->setName("last");
    model.addNode(last, 0, -1);

    // Snapshot the subtree rooted at 1 (position 1 among root's children)
    SubtreeSnapshot snap = model.snapshotSubtree(1);
    ASSERT_FALSE(snap.entries.empty());
    EXPECT_EQ(1, snap.entries.front().position);

    model.removeNodeWithChildren(1);

    // Restore — subtree should land back at position 1
    ASSERT_TRUE(model.restoreSubtree(snap));

    auto root = model.getNodeById(0);
    ASSERT_NE(nullptr, root);
    const auto& children = root->getChildren();
    ASSERT_EQ(3u, children.size());
    EXPECT_EQ(4, children[0]->getNodeId());
    EXPECT_EQ(1, children[1]->getNodeId());
    EXPECT_EQ(5, children[2]->getNodeId());
}

// ─── Test 4: updateNodeProperties notifies observer with onNodePropertiesChanged

TEST(CtDocumentModelTest, UpdateNodeProperties_NotifiesObserver)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    auto node = model.createNode(10);
    model.addNode(node, 0, -1);
    log.clear();

    CtNodeProps p;
    p.name = "renamed";
    p.syntax = "custom-colors";
    bool ok = model.updateNodeProperties(10, p);
    EXPECT_TRUE(ok);
    EXPECT_EQ(Glib::ustring("renamed"), model.getNodeById(10)->getName());

    // Exactly one props-changed notification, no spurious content-changed
    EXPECT_EQ(1, log.countType("props"));
    EXPECT_EQ(0, log.countType("changed"));
}

// ─── Test 5: moveNode at position -1 (append) vs explicit index ───────────────

TEST(CtDocumentModelTest, MoveNode_Sequencing)
{
    CtDocumentModel model;

    // root → [10, 11, 12]
    for (gint64 id : {10, 11, 12}) {
        auto n = model.createNode(id);
        model.addNode(n, 0, -1);
    }

    auto root = model.getNodeById(0);
    ASSERT_NE(nullptr, root);
    ASSERT_EQ(3u, root->getChildren().size());

    // Move 12 to position 0 (front)
    EXPECT_TRUE(model.moveNode(12, 0, 0));
    EXPECT_EQ(12, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(10, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(11, root->getChildren()[2]->getNodeId());

    // Move 10 to position -1 (append)
    EXPECT_TRUE(model.moveNode(10, 0, -1));
    EXPECT_EQ(12, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(11, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(10, root->getChildren()[2]->getNodeId());
}

// ─── Helpers shared by command tests ─────────────────────────────────────────

static CtNodeProps makeProps(const std::string& name, const std::string& syntax = "custom-colors")
{
    CtNodeProps p;
    p.name = name;
    p.syntax = syntax;
    return p;
}

// ─── EditNodePropertiesCommand ────────────────────────────────────────────────

TEST(NodeCommandsTest, EditNodePropsCmd_ExecuteAppliesNewProps)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    auto node = model.createNode(1);
    node->setName("old");
    node->setBold(false);
    model.addNode(node, 0, -1);
    log.clear();

    CtNodeProps oldProps = node->captureProps();
    CtNodeProps newProps = oldProps;
    newProps.name = "new";
    newProps.isBold = true;

    EditNodePropertiesCommand cmd(&model, 1, oldProps, newProps);
    cmd.execute();

    EXPECT_EQ(Glib::ustring("new"), model.getNodeById(1)->getName());
    EXPECT_TRUE(model.getNodeById(1)->isBold());
    EXPECT_EQ(1, log.countType("props"));
    EXPECT_EQ(0, log.countType("changed"));
}

TEST(NodeCommandsTest, EditNodePropsCmd_UndoRestoresOldProps)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    auto node = model.createNode(1);
    node->setName("old");
    model.addNode(node, 0, -1);
    log.clear();

    CtNodeProps oldProps = node->captureProps();
    CtNodeProps newProps = oldProps;
    newProps.name = "new";

    EditNodePropertiesCommand cmd(&model, 1, oldProps, newProps);
    cmd.execute();
    log.clear();
    cmd.undo();

    EXPECT_EQ(Glib::ustring("old"), model.getNodeById(1)->getName());
    EXPECT_EQ(1, log.countType("props"));
}

TEST(NodeCommandsTest, EditNodePropsCmd_RedoReappliesNewProps)
{
    CtDocumentModel model;
    auto node = model.createNode(1);
    node->setName("orig");
    model.addNode(node, 0, -1);

    CtNodeProps old = node->captureProps();
    CtNodeProps updated = old;
    updated.name = "changed";

    EditNodePropertiesCommand cmd(&model, 1, old, updated);
    cmd.execute();
    cmd.undo();
    EXPECT_EQ(Glib::ustring("orig"), model.getNodeById(1)->getName());
    cmd.redo();
    EXPECT_EQ(Glib::ustring("changed"), model.getNodeById(1)->getName());
}

// ─── AddNodeCommand ───────────────────────────────────────────────────────────

TEST(NodeCommandsTest, AddNodeCmd_ExecuteCreatesNodeAtCorrectPosition)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    // Pre-populate root with nodes 1 and 3
    model.addNode(model.createNode(1), 0, -1);
    model.addNode(model.createNode(3), 0, -1);
    log.clear();

    // Insert node 2 at position 1 (between 1 and 3)
    AddNodeCommand cmd(&model, 2, 0, 1, makeProps("middle"), CtNodeContent{});
    cmd.execute();

    auto root = model.getNodeById(0);
    ASSERT_EQ(3u, root->getChildren().size());
    EXPECT_EQ(1, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(2, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(3, root->getChildren()[2]->getNodeId());
    EXPECT_EQ(Glib::ustring("middle"), model.getNodeById(2)->getName());
    EXPECT_EQ(1, log.countType("added"));
    EXPECT_EQ(2, log.events.back().id);
}

TEST(NodeCommandsTest, AddNodeCmd_UndoRemovesNode)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    AddNodeCommand cmd(&model, 10, 0, -1, makeProps("to-remove"), CtNodeContent{});
    cmd.execute();
    log.clear();

    cmd.undo();

    EXPECT_EQ(nullptr, model.getNodeById(10));
    EXPECT_EQ(0u, model.getNodeById(0)->getChildren().size());
    EXPECT_EQ(1, log.countType("deleted"));
}

TEST(NodeCommandsTest, AddNodeCmd_RedoReAddsNode)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    AddNodeCommand cmd(&model, 10, 0, -1, makeProps("redoable"), CtNodeContent{});
    cmd.execute();
    cmd.undo();
    ASSERT_EQ(nullptr, model.getNodeById(10));
    log.clear();

    cmd.redo();

    ASSERT_NE(nullptr, model.getNodeById(10));
    EXPECT_EQ(Glib::ustring("redoable"), model.getNodeById(10)->getName());
    EXPECT_EQ(1, log.countType("added"));
}

TEST(NodeCommandsTest, AddNodeCmd_PreservesNodeProps)
{
    CtDocumentModel model;

    CtNodeProps props = makeProps("rich", "custom-colors");
    props.isBold = true;
    props.tags = "tag1 tag2";

    AddNodeCommand cmd(&model, 5, 0, -1, props, CtNodeContent{});
    cmd.execute();

    auto n = model.getNodeById(5);
    ASSERT_NE(nullptr, n);
    EXPECT_TRUE(n->isBold());
    EXPECT_EQ(Glib::ustring("tag1 tag2"), n->getTags());
    EXPECT_EQ("custom-colors", n->getSyntax());
}

// ─── Per-node line wrap property ──────────────────────────────────────────────

TEST(NodeLineWrapTest, DefaultsToNotWrapping)
{
    CtNodeProps p;
    EXPECT_FALSE(p.lineWrap);

    CtDocumentModel model;
    auto node = model.createNode(1);
    EXPECT_FALSE(node->isLineWrap());
}

TEST(NodeLineWrapTest, CapturePropsRoundTrip)
{
    CtDocumentModel model;
    auto node = model.createNode(1);
    node->setLineWrap(true);

    CtNodeProps p = node->captureProps();
    EXPECT_TRUE(p.lineWrap);

    auto other = model.createNode(2);
    other->applyProps(p);
    EXPECT_TRUE(other->isLineWrap());
}

TEST(NodeLineWrapTest, EditNodePropsCmd_TogglesLineWrap)
{
    CtDocumentModel model;
    auto node = model.createNode(1);
    model.addNode(node, 0, -1);

    CtNodeProps oldProps = node->captureProps();
    ASSERT_FALSE(oldProps.lineWrap);
    CtNodeProps newProps = oldProps;
    newProps.lineWrap = true;

    EditNodePropertiesCommand cmd(&model, 1, oldProps, newProps);
    cmd.execute();
    EXPECT_TRUE(model.getNodeById(1)->isLineWrap());

    cmd.undo();
    EXPECT_FALSE(model.getNodeById(1)->isLineWrap());

    cmd.redo();
    EXPECT_TRUE(model.getNodeById(1)->isLineWrap());
}

TEST(NodeLineWrapTest, AddNodeCmd_PreservesLineWrap)
{
    CtDocumentModel model;

    CtNodeProps props = makeProps("wrapped");
    props.lineWrap = true;

    AddNodeCommand cmd(&model, 7, 0, -1, props, CtNodeContent{});
    cmd.execute();

    auto n = model.getNodeById(7);
    ASSERT_NE(nullptr, n);
    EXPECT_TRUE(n->isLineWrap());
}

// ─── DeleteNodeCommand ────────────────────────────────────────────────────────

TEST(NodeCommandsTest, DeleteNodeCmd_ExecuteRemovesSubtree)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);
    build3LevelTree(model);  // root → 1(A) → 2(B) → 3(C)
    log.clear();

    SubtreeSnapshot snap = model.snapshotSubtree(1);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();

    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));
    // Bottom-up delete notifications: C, B, A
    ASSERT_EQ(3, log.countType("deleted"));
    EXPECT_EQ(3, log.events[0].id);
    EXPECT_EQ(2, log.events[1].id);
    EXPECT_EQ(1, log.events[2].id);
}

TEST(NodeCommandsTest, DeleteNodeCmd_UndoRestoresSubtreeWithOriginalIds)
{
    CtDocumentModel model;
    build3LevelTree(model);

    SubtreeSnapshot snap = model.snapshotSubtree(1);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();
    cmd.undo();

    ASSERT_NE(nullptr, model.getNodeById(1));
    ASSERT_NE(nullptr, model.getNodeById(2));
    ASSERT_NE(nullptr, model.getNodeById(3));
    EXPECT_EQ(Glib::ustring("A"), model.getNodeById(1)->getName());
    EXPECT_EQ(Glib::ustring("B"), model.getNodeById(2)->getName());
    EXPECT_EQ(Glib::ustring("C"), model.getNodeById(3)->getName());

    // Tree structure: root → 1 → 2 → 3
    EXPECT_EQ(1, model.getNodeById(0)->getChildren().size());
    EXPECT_EQ(1, model.getNodeById(1)->getChildren().size());
    EXPECT_EQ(1, model.getNodeById(2)->getChildren().size());
}

TEST(NodeCommandsTest, DeleteNodeCmd_UndoRestoresPosition)
{
    CtDocumentModel model;
    // root → [first(4), target(1 → 2 → 3), last(5)]
    model.addNode(model.createNode(4), 0, -1);
    build3LevelTree(model);
    model.addNode(model.createNode(5), 0, -1);

    SubtreeSnapshot snap = model.snapshotSubtree(1);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();
    cmd.undo();

    auto root = model.getNodeById(0);
    ASSERT_EQ(3u, root->getChildren().size());
    EXPECT_EQ(4, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(1, root->getChildren()[1]->getNodeId());  // restored at original position
    EXPECT_EQ(5, root->getChildren()[2]->getNodeId());
}

TEST(NodeCommandsTest, DeleteNodeCmd_RedoRemovesSubtreeAgain)
{
    CtDocumentModel model;
    build3LevelTree(model);

    SubtreeSnapshot snap = model.snapshotSubtree(1);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();
    cmd.undo();
    ASSERT_NE(nullptr, model.getNodeById(1));

    cmd.redo();

    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));
}

// ─── MoveNodeCommand ──────────────────────────────────────────────────────────

TEST(NodeCommandsTest, MoveNodeCmd_ExecuteReordersWithinParent)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);
    for (gint64 id : {1, 2, 3}) model.addNode(model.createNode(id), 0, -1);
    log.clear();

    // Move node 3 from position 2 to position 0
    MoveNodeCommand cmd(&model, 3, 0, 2, 0, 0);
    cmd.execute();

    auto root = model.getNodeById(0);
    EXPECT_EQ(3, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(1, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(2, root->getChildren()[2]->getNodeId());
    EXPECT_EQ(1, log.countType("moved"));
}

TEST(NodeCommandsTest, MoveNodeCmd_UndoRestoresOriginalOrder)
{
    CtDocumentModel model;
    for (gint64 id : {1, 2, 3}) model.addNode(model.createNode(id), 0, -1);

    MoveNodeCommand cmd(&model, 3, 0, 2, 0, 0);
    cmd.execute();
    cmd.undo();

    auto root = model.getNodeById(0);
    EXPECT_EQ(1, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(2, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(3, root->getChildren()[2]->getNodeId());
}

TEST(NodeCommandsTest, MoveNodeCmd_ExecuteReparentsNode)
{
    CtDocumentModel model;
    // root → [parent(1), orphan(2)]
    model.addNode(model.createNode(1), 0, -1);
    model.addNode(model.createNode(2), 0, -1);

    // Move node 2 under node 1
    MoveNodeCommand cmd(&model, 2, 0, 1, 1, 0);
    cmd.execute();

    ASSERT_EQ(1u, model.getNodeById(1)->getChildren().size());
    EXPECT_EQ(2, model.getNodeById(1)->getChildren()[0]->getNodeId());
    EXPECT_EQ(1u, model.getNodeById(0)->getChildren().size());
}

TEST(NodeCommandsTest, MoveNodeCmd_UndoReversesReparent)
{
    CtDocumentModel model;
    model.addNode(model.createNode(1), 0, -1);
    model.addNode(model.createNode(2), 0, -1);

    MoveNodeCommand cmd(&model, 2, 0, 1, 1, 0);
    cmd.execute();
    cmd.undo();

    EXPECT_EQ(0u, model.getNodeById(1)->getChildren().size());
    EXPECT_EQ(2u, model.getNodeById(0)->getChildren().size());
    EXPECT_EQ(2, model.getNodeById(0)->getChildren()[1]->getNodeId());
}

TEST(NodeCommandsTest, MoveNodeCmd_RedoReappliesMove)
{
    CtDocumentModel model;
    for (gint64 id : {1, 2, 3}) model.addNode(model.createNode(id), 0, -1);

    MoveNodeCommand cmd(&model, 1, 0, 0, 0, 2);
    cmd.execute();
    cmd.undo();
    cmd.redo();

    auto root = model.getNodeById(0);
    // After redo, node 1 should be back at position 2 (tail)
    EXPECT_EQ(2, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(3, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(1, root->getChildren()[2]->getNodeId());
}

// ─── CompoundCommand ──────────────────────────────────────────────────────────

TEST(NodeCommandsTest, CompoundCommand_ExecutesAllSubcommands)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    auto compound = std::make_unique<CompoundCommand>("batch add");
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 1, 0, -1, makeProps("A"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 2, 0, -1, makeProps("B"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 3, 0, -1, makeProps("C"), CtNodeContent{}));
    compound->execute();

    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_NE(nullptr, model.getNodeById(2));
    EXPECT_NE(nullptr, model.getNodeById(3));
    EXPECT_EQ(3, log.countType("added"));
}

TEST(NodeCommandsTest, CompoundCommand_UndoReversesAllInReverseOrder)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    auto compound = std::make_unique<CompoundCommand>("batch add");
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 1, 0, -1, makeProps("A"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 2, 1, -1, makeProps("B"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 3, 2, -1, makeProps("C"), CtNodeContent{}));
    compound->execute();
    log.clear();

    compound->undo();

    // Undo fires in reverse order: 3 deleted first, then 2, then 1
    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));
    ASSERT_EQ(3, log.countType("deleted"));
    EXPECT_EQ(3, log.events[0].id);
    EXPECT_EQ(2, log.events[1].id);
    EXPECT_EQ(1, log.events[2].id);
}

TEST(NodeCommandsTest, CompoundCommand_RedoReappliesAll)
{
    CtDocumentModel model;

    auto compound = std::make_unique<CompoundCommand>("batch");
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 1, 0, -1, makeProps("A"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 2, 0, -1, makeProps("B"), CtNodeContent{}));
    compound->execute();
    compound->undo();
    compound->redo();

    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_NE(nullptr, model.getNodeById(2));
}

// ─── CtCommandManager ─────────────────────────────────────────────────────────

TEST(NodeCommandsTest, CommandManager_InitialStateEmpty)
{
    CtCommandManager mgr;
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
}

TEST(NodeCommandsTest, CommandManager_UndoRedoStackTransitions)
{
    CtDocumentModel model;
    CtCommandManager mgr;

    mgr.executeCommand(std::make_unique<AddNodeCommand>(&model, 1, 0, -1, makeProps("N"), CtNodeContent{}));
    EXPECT_TRUE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
    ASSERT_NE(nullptr, model.getNodeById(1));

    mgr.undo();
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_TRUE(mgr.canRedo());
    EXPECT_EQ(nullptr, model.getNodeById(1));

    mgr.redo();
    EXPECT_TRUE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
    ASSERT_NE(nullptr, model.getNodeById(1));
}

TEST(NodeCommandsTest, CommandManager_NewCommandClearsRedoStack)
{
    CtDocumentModel model;
    CtCommandManager mgr;

    mgr.executeCommand(std::make_unique<AddNodeCommand>(&model, 1, 0, -1, makeProps("N1"), CtNodeContent{}));
    mgr.executeCommand(std::make_unique<AddNodeCommand>(&model, 2, 0, -1, makeProps("N2"), CtNodeContent{}));
    mgr.undo();
    EXPECT_TRUE(mgr.canRedo());
    EXPECT_EQ(nullptr, model.getNodeById(2));

    mgr.executeCommand(std::make_unique<AddNodeCommand>(&model, 3, 0, -1, makeProps("N3"), CtNodeContent{}));

    EXPECT_FALSE(mgr.canRedo());
    EXPECT_EQ(nullptr, model.getNodeById(2));  // undone and not redone
    ASSERT_NE(nullptr, model.getNodeById(3));  // new command executed
}

TEST(NodeCommandsTest, CommandManager_MultipleUndoRedo)
{
    CtDocumentModel model;
    CtCommandManager mgr;

    for (gint64 id : {1, 2, 3}) {
        mgr.executeCommand(std::make_unique<AddNodeCommand>(&model, id, 0, -1, makeProps("N"), CtNodeContent{}));
    }

    mgr.undo(3);
    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));
    EXPECT_FALSE(mgr.canUndo());

    mgr.redo(3);
    ASSERT_NE(nullptr, model.getNodeById(1));
    ASSERT_NE(nullptr, model.getNodeById(2));
    ASSERT_NE(nullptr, model.getNodeById(3));
    EXPECT_FALSE(mgr.canRedo());
}

TEST(NodeCommandsTest, CommandManager_CompoundUndoRedoAsUnit)
{
    CtDocumentModel model;
    CtCommandManager mgr;

    auto compound = std::make_unique<CompoundCommand>("add 3 nodes");
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 1, 0, -1, makeProps("A"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 2, 0, -1, makeProps("B"), CtNodeContent{}));
    compound->addCommand(std::make_unique<AddNodeCommand>(&model, 3, 0, -1, makeProps("C"), CtNodeContent{}));
    mgr.executeCommand(std::move(compound));

    EXPECT_EQ(1u, mgr.getUndoStackDescriptions().size());  // one entry despite 3 nodes
    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_NE(nullptr, model.getNodeById(2));
    EXPECT_NE(nullptr, model.getNodeById(3));

    mgr.undo();  // undoes the entire compound in one step
    EXPECT_EQ(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_EQ(nullptr, model.getNodeById(3));
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_TRUE(mgr.canRedo());
}

// ─── DeleteNodeCommand: sibling preservation (regression for save-corruption bug)

TEST(NodeCommandsTest, DeleteNodeCmd_SiblingsSurviveDeletion)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    // root → [A(1), B(2), C(3)]
    for (gint64 id : {1, 2, 3}) {
        auto n = model.createNode(id);
        n->setName(id == 1 ? "A" : id == 2 ? "B" : "C");
        model.addNode(n, 0, -1);
    }
    log.clear();

    SubtreeSnapshot snap = model.snapshotSubtree(2);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();

    // Only node 2 should be deleted
    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_NE(nullptr, model.getNodeById(3));

    // Observer must see exactly one delete notification, for node 2 only
    ASSERT_EQ(1, log.countType("deleted"));
    EXPECT_EQ(2, log.events[0].id);

    // Remaining siblings should be intact under root
    auto root = model.getNodeById(0);
    ASSERT_EQ(2u, root->getChildren().size());
    EXPECT_EQ(1, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(3, root->getChildren()[1]->getNodeId());
}

TEST(NodeCommandsTest, DeleteNodeCmd_UndoRedoPreservesSiblings)
{
    CtDocumentModel model;

    // root → [A(1), B(2), C(3)]
    for (gint64 id : {1, 2, 3}) {
        auto n = model.createNode(id);
        n->setName(id == 1 ? "A" : id == 2 ? "B" : "C");
        model.addNode(n, 0, -1);
    }

    SubtreeSnapshot snap = model.snapshotSubtree(2);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();
    cmd.undo();

    // All three siblings restored
    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_NE(nullptr, model.getNodeById(2));
    EXPECT_NE(nullptr, model.getNodeById(3));
    auto root = model.getNodeById(0);
    ASSERT_EQ(3u, root->getChildren().size());
    EXPECT_EQ(1, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(2, root->getChildren()[1]->getNodeId());
    EXPECT_EQ(3, root->getChildren()[2]->getNodeId());

    // Redo: only B deleted again, siblings survive
    cmd.redo();
    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_EQ(nullptr, model.getNodeById(2));
    EXPECT_NE(nullptr, model.getNodeById(3));
    ASSERT_EQ(2u, root->getChildren().size());
    EXPECT_EQ(1, root->getChildren()[0]->getNodeId());
    EXPECT_EQ(3, root->getChildren()[1]->getNodeId());
}

TEST(NodeCommandsTest, DeleteNodeCmd_OnlyTargetSubtreeDeleteNotifications)
{
    CtDocumentModel model;
    ObserverLog log;
    model.addObserver(&log);

    // root → [A(1), B(2 → D(4)), C(3)]
    for (gint64 id : {1, 2, 3}) {
        model.addNode(model.createNode(id), 0, -1);
    }
    model.addNode(model.createNode(4), 2, -1);  // D is child of B
    log.clear();

    SubtreeSnapshot snap = model.snapshotSubtree(2);
    DeleteNodeCommand cmd(&model, nullptr, snap, {}, {}, {}, 0, -1);
    cmd.execute();

    // Exactly 2 deletes: D(4) then B(2), bottom-up
    ASSERT_EQ(2, log.countType("deleted"));
    EXPECT_EQ(4, log.events[0].id);
    EXPECT_EQ(2, log.events[1].id);

    // Siblings A and C untouched
    EXPECT_NE(nullptr, model.getNodeById(1));
    EXPECT_NE(nullptr, model.getNodeById(3));
}
