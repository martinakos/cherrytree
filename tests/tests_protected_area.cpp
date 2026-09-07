/*
 * tests_protected_area.cpp
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

#include "ct_app.h"
#include "ct_command_bridge.h"
#include "ct_misc_utils.h"
#include "ct_protected_area.h"
#include "ct_storage_control.h"
#include "ct_storage_sqlite.h"
#include "ct_storage_xml.h"
#include "ct_command.h"
#include "ct_menu.h"
#include "ct_const.h"
#include "tests_common.h"

#include "gtest/gtest.h"

#include <sqlite3.h>
#include <fstream>
#include <map>
#include <algorithm>
#include <iterator>

// ── protecting a node is persisted, and marked in the level bitfield ───────

class ProtectedFlagRoundTripApp : public CtApp
{
public:
    ProtectedFlagRoundTripApp() : CtApp{"_test_protflag_rt"} { _no_gui = true; }
private:
    void on_activate() override;
};

void ProtectedFlagRoundTripApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin1 = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin1->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    fs::path tmpDir = pWin1->get_ct_tmp()->getHiddenDirPath("UT_PROTFLAG");
    fs::path tmpDoc = tmpDir / "protflag_test.ctb";
    pWin1->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

    CtTreeIter treeIter = pWin1->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(treeIter);
    const gint64 nodeId = treeIter.get_node_id();
    EXPECT_FALSE(pWin1->get_protected_areas().is_protected_root(nodeId)) << "should default to off";

    Glib::ustring error;
    ASSERT_TRUE(pWin1->get_protected_areas().protect(treeIter, "swordfish", error)) << error.raw();
    ASSERT_TRUE(pWin1->file_save(false/*need_vacuum*/));

    pWin1->force_exit() = true;
    remove_window(*pWin1);

    // Raw file check: the storage marks bit 3 of node.level so that a leftover
    // protected_area row can be recognised as stale, and it leaves the other
    // flags packed in that column alone
    {
        sqlite3* pDb{nullptr};
        ASSERT_EQ(SQLITE_OK, sqlite3_open_v2(tmpDoc.c_str(), &pDb, SQLITE_OPEN_READONLY, nullptr));
        sqlite3_stmt* pStmt{nullptr};
        ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, "SELECT level FROM node WHERE name='b'", -1, &pStmt, nullptr));
        ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
        const gint64 level = sqlite3_column_int64(pStmt, 0);
        EXPECT_EQ(0x08, level & 0x08) << "protected root bit missing from node.level";
        EXPECT_EQ(0, level & 0x07) << "the other level bits were disturbed";
        sqlite3_finalize(pStmt);
        sqlite3_close(pDb);
    }

    // reopening finds the area again, from the protected_area table
    CtMainWin* pWin2 = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin2->file_open(tmpDoc, ""/*node*/, ""/*anchor*/));
    EXPECT_TRUE(pWin2->get_protected_areas().is_protected_root(nodeId)) << "protection not persisted";
    EXPECT_TRUE(pWin2->get_protected_areas().is_locked(nodeId));

    // and it is per node: the neighbours are untouched
    CtTreeIter treeIterD = pWin2->get_tree_store().get_node_from_node_name("d");
    ASSERT_TRUE(treeIterD);
    EXPECT_FALSE(pWin2->get_protected_areas().is_protected_root(treeIterD.get_node_id()));

    pWin2->force_exit() = true;
    remove_window(*pWin2);
}

TEST(ProtectedAreaFlag, SqliteSaveAndReload)
{
    ProtectedFlagRoundTripApp app;
    app.run(0, nullptr);
}

// ── the protected_area table stores and returns an encrypted blob ───────────

class ProtectedAreaTableApp : public CtApp
{
public:
    ProtectedAreaTableApp() : CtApp{"_test_protarea_tbl"} { _no_gui = true; }
private:
    void on_activate() override;
};

void ProtectedAreaTableApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    const std::string secretXml = "<node name=\"diary\"><rich_text>the treasure is buried under the oak</rich_text></node>";

    CtMainWin* pWin1 = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin1->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));

    fs::path tmpDir = pWin1->get_ct_tmp()->getHiddenDirPath("UT_PROTAREA");
    fs::path tmpDoc = tmpDir / "protarea_test.ctb";
    pWin1->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

    CtTreeIter treeIter = pWin1->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(treeIter);
    const gint64 protectedNodeId = treeIter.get_node_id();

    // seal the payload and hand it to the storage backend
    // the row must belong to a genuinely protected node, otherwise the storage
    // treats it as an orphan and filters it out on read
    Glib::ustring protectError;
    ASSERT_TRUE(pWin1->get_protected_areas().protect(treeIter, "swordfish", protectError)) << protectError.raw();
    ASSERT_TRUE(pWin1->file_save(false/*need_vacuum*/));

    CtProtectedAreaRecord record;
    record.nodeId = protectedNodeId;
    record.tsLastSave = 1234567890;
    ASSERT_TRUE(CtCrypto::seal(secretXml, "swordfish", record.envelope, 1000u/*fast for tests*/));

    auto* pStorage = dynamic_cast<CtStorageSqlite*>(pWin1->get_ct_storage()->get_storage_entity());
    ASSERT_TRUE(pStorage) << "expected the sqlite backend";
    pStorage->write_protected_area(record);

    // read it straight back
    std::vector<CtProtectedAreaRecord> readBack = pStorage->read_protected_areas();
    ASSERT_EQ(1u, readBack.size());
    EXPECT_EQ(protectedNodeId, readBack[0].nodeId);
    EXPECT_EQ(1234567890, readBack[0].tsLastSave);
    EXPECT_EQ(record.envelope.kdfSalt, readBack[0].envelope.kdfSalt);
    EXPECT_EQ(record.envelope.iv, readBack[0].envelope.iv);
    EXPECT_EQ(record.envelope.mac, readBack[0].envelope.mac);
    EXPECT_EQ(record.envelope.payload, readBack[0].envelope.payload);
    EXPECT_EQ(record.envelope.kdfIterations, readBack[0].envelope.kdfIterations);

    // the blob is what it claims to be
    std::string recovered;
    ASSERT_TRUE(CtCrypto::unseal(readBack[0].envelope, "swordfish", recovered));
    EXPECT_EQ(secretXml, recovered);
    EXPECT_FALSE(CtCrypto::unseal(readBack[0].envelope, "wrong", recovered));

    // replacing the row for the same node keeps exactly one record
    CtProtectedAreaRecord second = record;
    ASSERT_TRUE(CtCrypto::seal("<node/>", "swordfish", second.envelope, 1000u));
    pStorage->write_protected_area(second);
    EXPECT_EQ(1u, pStorage->read_protected_areas().size());

    // the plaintext must not be findable anywhere in the raw file
    pStorage->write_protected_area(record);
    {
        std::ifstream rawFile{tmpDoc.string(), std::ios::binary};
        ASSERT_TRUE(rawFile.is_open());
        const std::string rawBytes{std::istreambuf_iterator<char>{rawFile}, std::istreambuf_iterator<char>{}};
        EXPECT_EQ(std::string::npos, rawBytes.find("the treasure is buried under the oak"))
            << "protected content found in clear in the document file";
        EXPECT_NE(std::string::npos, rawBytes.find(record.envelope.payload))
            << "the encrypted payload was not written to the file";
    }

    pStorage->remove_protected_area(protectedNodeId);
    EXPECT_TRUE(pStorage->read_protected_areas().empty());

    pWin1->force_exit() = true;
    remove_window(*pWin1);
}

