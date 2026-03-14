/*
 * tests_all_command_descriptions.cpp
 *
 * Comprehensive test to verify all commands show proper descriptions
 * and no spurious commands are created in the undo/redo stack
 */

#include "ct_app.h"
#include "ct_main_win.h"
#include "ct_command_bridge.h"
#include "tests_common.h"
#include <gtest/gtest.h>

class AllCommandDescriptionsTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a minimal document for testing
        _pCtApp = std::make_shared<CtApp>();
        _pCtMainWin = _pCtApp->create_window(true/*no_gui*/);
        _pCtMainWin->get_command_bridge()->setActive(true);

        // Create a simple test document
        _pCtMainWin->file_new();
        auto tree_store = &_pCtMainWin->get_tree_store();
        auto test_node_id = tree_store->appendNode(nullptr, "Test Node", -1);
        auto iter = tree_store->get_node_from_node_id(test_node_id);
        _pCtMainWin->get_tree_view().set_cursor_safe(iter);
    }

    void TearDown() override {
        if (_pCtMainWin) {
            delete _pCtMainWin;
            _pCtMainWin = nullptr;
        }
    }

    std::vector<std::string> getUndoDescriptions() {
        auto bridge = _pCtMainWin->get_command_bridge();
        return bridge->getUndoStackDescriptions();
    }

    void clearUndoStack() {
        auto bridge = _pCtMainWin->get_command_bridge();
        while (bridge->canUndo()) {
            bridge->undo();
        }
        while (bridge->canRedo()) {
            bridge->redo();
        }
        // Clear by creating and discarding a dummy session
        auto buffer = _pCtMainWin->get_text_view().get_buffer();
        buffer->set_text("");
    }

    void simulateTyping(const std::string& text) {
        auto buffer = _pCtMainWin->get_text_view().get_buffer();
        buffer->insert(buffer->end(), text);
        // Give edit session time to be created and ended
        g_usleep(10000); // 10ms
    }

    void simulatePaste(const std::string& text) {
        auto bridge = _pCtMainWin->get_command_bridge();
        auto nodeId = _pCtMainWin->curr_tree_iter().get_node_id();
        auto buffer = _pCtMainWin->get_text_view().get_buffer();

        // Simulate paste operation flow
        bridge->cancelTextEditSession();
        bridge->beginPaste(nodeId);
        buffer->insert(buffer->end(), text);
        bridge->endPaste();
        bridge->beginTextEditSession(nodeId);
    }

    void simulateCut() {
        auto bridge = _pCtMainWin->get_command_bridge();
        auto nodeId = _pCtMainWin->curr_tree_iter().get_node_id();
        auto buffer = _pCtMainWin->get_text_view().get_buffer();

        if (!buffer->get_has_selection()) {
            buffer->select_range(buffer->begin(), buffer->end());
        }

        // Simulate cut operation flow
        bridge->cancelTextEditSession();
        bridge->beginCut(nodeId);
        buffer->erase(buffer->get_selection_bounds().first, buffer->get_selection_bounds().second);
        bridge->endCut();
        bridge->beginTextEditSession(nodeId);
    }

    std::shared_ptr<CtApp> _pCtApp;
    CtMainWin* _pCtMainWin{nullptr};
};

TEST_F(AllCommandDescriptionsTest, TypingNewlineShowsCorrectDescription) {
    clearUndoStack();
    simulateTyping("\n");

    auto descriptions = getUndoDescriptions();
    ASSERT_EQ(descriptions.size(), 1) << "Should have exactly 1 command";
    EXPECT_TRUE(descriptions[0].find("newline") != std::string::npos)
        << "Newline command should contain 'newline', got: " << descriptions[0];
}

TEST_F(AllCommandDescriptionsTest, TypingTextShowsCorrectDescription) {
    clearUndoStack();
    simulateTyping("Hello World");

    auto descriptions = getUndoDescriptions();
    ASSERT_EQ(descriptions.size(), 1) << "Should have exactly 1 command";
    EXPECT_TRUE(descriptions[0].find("Type") != std::string::npos)
        << "Typing command should contain 'Type', got: " << descriptions[0];
    EXPECT_TRUE(descriptions[0].find("Hello") != std::string::npos)
        << "Typing command should show typed text, got: " << descriptions[0];
}

TEST_F(AllCommandDescriptionsTest, PasteShowsSingleCommand) {
    clearUndoStack();
    simulatePaste("Pasted content");

    auto descriptions = getUndoDescriptions();
    ASSERT_EQ(descriptions.size(), 1) << "Paste should create exactly 1 command, not " << descriptions.size();
    EXPECT_TRUE(descriptions[0].find("Paste") != std::string::npos)
        << "Paste command should contain 'Paste', got: " << descriptions[0];
}

TEST_F(AllCommandDescriptionsTest, TypingThenPasteShowsTwoCommands) {
    clearUndoStack();
    simulateTyping("Before");
    simulatePaste("Pasted");

    auto descriptions = getUndoDescriptions();
    ASSERT_EQ(descriptions.size(), 2) << "Should have exactly 2 commands (typing + paste)";
    EXPECT_TRUE(descriptions[0].find("Before") != std::string::npos)
        << "First command should be typing 'Before', got: " << descriptions[0];
    EXPECT_TRUE(descriptions[1].find("Paste") != std::string::npos)
        << "Second command should be 'Paste', got: " << descriptions[1];
}

TEST_F(AllCommandDescriptionsTest, CutShowsSingleCommand) {
    clearUndoStack();
    simulateTyping("Text to cut");
    auto beforeCut = getUndoDescriptions().size();

    simulateCut();

    auto descriptions = getUndoDescriptions();
    ASSERT_EQ(descriptions.size(), beforeCut + 1) << "Cut should create exactly 1 additional command";
    EXPECT_TRUE(descriptions.back().find("Cut") != std::string::npos)
        << "Cut command should contain 'Cut', got: " << descriptions.back();
}

TEST_F(AllCommandDescriptionsTest, NoSpuriousEmptyCommands) {
    clearUndoStack();

    // Perform various operations
    simulateTyping("Hello");
    simulatePaste("World");
    simulateCut();
    simulateTyping("Again");

    auto descriptions = getUndoDescriptions();

    // Check that no empty "Type """" commands exist
    for (const auto& desc : descriptions) {
        EXPECT_FALSE(desc.find("Type \"\"") != std::string::npos)
            << "Found spurious empty Type command: " << desc;
    }
}

TEST_F(AllCommandDescriptionsTest, AllCommandsHaveNodeId) {
    clearUndoStack();

    simulateTyping("Test");
    simulatePaste("Paste");

    auto descriptions = getUndoDescriptions();
    auto nodeId = _pCtMainWin->curr_tree_iter().get_node_id();
    std::string nodeStr = "Node " + std::to_string(nodeId);

    for (const auto& desc : descriptions) {
        EXPECT_TRUE(desc.find(nodeStr) != std::string::npos)
            << "Command should contain node ID, got: " << desc;
    }
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
