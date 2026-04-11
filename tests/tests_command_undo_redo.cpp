/*
 * tests_command_undo_redo.cpp
 *
 * Verifies that the command pattern properly implements undo/redo.
 */

#include "tests_common.h"
#include "ct_app.h"
#include "ct_command_bridge.h"
#include <gtest/gtest.h>
#include <fstream>

class TestUndoRedoApp : public CtApp
{
public:
    TestUndoRedoApp() : CtApp{"_test_undo_redo", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
    void _run_tests(CtMainWin* pWin);
};

void TestUndoRedoApp::on_activate()
{
    _on_startup();

    // Create window and load test document
    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));

    // Realize the window (required for cursor/buffer operations) then hide it
    pWin->show_all();
    pWin->hide();
    while (gtk_events_pending()) gtk_main_iteration_do(false);

    _run_tests(pWin);

    // Cleanup
    pWin->force_exit() = true;
    remove_window(*pWin);
}

void TestUndoRedoApp::_run_tests(CtMainWin* pWin)
{
    auto drainEvents = [](){
        while (gtk_events_pending()) gtk_main_iteration_do(false);
    };

    auto pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge);
    ASSERT_TRUE(pBridge->isActive());

    spdlog::info("=== Command Pattern Undo/Redo Testing ===");

    // TEST 1: Text editing undo/redo (model level)
    {
        spdlog::info("Test 1: Text editing → undo → redo (command pattern verification)");

        // Find a rich text node without complex widgets
        auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
        ASSERT_TRUE(ctIter);
        gint64 nodeId = ctIter.get_node_id();

        // Select node "b" and let GTK settle
        pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);

        // Store original model content
        auto docModel = pBridge->getDocumentModel();
        auto node = docModel->getNodeById(nodeId);
        ASSERT_TRUE(node);
        Glib::ustring originalXml = node->getContentXml();

        // End any session that was auto-started by the cursor change, then
        // start a fresh session explicitly so we control exactly what's captured.
        pBridge->endTextEditSession();
        pBridge->beginTextEditSession(nodeId);

        // Insert text — the active session captures this
        buffer->insert(buffer->end(), "\nTest undo/redo");

        // End session explicitly (creates the undo command)
        pBridge->endTextEditSession();
        drainEvents();

        // Verify model was modified
        Glib::ustring modifiedXml = node->getContentXml();
        ASSERT_NE(originalXml, modifiedXml);

        // Undo — still on node "b", so no node-switch happens during undo
        ASSERT_TRUE(pBridge->canUndo());
        pBridge->undo();
        drainEvents();

        Glib::ustring afterUndoXml = node->getContentXml();

        // Verify undo fully restored the original content
        ASSERT_EQ(originalXml, afterUndoXml);
        ASSERT_TRUE(afterUndoXml.find("Test undo/redo") == Glib::ustring::npos);

        // Redo — verify model re-applies change
        ASSERT_TRUE(pBridge->canRedo());
        pBridge->redo();
        drainEvents();

        Glib::ustring afterRedoXml = node->getContentXml();
        ASSERT_TRUE(afterRedoXml.find("Test undo/redo") != Glib::ustring::npos);
        ASSERT_NE(originalXml, afterRedoXml);

        spdlog::info("✓ Test 1 passed: Text undo/redo works at model level");
    }

    // TEST 2: canUndo/canRedo state management
    {
        spdlog::info("Test 2: canUndo/canRedo tracking");

        // After Test 1, we should have undo/redo available
        int undoCount = 0;
        while (pBridge->canUndo()) {
            pBridge->undo();
            drainEvents();
            undoCount++;
        }
        ASSERT_GT(undoCount, 0); // Should have at least the edit from Test 1

        // Now no more undos
        ASSERT_FALSE(pBridge->canUndo());

        // But we should have redos
        int redoCount = 0;
        while (pBridge->canRedo()) {
            pBridge->redo();
            drainEvents();
            redoCount++;
        }
        ASSERT_EQ(undoCount, redoCount); // Should match

        // Now no more redos
        ASSERT_FALSE(pBridge->canRedo());

        spdlog::info("✓ Test 2 passed: Undo/redo stack management works");
    }

    spdlog::info("=== All command undo/redo tests passed! ===");
}

TEST(CommandUndoRedoTests, Phase6_2_UndoRedoFunctionality)
{
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);

    TestUndoRedoApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);
}