TEST(ProtectedAreaTable, WriteReadDelete)
{
    ProtectedAreaTableApp app;
    app.run(0, nullptr);
}

// ── the payload serialiser round-trips a whole subtree ──────────────────────

class SubtreeSerialisationApp : public CtApp
{
public:
    SubtreeSerialisationApp() : CtApp{"_test_subtree_serial"} { _no_gui = true; }
private:
    void on_activate() override;
};

namespace {

struct NodeSnapshot {
    gint64        nodeId{0};
    Glib::ustring name;
    std::string   syntax;
    Glib::ustring text;
    size_t        numWidgets{0};
    int           depth{0};
};

// depth first walk, the same order both times so the vectors compare directly
void collect_subtree(CtTreeIter iter, std::vector<NodeSnapshot>& rCollected, const int depth)
{
    while (iter) {
        NodeSnapshot snapshot;
        snapshot.nodeId = iter.get_node_id();
        snapshot.name = iter.get_node_name();
        snapshot.syntax = iter.get_node_syntax_highlighting();
        snapshot.depth = depth;
        auto pBuffer = iter.get_node_text_buffer();
        if (pBuffer) snapshot.text = pBuffer->get_text();
        snapshot.numWidgets = iter.get_anchored_widgets_fast().size();
        rCollected.push_back(snapshot);
        CtTreeIter child = iter.first_child();
        collect_subtree(child, rCollected, depth + 1);
        ++iter;
    }
}

} // anonymous namespace

void SubtreeSerialisationApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    CtTreeStore& ctTreeStore = pWin->get_tree_store();

    // node "b" carries an image and a three level subtree beneath it
    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);

    std::vector<NodeSnapshot> before;
    collect_subtree(rootIter, before, 0);
    // only the subtree of "b", not its siblings
    before.erase(std::remove_if(before.begin(), before.end(),
                                [&](const NodeSnapshot& s){ return 0 == s.depth and s.nodeId != rootIter.get_node_id(); }),
                 before.end());
    ASSERT_EQ(6u, before.size()) << "expected b + c + sh + html + xml + py";
    EXPECT_LT(0u, before[0].numWidgets) << "node b should carry an image";

    const std::string payloadXml = CtProtectedAreaSerial::subtree_to_xml(pWin, rootIter);
    ASSERT_FALSE(payloadXml.empty());
    // the payload really contains the descendants, this is what gets encrypted
    EXPECT_NE(std::string::npos, payloadXml.find("cherrytree_protected_area"));
    for (const NodeSnapshot& snapshot : before) {
        EXPECT_NE(std::string::npos, payloadXml.find(std::string{"\""} + snapshot.name.raw() + "\""))
            << "node name " << snapshot.name.raw() << " missing from the payload";
    }

    // now drop the descendants, the way locking will
    {
        for (CtTreeIter child = rootIter.first_child(); child; ++child) {
            child.remove_all_embedded_widgets();
        }
        while (not rootIter->children().empty()) {
            ctTreeStore.get_store()->erase(rootIter->children().begin());
        }
    }
    ASSERT_TRUE(rootIter->children().empty());
    rootIter.remove_all_embedded_widgets();
    rootIter.set_node_text_buffer(pWin->get_new_text_buffer(), before[0].syntax);
    EXPECT_TRUE(rootIter.get_node_text_buffer()->get_text().empty());

    // and restore
    ASSERT_TRUE(CtProtectedAreaSerial::subtree_from_xml(pWin, payloadXml, rootIter));

    std::vector<NodeSnapshot> after;
    collect_subtree(rootIter, after, 0);
    after.erase(std::remove_if(after.begin(), after.end(),
                               [&](const NodeSnapshot& s){ return 0 == s.depth and s.nodeId != rootIter.get_node_id(); }),
                after.end());

    ASSERT_EQ(before.size(), after.size()) << "subtree shape changed";
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].nodeId, after[i].nodeId)   << "node id differs at " << i;
        EXPECT_EQ(before[i].name, after[i].name)       << "name differs at " << i;
        EXPECT_EQ(before[i].syntax, after[i].syntax)   << "syntax differs at " << i;
        EXPECT_EQ(before[i].text, after[i].text)       << "text differs at " << i;
        EXPECT_EQ(before[i].depth, after[i].depth)     << "depth differs at " << i;
        EXPECT_EQ(before[i].numWidgets, after[i].numWidgets) << "widget count differs at " << i;
    }

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaSerial, SubtreeRoundTrip)
{
    SubtreeSerialisationApp app;
    app.run(0, nullptr);
}

