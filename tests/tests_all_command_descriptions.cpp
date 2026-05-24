/*
 * tests_all_command_descriptions.cpp
 *
 * Comprehensive test to verify all commands show proper descriptions
 * and no spurious commands are created in the undo/redo stack
 */

#include "tests_common.h"
#include "ct_app.h"
#include "ct_command_bridge.h"
#include <gtest/gtest.h>

class TestAllCommandDescriptionsApp : public CtApp
{
public:
    TestAllCommandDescriptionsApp() : CtApp{"_test_all_cmd_desc"} { _no_gui = true; }

private:
    void on_activate() final;
    void _run_tests(CtMainWin* pWin);

    void _test_typing_text_description(CtMainWin* pWin);
    void _test_paste_single_command(CtMainWin* pWin);
    void _test_cut_single_command(CtMainWin* pWin);
    void _test_no_spurious_empty_commands(CtMainWin* pWin);
    void _test_all_commands_have_node_id(CtMainWin* pWin);
};

void TestAllCommandDescriptionsApp::on_activate()
{
    _on_startup();

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));

    _run_tests(pWin);

    pWin->force_exit() = true;
    remove_window(*pWin);
}

void TestAllCommandDescriptionsApp::_run_tests(CtMainWin* pWin)
{
    auto pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge);
    ASSERT_TRUE(pBridge->isActive());

    spdlog::info("=== All Command Descriptions Testing ===");

    _test_typing_text_description(pWin);
    _test_paste_single_command(pWin);
    _test_cut_single_command(pWin);
    _test_no_spurious_empty_commands(pWin);
    _test_all_commands_have_node_id(pWin);
}

void TestAllCommandDescriptionsApp::_test_typing_text_description(CtMainWin* pWin)
{
    spdlog::info("Test: typing text shows correct description");

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);
    auto pBridge = pWin->get_command_bridge();

    buffer->insert(buffer->end(), "Hello World");
    buffer->set_modified(true);

    // End edit session by switching to another node
    auto otherIter = pWin->get_tree_store().get_node_from_node_name("html");
    ASSERT_TRUE(otherIter);
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(otherIter));

    // descriptions[0] is most recent
    auto descriptions = pBridge->getUndoStackDescriptions();
    ASSERT_FALSE(descriptions.empty()) << "Should have at least 1 command after typing";
    EXPECT_TRUE(descriptions[0].find("Type") != std::string::npos ||
                descriptions[0].find("Hello") != std::string::npos)
        << "Most recent command should describe typing, got: " << descriptions[0];
}

void TestAllCommandDescriptionsApp::_test_paste_single_command(CtMainWin* pWin)
{
    spdlog::info("Test: paste shows single command");

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);
    auto pBridge = pWin->get_command_bridge();

    size_t beforeCount = pBridge->getUndoStackDescriptions().size();

    // Simulate paste
    pBridge->cancelTextEditSession();
    pBridge->beginPaste(nodeId);
    buffer->insert(buffer->end(), "Pasted content");
    pBridge->endPaste();
    pBridge->beginTextEditSession(nodeId);

    auto descriptions = pBridge->getUndoStackDescriptions();
    EXPECT_EQ(descriptions.size(), beforeCount + 1)
        << "Paste should create exactly 1 command";
    EXPECT_TRUE(descriptions[0].find("Paste") != std::string::npos)
        << "Most recent command should contain 'Paste', got: " << descriptions[0];
}

void TestAllCommandDescriptionsApp::_test_cut_single_command(CtMainWin* pWin)
{
    spdlog::info("Test: cut shows single command");

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);
    auto pBridge = pWin->get_command_bridge();

    size_t beforeCount = pBridge->getUndoStackDescriptions().size();

    // Select all and simulate cut
    buffer->select_range(buffer->begin(), buffer->end());
    Gtk::TextIter selStart, selEnd;
    buffer->get_selection_bounds(selStart, selEnd);

    pBridge->cancelTextEditSession();
    pBridge->beginCut(nodeId);
    buffer->erase(selStart, selEnd);
    pBridge->endCut();
    pBridge->beginTextEditSession(nodeId);

    auto descriptions = pBridge->getUndoStackDescriptions();
    EXPECT_GE(descriptions.size(), beforeCount + 1)
        << "Cut should create at least 1 additional command";
    EXPECT_TRUE(descriptions[0].find("Cut") != std::string::npos)
        << "Most recent command should contain 'Cut', got: " << descriptions[0];
}

void TestAllCommandDescriptionsApp::_test_no_spurious_empty_commands(CtMainWin* pWin)
{
    spdlog::info("Test: no spurious empty commands");

    auto pBridge = pWin->get_command_bridge();
    auto descriptions = pBridge->getUndoStackDescriptions();

    for (const auto& desc : descriptions) {
        EXPECT_FALSE(desc.find("Type \"\"") != std::string::npos)
            << "Found spurious empty Type command: " << desc;
    }
}

void TestAllCommandDescriptionsApp::_test_all_commands_have_node_id(CtMainWin* pWin)
{
    spdlog::info("Test: all commands have node ID");

    auto pBridge = pWin->get_command_bridge();
    auto descriptions = pBridge->getUndoStackDescriptions();

    for (const auto& desc : descriptions) {
        EXPECT_TRUE(desc.find("Node") != std::string::npos)
            << "Command should contain node reference, got: " << desc;
    }
}

TEST(AllCommandDescriptionTests, CommandDescriptions)
{
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);

    TestAllCommandDescriptionsApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);
}