// ── widgets and drawing canvases survive the payload round-trip ─────────────

class WidgetPayloadApp : public CtApp
{
public:
    WidgetPayloadApp() : CtApp{"_test_widget_payload"} { _no_gui = true; }
private:
    void on_activate() override;
};

void WidgetPayloadApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    CtTreeStore& ctTreeStore = pWin->get_tree_store();

    // node id 5 ("e") is the widget rich one: images, a codebox and two tables.
    // by id and not by name, because it also has a shared instance
    CtTreeIter rootIter = ctTreeStore.get_node_from_node_id(5);
    ASSERT_TRUE(rootIter);
    ASSERT_TRUE(rootIter->children().empty()) << "this test wants a leaf node";

    // give it a drawing canvas too, so that path is covered
    auto* pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge and pBridge->isActive());
    auto pNodeModel = pBridge->getDocumentModel()->getNodeById(5);
    ASSERT_TRUE(pNodeModel);
    {
        CtDrawingCanvas canvas;
        canvas.x = 11.5; canvas.y = 22.5; canvas.width = 130.0; canvas.height = 140.0;
        canvas.name = "sketch";
        CtDrawingStroke stroke;
        stroke.color = "#123456";
        stroke.lineWidth = 2.5;
        stroke.opacity = 0.5;
        stroke.points = {{1.0, 2.0}, {3.0, 4.0}};
        canvas.strokes.push_back(std::move(stroke));
        pNodeModel->getDrawingCanvasesMut().push_back(std::move(canvas));
    }

    auto f_widgetTypeCounts = [](CtTreeIter& iter){
        std::map<CtAnchWidgType, size_t> counts;
        for (CtAnchoredWidget* pWidget : iter.get_anchored_widgets_fast()) {
            ++counts[pWidget->get_type()];
        }
        return counts;
    };
    const std::map<CtAnchWidgType, size_t> countsBefore = f_widgetTypeCounts(rootIter);
    const Glib::ustring textBefore = rootIter.get_node_text_buffer()->get_text();
    const std::string syntaxBefore = rootIter.get_node_syntax_highlighting();
    ASSERT_FALSE(countsBefore.empty()) << "expected widgets on node 5";

    const std::string payloadXml = CtProtectedAreaSerial::subtree_to_xml(pWin, rootIter);
    ASSERT_FALSE(payloadXml.empty());

    // wipe the node content, then bring it back from the payload
    rootIter.remove_all_embedded_widgets();
    rootIter.set_node_text_buffer(pWin->get_new_text_buffer(), syntaxBefore);
    pNodeModel->getDrawingCanvasesMut().clear();
    ASSERT_TRUE(rootIter.get_anchored_widgets_fast().empty());

    ASSERT_TRUE(CtProtectedAreaSerial::subtree_from_xml(pWin, payloadXml, rootIter));

    EXPECT_EQ(textBefore, rootIter.get_node_text_buffer()->get_text());
    const std::map<CtAnchWidgType, size_t> countsAfter = f_widgetTypeCounts(rootIter);
    EXPECT_EQ(countsBefore, countsAfter) << "widget types or counts changed across the payload";

    const auto& canvasesAfter = pNodeModel->getDrawingCanvases();
    ASSERT_EQ(1u, canvasesAfter.size()) << "drawing canvas lost";
    EXPECT_EQ("sketch", canvasesAfter[0].name);
    EXPECT_DOUBLE_EQ(11.5, canvasesAfter[0].x);
    EXPECT_DOUBLE_EQ(140.0, canvasesAfter[0].height);
    ASSERT_EQ(1u, canvasesAfter[0].strokes.size());
    EXPECT_EQ("#123456", canvasesAfter[0].strokes[0].color);
    EXPECT_EQ(2u, canvasesAfter[0].strokes[0].points.size());

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaSerial, WidgetsAndCanvasesRoundTrip)
{
    WidgetPayloadApp app;
    app.run(0, nullptr);
}

// ── end to end: subtree -> xml -> sealed blob -> sqlite -> back again ───────

class EndToEndPayloadApp : public CtApp
{
public:
    EndToEndPayloadApp() : CtApp{"_test_e2e_payload"} { _no_gui = true; }
private:
    void on_activate() override;
};

void EndToEndPayloadApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));

    fs::path tmpDir = pWin->get_ct_tmp()->getHiddenDirPath("UT_E2E");
    fs::path tmpDoc = tmpDir / "e2e_test.ctb";
    pWin->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

    // NB: file_save_as goes through CtMainWin::_reset_CtTreestore_CtTreeview(),
    // which destroys and rebuilds the tree store, so the reference must be taken
    // after it and never cached across a document level operation
    CtTreeStore& ctTreeStore = pWin->get_tree_store();

    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);
    const gint64 rootNodeId = rootIter.get_node_id();

    // a recognisable secret, to grep for in the raw file afterwards
    const Glib::ustring secretText = "correct-horse-battery-staple-42";
    CtTreeIter childIter = ctTreeStore.get_node_from_node_name("html");
    ASSERT_TRUE(childIter);
    childIter.get_node_text_buffer()->set_text(secretText);

    std::vector<NodeSnapshot> before;
    collect_subtree(rootIter, before, 0);
    before.erase(std::remove_if(before.begin(), before.end(),
                                [&](const NodeSnapshot& s){ return 0 == s.depth and s.nodeId != rootNodeId; }),
                 before.end());
    ASSERT_EQ(6u, before.size());

    // lock: serialise, seal, store, drop the plaintext nodes
    const std::string payloadXml = CtProtectedAreaSerial::subtree_to_xml(pWin, rootIter);
    ASSERT_FALSE(payloadXml.empty());
    EXPECT_NE(std::string::npos, payloadXml.find(secretText.raw())) << "secret missing from the payload";

    // protect and save first, so the node carries the on disk mark that stops
    // the storage treating the row as an orphan
    Glib::ustring protectError;
    ASSERT_TRUE(pWin->get_protected_areas().protect(rootIter, "hunter2", protectError)) << protectError.raw();
    ASSERT_TRUE(pWin->file_save(false/*need_vacuum*/));

    CtProtectedAreaRecord record;
    record.nodeId = rootNodeId;
    ASSERT_TRUE(CtCrypto::seal(payloadXml, "hunter2", record.envelope, 1000u/*fast for tests*/));

    auto* pStorage = dynamic_cast<CtStorageSqlite*>(pWin->get_ct_storage()->get_storage_entity());
    ASSERT_TRUE(pStorage);
    pStorage->write_protected_area(record);

    for (CtTreeIter child = rootIter.first_child(); child; ++child) {
        child.remove_all_embedded_widgets();
    }
    while (not rootIter->children().empty()) {
        ctTreeStore.get_store()->erase(rootIter->children().begin());
    }
    rootIter.remove_all_embedded_widgets();
    rootIter.set_node_text_buffer(pWin->get_new_text_buffer(), before[0].syntax);

    // unlock: read back, unseal, restore
    std::vector<CtProtectedAreaRecord> stored = pStorage->read_protected_areas();
    ASSERT_EQ(1u, stored.size());
    EXPECT_EQ(rootNodeId, stored[0].nodeId);

    std::string wrongPasswordPlaintext;
    EXPECT_FALSE(CtCrypto::unseal(stored[0].envelope, "hunter3", wrongPasswordPlaintext));

    std::string recoveredXml;
    ASSERT_TRUE(CtCrypto::unseal(stored[0].envelope, "hunter2", recoveredXml));
    EXPECT_EQ(payloadXml, recoveredXml);
    ASSERT_TRUE(CtProtectedAreaSerial::subtree_from_xml(pWin, recoveredXml, rootIter));

    std::vector<NodeSnapshot> after;
    collect_subtree(rootIter, after, 0);
    after.erase(std::remove_if(after.begin(), after.end(),
                               [&](const NodeSnapshot& s){ return 0 == s.depth and s.nodeId != rootNodeId; }),
                after.end());
    ASSERT_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].nodeId, after[i].nodeId) << "at " << i;
        EXPECT_EQ(before[i].name, after[i].name)     << "at " << i;
        EXPECT_EQ(before[i].text, after[i].text)     << "at " << i;
        EXPECT_EQ(before[i].numWidgets, after[i].numWidgets) << "at " << i;
    }

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaEndToEnd, SealStoreReadUnsealRestore)
{
    EndToEndPayloadApp app;
    app.run(0, nullptr);
}

// ── protect, save, reopen locked, unlock ───────────────────────────────────

class ProtectLifecycleApp : public CtApp
{
public:
    ProtectLifecycleApp() : CtApp{"_test_protect_lifecycle"} { _no_gui = true; }
private:
    void on_activate() override;
};

void ProtectLifecycleApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    const Glib::ustring secretText = "correct-horse-battery-staple-42";
    const Glib::ustring rootSecretText = "root-level-secret-zebra-99";
    fs::path tmpDoc;
    std::vector<NodeSnapshot> before;
    gint64 rootNodeId{0};

    // ── phase 1: protect a subtree and save ────────────────────────────────
    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
        tmpDoc = pWin->get_ct_tmp()->getHiddenDirPath("UT_LIFECYCLE") / "lifecycle.ctb";
        pWin->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

        CtTreeStore& ctTreeStore = pWin->get_tree_store();
        CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
        ASSERT_TRUE(rootIter);
        rootNodeId = rootIter.get_node_id();

        CtTreeIter childIter = ctTreeStore.get_node_from_node_name("html");
        ASSERT_TRUE(childIter);
        childIter.get_node_text_buffer()->set_text(secretText);
        // the protected root's OWN content must be protected too
        rootIter.get_node_text_buffer()->set_text(rootSecretText);

        collect_subtree(rootIter, before, 0);
        before.erase(std::remove_if(before.begin(), before.end(),
                                    [&](const NodeSnapshot& s){ return 0 == s.depth and s.nodeId != rootNodeId; }),
                     before.end());
        ASSERT_EQ(6u, before.size());

        CtProtectedAreas& areas = pWin->get_protected_areas();
        Glib::ustring error;
        ASSERT_TRUE(areas.protect(rootIter, "swordfish", error)) << error.raw();
        EXPECT_TRUE(areas.is_protected_root(rootNodeId));
        EXPECT_TRUE(areas.is_unlocked(rootNodeId)) << "the area stays open right after protecting";
        // the subtree is still there to work in
        EXPECT_FALSE(rootIter->children().empty());

        // nesting is refused
        CtTreeIter innerIter = ctTreeStore.get_node_from_node_name("sh");
        ASSERT_TRUE(innerIter);
        EXPECT_FALSE(areas.protect(innerIter, "other", error));
        EXPECT_EQ(rootNodeId, areas.enclosing_area_id(innerIter));

        ASSERT_TRUE(pWin->file_save(false/*need_vacuum*/));
        pWin->force_exit() = true;
        remove_window(*pWin);
    }

    // ── phase 2: the saved file must not hold the plaintext ────────────────
    {
        std::ifstream rawFile{tmpDoc.string(), std::ios::binary};
        ASSERT_TRUE(rawFile.is_open());
        const std::string rawBytes{std::istreambuf_iterator<char>{rawFile}, std::istreambuf_iterator<char>{}};
        EXPECT_EQ(std::string::npos, rawBytes.find(secretText.raw()))
            << "protected content found in clear in the saved document";
        EXPECT_EQ(std::string::npos, rawBytes.find(rootSecretText.raw()))
            << "the protected root's own content found in clear in the saved document";
        // content that was already written to this file before protecting must
        // be gone from it too, not merely left behind unreferenced
        EXPECT_EQ(std::string::npos, rawBytes.find("BOLDWORDBOLDWORDBOLDWORD"))
            << "pre-existing plaintext of the protected root survived in the file";
        EXPECT_EQ(std::string::npos, rawBytes.find("html"))
            << "a protected node name found in clear in the saved document";

        sqlite3* pDb{nullptr};
        ASSERT_EQ(SQLITE_OK, sqlite3_open_v2(tmpDoc.c_str(), &pDb, SQLITE_OPEN_READONLY, nullptr));
        sqlite3_stmt* pStmt{nullptr};
        // the descendants own no rows at all
        ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb,
            "SELECT COUNT(*) FROM children WHERE father_id=(SELECT node_id FROM node WHERE name='b')",
            -1, &pStmt, nullptr));
        ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
        EXPECT_EQ(0, sqlite3_column_int(pStmt, 0)) << "protected descendants still have children rows";
        sqlite3_finalize(pStmt);
        // and there is exactly one blob
        ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(pDb, "SELECT COUNT(*) FROM protected_area", -1, &pStmt, nullptr));
        ASSERT_EQ(SQLITE_ROW, sqlite3_step(pStmt));
        EXPECT_EQ(1, sqlite3_column_int(pStmt, 0));
        sqlite3_finalize(pStmt);
        sqlite3_close(pDb);
    }

    // ── phase 3: reopen, it is locked, then unlock and compare ─────────────
    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(tmpDoc, ""/*node*/, ""/*anchor*/));
        CtTreeStore& ctTreeStore = pWin->get_tree_store();
        CtProtectedAreas& areas = pWin->get_protected_areas();

        EXPECT_TRUE(areas.is_protected_root(rootNodeId));
        EXPECT_TRUE(areas.is_locked(rootNodeId)) << "an area must come back locked";

        CtTreeIter rootIter = ctTreeStore.get_node_from_node_id(rootNodeId);
        ASSERT_TRUE(rootIter) << "the protected root itself stays visible";
        EXPECT_EQ("b", rootIter.get_node_name());
        EXPECT_TRUE(rootIter->children().empty()) << "the descendants must not be in the tree while locked";
        EXPECT_FALSE(ctTreeStore.get_node_from_node_id(before[1].nodeId)) << "a locked descendant is reachable";

        Glib::ustring error;
        EXPECT_FALSE(areas.unlock(rootNodeId, "wrong password", error));
        EXPECT_TRUE(areas.is_locked(rootNodeId));
        EXPECT_TRUE(rootIter->children().empty()) << "a failed unlock must not leak anything";

        ASSERT_TRUE(areas.unlock(rootNodeId, "swordfish", error)) << error.raw();
        EXPECT_TRUE(areas.is_unlocked(rootNodeId));

        std::vector<NodeSnapshot> after;
        collect_subtree(rootIter, after, 0);
        after.erase(std::remove_if(after.begin(), after.end(),
                                   [&](const NodeSnapshot& s){ return 0 == s.depth and s.nodeId != rootNodeId; }),
                    after.end());
        ASSERT_EQ(before.size(), after.size()) << "the subtree did not come back whole";
        for (size_t i = 0; i < before.size(); ++i) {
            EXPECT_EQ(before[i].nodeId, after[i].nodeId) << "at " << i;
            EXPECT_EQ(before[i].name, after[i].name)     << "at " << i;
            EXPECT_EQ(before[i].text, after[i].text)     << "at " << i;
            EXPECT_EQ(before[i].numWidgets, after[i].numWidgets) << "at " << i;
        }

        // the restored nodes are in the document model too
        auto* pBridge = pWin->get_command_bridge();
        ASSERT_TRUE(pBridge and pBridge->isActive());
        for (const NodeSnapshot& snapshot : after) {
            EXPECT_TRUE(pBridge->getDocumentModel()->getNodeById(snapshot.nodeId))
                << "node " << snapshot.nodeId << " missing from the document model";
        }

        // locking again puts it away
        ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();
        EXPECT_TRUE(areas.is_locked(rootNodeId));
        EXPECT_TRUE(rootIter->children().empty());

        pWin->force_exit() = true;
        remove_window(*pWin);
    }
}

TEST(ProtectedAreaLifecycle, ProtectSaveReopenUnlock)
{
    ProtectLifecycleApp app;
    app.run(0, nullptr);
}

// ── locking purges the undo history of the nodes that leave the tree ────────

class LockPurgesUndoApp : public CtApp
{
public:
    LockPurgesUndoApp() : CtApp{"_test_lock_purge_undo"} { _no_gui = true; }
private:
    void on_activate() override;
};

void LockPurgesUndoApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    fs::path tmpDoc = pWin->get_ct_tmp()->getHiddenDirPath("UT_PURGE") / "purge.ctb";
    pWin->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

    CtTreeStore& ctTreeStore = pWin->get_tree_store();
    CtProtectedAreas& areas = pWin->get_protected_areas();
    auto* pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge and pBridge->isActive());
    CtCommandManager& commandManager = pBridge->getCommandManager();

    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);
    const gint64 rootNodeId = rootIter.get_node_id();
    CtTreeIter insideIter = ctTreeStore.get_node_from_node_name("html");
    ASSERT_TRUE(insideIter);
    const gint64 insideNodeId = insideIter.get_node_id();
    CtTreeIter outsideIter = ctTreeStore.get_node_from_node_name("d");
    ASSERT_TRUE(outsideIter);
    const gint64 outsideNodeId = outsideIter.get_node_id();

    Glib::ustring error;
    ASSERT_TRUE(areas.protect(rootIter, "swordfish", error)) << error.raw();

    // one edit inside the area, one outside it
    commandManager.clear();
    {
        auto insideCmd = std::make_unique<CompoundCommand>("edit inside");
        insideCmd->setNodeId(insideNodeId);
        commandManager.addCommandToStack(std::move(insideCmd));
        auto outsideCmd = std::make_unique<CompoundCommand>("edit outside");
        outsideCmd->setNodeId(outsideNodeId);
        commandManager.addCommandToStack(std::move(outsideCmd));
    }
    EXPECT_EQ(2u, commandManager.getUndoStackDescriptions().size());

    ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();

    const std::vector<std::string> remaining = commandManager.getUndoStackDescriptions();
    ASSERT_EQ(1u, remaining.size()) << "the entry touching the locked node should be gone";
    // the manager prefixes an index, so match on the description itself
    EXPECT_NE(std::string::npos, remaining[0].find("edit outside")) << "the wrong entry was purged: " << remaining[0];
    EXPECT_EQ(std::string::npos, remaining[0].find("edit inside"));

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaUndo, LockPurgesHistoryOfLockedNodes)
{
    LockPurgesUndoApp app;
    app.run(0, nullptr);
}

// ── changing the password invalidates the old one ───────────────────────────

class ChangePasswordApp : public CtApp
{
public:
    ChangePasswordApp() : CtApp{"_test_change_password"} { _no_gui = true; }
private:
    void on_activate() override;
};

void ChangePasswordApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    fs::path tmpDoc = pWin->get_ct_tmp()->getHiddenDirPath("UT_CHGPW") / "chgpw.ctb";
    pWin->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

    CtTreeStore& ctTreeStore = pWin->get_tree_store();
    CtProtectedAreas& areas = pWin->get_protected_areas();
    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);
    const gint64 rootNodeId = rootIter.get_node_id();

    Glib::ustring error;
    ASSERT_TRUE(areas.protect(rootIter, "first-password", error)) << error.raw();
    ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();
    ASSERT_TRUE(areas.unlock(rootNodeId, "first-password", error)) << error.raw();

    ASSERT_TRUE(areas.change_password(rootNodeId, "second-password", error)) << error.raw();
    ASSERT_TRUE(pWin->file_save(false/*need_vacuum*/));
    ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();

    // the old password must no longer open it, the new one must
    EXPECT_FALSE(areas.unlock(rootNodeId, "first-password", error));
    EXPECT_TRUE(areas.is_locked(rootNodeId));
    ASSERT_TRUE(areas.unlock(rootNodeId, "second-password", error)) << error.raw();
    EXPECT_FALSE(rootIter->children().empty()) << "the content came back under the new password";

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaPassword, ChangePasswordInvalidatesTheOldOne)
{
    ChangePasswordApp app;
    app.run(0, nullptr);
}

// ── every action referenced by the default menus really exists ──────────────

class MenuActionIdsApp : public CtApp
{
public:
    MenuActionIdsApp() : CtApp{"_test_menu_action_ids"} { _no_gui = true; }
private:
    void on_activate() override;
};

void MenuActionIdsApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    CtMenu& menu = pWin->get_ct_menu();

    std::set<std::string> knownIds;
    for (const CtMenuAction& action : menu.get_actions()) {
        knownIds.insert(action.id);
    }
    ASSERT_FALSE(knownIds.empty());

    // pull every id out of a menu config string, ignoring separators and the
    // "{SubMenuName," markers
    auto f_checkConfig = [&knownIds](const char* configName, const std::string& configStr){
        for (std::string token : str::split(configStr, ",")) {
            if (not token.empty() and '{' == token[0]) token = token.substr(1);
            if (token.empty() or "separator" == token or "}" == token) continue;
            EXPECT_NE(0u, knownIds.count(token))
                << "menu config " << configName << " refers to the unknown action '" << token << "'";
        }
    };
    f_checkConfig("MENUBAR_TREE_DEFAULT", CtConst::MENUBAR_TREE_DEFAULT);
    f_checkConfig("POPUP_NODE_DEFAULT", CtConst::POPUP_NODE_DEFAULT);

    // and specifically the password protection entries added for this feature
    for (const char* actionId : {"tree_node_protect", "tree_node_unprotect",
                                 "tree_node_change_password", "tree_lock_protected",
                                 "TreeProtectSubMenu"}) {
        EXPECT_NE(0u, knownIds.count(actionId)) << "action '" << actionId << "' is not registered";
    }

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaMenu, DefaultMenuActionIdsAllResolve)
{
    MenuActionIdsApp app;
    app.run(0, nullptr);
}

// ── formats that cannot hold the blobs must refuse, not drop content ────────

class NonSqliteRefusalApp : public CtApp
{
public:
    NonSqliteRefusalApp() : CtApp{"_test_nonsqlite_refuse"} { _no_gui = true; }
private:
    void on_activate() override;
};

void NonSqliteRefusalApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    fs::path tmpDir = pWin->get_ct_tmp()->getHiddenDirPath("UT_NOSQL");
    pWin->file_save_as((tmpDir / "nosql.ctb").string(), CtDocType::SQLite, ""/*password*/);

    CtTreeStore& ctTreeStore = pWin->get_tree_store();
    CtProtectedAreas& areas = pWin->get_protected_areas();
    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);
    const gint64 rootNodeId = rootIter.get_node_id();

    Glib::ustring error;
    ASSERT_TRUE(areas.protect(rootIter, "swordfish", error)) << error.raw();
    ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();

    // with a locked area the XML backend must refuse rather than write a file
    // that silently lacks the protected content
    {
        CtStorageXml xmlStorage{pWin};
        Glib::ustring saveError;
        const fs::path xmlPath = tmpDir / "should_not_exist.ctd";
        EXPECT_FALSE(xmlStorage.save_treestore(xmlPath, CtStorageSyncPending{}, saveError,
                                               CtExporting::NONESAVEAS));
        EXPECT_FALSE(saveError.empty()) << "the refusal must explain itself";
        EXPECT_FALSE(fs::is_regular_file(xmlPath)) << "no file should have been written";
    }

    // once unlocked it is allowed through, that is the confirmed decrypt path
    ASSERT_TRUE(areas.unlock(rootNodeId, "swordfish", error)) << error.raw();
    {
        CtStorageXml xmlStorage{pWin};
        Glib::ustring saveError;
        const fs::path xmlPath = tmpDir / "unlocked_ok.ctd";
        EXPECT_TRUE(xmlStorage.save_treestore(xmlPath, CtStorageSyncPending{}, saveError,
                                              CtExporting::NONESAVEAS)) << saveError.raw();
        EXPECT_TRUE(fs::is_regular_file(xmlPath));
    }

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaFormats, XmlRefusesWhileLockedAndAcceptsWhenUnlocked)
{
    NonSqliteRefusalApp app;
    app.run(0, nullptr);
}

// ── bookmarks inside an area survive a lock/unlock cycle ───────────────────

class BookmarkSurvivalApp : public CtApp
{
public:
    BookmarkSurvivalApp() : CtApp{"_test_bookmark_survival"} { _no_gui = true; }
private:
    void on_activate() override;
};

void BookmarkSurvivalApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    pWin->file_save_as((pWin->get_ct_tmp()->getHiddenDirPath("UT_BOOKM") / "bookm.ctb").string(),
                       CtDocType::SQLite, ""/*password*/);

    CtTreeStore& ctTreeStore = pWin->get_tree_store();
    CtProtectedAreas& areas = pWin->get_protected_areas();
    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);
    const gint64 rootNodeId = rootIter.get_node_id();
    CtTreeIter insideIter = ctTreeStore.get_node_from_node_name("html");
    ASSERT_TRUE(insideIter);
    const gint64 insideNodeId = insideIter.get_node_id();

    ctTreeStore.bookmarks_add(insideNodeId);
    ASSERT_TRUE(ctTreeStore.is_node_bookmarked(insideNodeId));

    Glib::ustring error;
    ASSERT_TRUE(areas.protect(rootIter, "swordfish", error)) << error.raw();
    ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();
    // while locked the node is gone, so its bookmark is gone from the menu too
    EXPECT_FALSE(ctTreeStore.is_node_bookmarked(insideNodeId));

    ASSERT_TRUE(areas.unlock(rootNodeId, "swordfish", error)) << error.raw();
    EXPECT_TRUE(ctTreeStore.is_node_bookmarked(insideNodeId))
        << "the bookmark should have travelled inside the encrypted payload";

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaBookmarks, SurviveALockUnlockCycle)
{
    BookmarkSurvivalApp app;
    app.run(0, nullptr);
}

// ── locking must stop showing the content that was on screen ───────────────

class LockStopsShowingApp : public CtApp
{
public:
    LockStopsShowingApp() : CtApp{"_test_cursormove"} { _no_gui = true; }
private:
    void on_activate() override;
};

void LockStopsShowingApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
    pWin->file_save_as((pWin->get_ct_tmp()->getHiddenDirPath("UT_CURSOR") / "cursor.ctb").string(),
                       CtDocType::SQLite, ""/*password*/);

    CtTreeStore& ctTreeStore = pWin->get_tree_store();
    CtProtectedAreas& areas = pWin->get_protected_areas();
    CtTreeIter rootIter = ctTreeStore.get_node_from_node_name("b");
    ASSERT_TRUE(rootIter);
    const gint64 rootNodeId = rootIter.get_node_id();

    // sit on the area's own root, the way someone reading it would
    pWin->get_tree_view().set_cursor_safe(rootIter);
    ASSERT_EQ(rootNodeId, pWin->curr_tree_iter().get_node_id());

    const Glib::ustring secretText = "still-on-screen-armadillo-31";
    rootIter.get_node_text_buffer()->set_text(secretText);
    ctTreeStore.text_view_apply_textbuffer(rootIter, &pWin->get_text_view());
    ASSERT_NE(std::string::npos, pWin->curr_buffer()->get_text().raw().find(secretText.raw()))
        << "the fixture should start with the protected content on screen";

    Glib::ustring error;
    ASSERT_TRUE(areas.protect(rootIter, "swordfish", error)) << error.raw();
    ASSERT_TRUE(areas.lock(rootNodeId, error)) << error.raw();

    // the whole point: it must not still be on screen after the area closed
    EXPECT_EQ(std::string::npos, pWin->curr_buffer()->get_text().raw().find(secretText.raw()))
        << "the locked content is still displayed after the area closed";
    EXPECT_TRUE(rootIter->children().empty());
    EXPECT_TRUE(areas.is_locked(rootNodeId));

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(ProtectedAreaLock, LockStopsShowingTheContent)
{
    LockStopsShowingApp app;
    app.run(0, nullptr);
}

// ── removing the protection must actually stick across a reopen ────────────
// Regression test for the reported bug: unprotect cleared the in memory record
// but left the protected_area row behind, so the area came back on reload.

class UnprotectPersistsApp : public CtApp
{
public:
    UnprotectPersistsApp() : CtApp{"_test_unprotect_persists"} { _no_gui = true; }
private:
    void on_activate() override;
};

void UnprotectPersistsApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    fs::path tmpDoc;
    gint64 childNodeId{0};
    gint64 parentNodeId{0};
    const Glib::ustring secretText = "unprotect-me-pangolin-77";

    // protect a child node, save
    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
        tmpDoc = pWin->get_ct_tmp()->getHiddenDirPath("UT_UNPROT") / "unprot.ctb";
        pWin->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);

        CtTreeStore& ctTreeStore = pWin->get_tree_store();
        CtTreeIter childIter = ctTreeStore.get_node_from_node_name("sh");
        ASSERT_TRUE(childIter);
        childNodeId = childIter.get_node_id();
        CtTreeIter parentIter = ctTreeStore.get_node_from_node_name("b");
        ASSERT_TRUE(parentIter);
        parentNodeId = parentIter.get_node_id();
        childIter.get_node_text_buffer()->set_text(secretText);

        Glib::ustring error;
        ASSERT_TRUE(pWin->get_protected_areas().protect(childIter, "swordfish", error)) << error.raw();
        ASSERT_TRUE(pWin->file_save(false/*need_vacuum*/));
        pWin->force_exit() = true;
        remove_window(*pWin);
    }

    // reopen, remove the protection, save, close
    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(tmpDoc, ""/*node*/, ""/*anchor*/));
        CtProtectedAreas& areas = pWin->get_protected_areas();
        ASSERT_TRUE(areas.is_protected_root(childNodeId));

        Glib::ustring error;
        ASSERT_TRUE(areas.unprotect(childNodeId, "swordfish", error)) << error.raw();
        EXPECT_FALSE(areas.is_protected_root(childNodeId));
        ASSERT_TRUE(pWin->file_save(false/*need_vacuum*/));
        pWin->force_exit() = true;
        remove_window(*pWin);
    }

    // reopen again: it must be an ordinary node
    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(tmpDoc, ""/*node*/, ""/*anchor*/));
        CtTreeStore& ctTreeStore = pWin->get_tree_store();
        CtProtectedAreas& areas = pWin->get_protected_areas();

        EXPECT_FALSE(areas.is_protected_root(childNodeId)) << "the protection came back from the dead";
        EXPECT_FALSE(areas.is_locked(childNodeId));
        EXPECT_FALSE(areas.has_any()) << "a stale blob was left in the document";

        CtTreeIter childIter = ctTreeStore.get_node_from_node_id(childNodeId);
        ASSERT_TRUE(childIter);
        // its content and its subtree are back in the open
        EXPECT_EQ(secretText, childIter.get_node_text_buffer()->get_text());
        EXPECT_FALSE(childIter->children().empty()) << "the subnodes did not come back";

        // and now the parent can be protected, which was the second symptom
        CtTreeIter parentIter = ctTreeStore.get_node_from_node_id(parentNodeId);
        ASSERT_TRUE(parentIter);
        Glib::ustring error;
        EXPECT_TRUE(areas.protect(parentIter, "newpassword", error)) << error.raw();

        pWin->force_exit() = true;
        remove_window(*pWin);
    }
}

TEST(ProtectedAreaUnprotect, RemovingProtectionSurvivesAReopen)
{
    UnprotectPersistsApp app;
    app.run(0, nullptr);
}

// ── a document already left in the broken state repairs itself ─────────────

class StaleBlobRepairApp : public CtApp
{
public:
    StaleBlobRepairApp() : CtApp{"_test_stale_blob_repair"} { _no_gui = true; }
private:
    void on_activate() override;
};

void StaleBlobRepairApp::on_activate()
{
    _on_startup();
    auto quitGuard = scope_guard([this](void*) { quit(); });

    fs::path tmpDoc;
    gint64 nodeId{0};
    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(UT::testCtbDocPath, ""/*node*/, ""/*anchor*/));
        tmpDoc = pWin->get_ct_tmp()->getHiddenDirPath("UT_STALE") / "stale.ctb";
        pWin->file_save_as(tmpDoc.string(), CtDocType::SQLite, ""/*password*/);
        CtTreeIter iter = pWin->get_tree_store().get_node_from_node_name("b");
        ASSERT_TRUE(iter);
        nodeId = iter.get_node_id();

        // fabricate the state the bug produced: a blob whose node is not marked
        // as a protected root
        CtProtectedAreaRecord record;
        record.nodeId = nodeId;
        ASSERT_TRUE(CtCrypto::seal("<cherrytree_protected_area/>", "pw", record.envelope, 1000u));
        auto* pStorage = dynamic_cast<CtStorageSqlite*>(pWin->get_ct_storage()->get_storage_entity());
        ASSERT_TRUE(pStorage);
        pStorage->write_protected_area(record);

        std::vector<gint64> staleIds;
        EXPECT_TRUE(pStorage->read_protected_areas(&staleIds).empty())
            << "an orphaned row must not be returned as a real area";
        ASSERT_EQ(1u, staleIds.size()) << "and it should be reported as stale";
        EXPECT_EQ(nodeId, staleIds[0]);

        pWin->force_exit() = true;
        remove_window(*pWin);
    }

    {
        CtMainWin* pWin = _create_window(true/*start_hidden*/);
        ASSERT_TRUE(pWin->file_open(tmpDoc, ""/*node*/, ""/*anchor*/));
        EXPECT_FALSE(pWin->get_protected_areas().is_protected_root(nodeId));
        EXPECT_FALSE(pWin->get_protected_areas().is_locked(nodeId));

        // and saving clears it out of the document for good
        ASSERT_TRUE(pWin->file_save(false/*need_vacuum*/));
        auto* pStorage = dynamic_cast<CtStorageSqlite*>(pWin->get_ct_storage()->get_storage_entity());
        ASSERT_TRUE(pStorage);
        std::vector<gint64> staleIds;
        EXPECT_TRUE(pStorage->read_protected_areas(&staleIds).empty());
        EXPECT_TRUE(staleIds.empty()) << "the stale row should have been deleted from the table";

        pWin->force_exit() = true;
        remove_window(*pWin);
    }
}

TEST(ProtectedAreaUnprotect, StaleBlobIsIgnoredAndCleanedUp)
{
    StaleBlobRepairApp app;
    app.run(0, nullptr);
}
