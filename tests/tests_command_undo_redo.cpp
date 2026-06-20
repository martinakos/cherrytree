/*
 * tests_command_undo_redo.cpp
 *
 * Verifies that the command pattern properly implements undo/redo.
 */

#include "tests_common.h"
#include "ct_app.h"
#include "ct_clipboard.h"
#include "ct_command_bridge.h"
#include "ct_const.h"
#include "ct_text_commands.h"
#include "ct_node_commands.h"
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
    auto _quitGuard = scope_guard([this](void*) { quit(); });

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
        Glib::ustring originalXml = node->getContent().toXml();

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
        Glib::ustring modifiedXml = node->getContent().toXml();
        ASSERT_NE(originalXml, modifiedXml);

        // Undo — still on node "b", so no node-switch happens during undo
        ASSERT_TRUE(pBridge->canUndo());
        pBridge->undo();
        drainEvents();

        Glib::ustring afterUndoXml = node->getContent().toXml();

        // Verify undo fully restored the original content
        ASSERT_EQ(originalXml, afterUndoXml);
        ASSERT_TRUE(afterUndoXml.find("Test undo/redo") == Glib::ustring::npos);

        // Redo — verify model re-applies change
        ASSERT_TRUE(pBridge->canRedo());
        pBridge->redo();
        drainEvents();

        Glib::ustring afterRedoXml = node->getContent().toXml();
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

// ─── Middle-click PRIMARY-selection paste ────────────────────────────────────
//
// Regression test for the bug where middle-click paste (X11 PRIMARY selection)
// inserted text into the buffer without going through the command bridge,
// producing no undo entry. The fix routes middle-click through the same
// _paste_clipboard path as Ctrl+V via paste_from_primary().

class TestPastePrimaryApp : public CtApp
{
public:
    TestPastePrimaryApp() : CtApp{"_test_paste_primary", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
};

void TestPastePrimaryApp::on_activate()
{
    _on_startup();

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));

    pWin->show_all();
    pWin->hide();
    auto drainEvents = [](){
        while (gtk_events_pending()) gtk_main_iteration_do(false);
    };
    drainEvents();

    auto pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge);
    ASSERT_TRUE(pBridge->isActive());

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    drainEvents();

    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);

    // Park the cursor at the end so the paste insertion point is deterministic,
    // then drop any session the cursor change auto-started so we measure the
    // command-stack delta caused only by the paste itself.
    buffer->place_cursor(buffer->end());
    pBridge->endTextEditSession();
    drainEvents();

    const Glib::ustring originalText = buffer->get_text();
    const size_t baselineUndoSize = pBridge->getUndoStackDescriptions().size();

    // Stage the PRIMARY selection (the same buffer GTK pastes from on middle-click).
    const Glib::ustring pasteText = "PRIMARY-paste-test";
    Gtk::Clipboard::get(GDK_SELECTION_PRIMARY)->set_text(pasteText);
    drainEvents();

    // Invoke the same code path the middle-click button-press handler runs.
    CtClipboard{pWin}.paste_from_primary(&pWin->get_text_view().mm(), nullptr);
    drainEvents();

    // The text must have been inserted ...
    const Glib::ustring afterPasteText = buffer->get_text();
    ASSERT_NE(originalText, afterPasteText);
    ASSERT_TRUE(afterPasteText.find(pasteText) != Glib::ustring::npos);

    // ... and a new undo command must have been pushed (the regression check).
    const auto undoStack = pBridge->getUndoStackDescriptions();
    ASSERT_GT(undoStack.size(), baselineUndoSize);
    ASSERT_TRUE(pBridge->canUndo());

    // Undo must reverse the paste.
    pBridge->undo();
    drainEvents();
    const Glib::ustring afterUndoText = buffer->get_text();
    ASSERT_TRUE(afterUndoText.find(pasteText) == Glib::ustring::npos);

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(CommandUndoRedoTests, MiddleClickPrimaryPasteCreatesUndoCommand)
{
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);

    TestPastePrimaryApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);
}

// ─── InsertTextCommand space description ────────────────────────────────────
// Regression: single-space InsertTextCommands used to return "" as description,
// causing CtTextEditSession::end() to discard entire sessions and lose model
// changes from the undo stack.

TEST(CommandUndoRedoTests, InsertTextCommand_SpaceHasDescription)
{
    auto model = std::make_shared<CtDocumentModel>();
    auto node = model->createNode(1);
    model->addNode(node, 0);

    InsertTextCommand cmd(model, 1, 0, " ", {});
    std::string desc = cmd.getDescription();
    ASSERT_FALSE(desc.empty());
    ASSERT_NE(desc.find("Space"), std::string::npos);
}

// ─── CtTextEditSession: space-only sessions produce commands ────────────────
// Regression: sessions with only space inserts were discarded by the allEmpty
// check, silently dropping model changes from the undo history.

TEST(CommandUndoRedoTests, SpaceOnlySession_ProducesCommand)
{
    auto model = std::make_shared<CtDocumentModel>();
    auto node = model->createNode(1);
    node->setName("test");
    node->setSyntax("custom-colors");
    model->addNode(node, 0);

    CtTextEditSession session(model);

    // Simulate what beginTextEditSession + buffer insert does:
    // create a minimal buffer, begin session, then manually call onBufferInsert
    auto buffer = Gtk::TextBuffer::create();
    session.begin(1, buffer, nullptr);

    // Insert a space into both the model and the session
    node->getContent().insertText(0, " ", {});
    buffer->insert(buffer->end(), " ");

    auto cmd = session.end(buffer, {}, 1);
    ASSERT_NE(cmd, nullptr);
    ASSERT_FALSE(cmd->getDescription().empty());
}

// ─── pushNodeCommand restarts edit session ──────────────────────────────────
// Regression: after pushNodeCommand, no edit session was active. Characters
// typed before the next cursor-change event went into the GTK buffer untracked,
// making them invisible to undo/redo.

class TestNodeCommandSessionApp : public CtApp
{
public:
    TestNodeCommandSessionApp() : CtApp{"_test_node_cmd_session", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
};

void TestNodeCommandSessionApp::on_activate()
{
    _on_startup();

    CtMainWin* pWin = _create_window(true);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, "", "", UT::testPassword));

    pWin->show_all();
    pWin->hide();
    auto drainEvents = [](){ while (gtk_events_pending()) gtk_main_iteration_do(false); };
    drainEvents();

    auto pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge && pBridge->isActive());

    // Select a rich-text node
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    drainEvents();

    auto docModel = pBridge->getDocumentModel();
    auto node = docModel->getNodeById(nodeId);
    ASSERT_TRUE(node);

    // Flush any auto-started session
    pBridge->endTextEditSession();
    drainEvents();

    size_t undoStackBefore = pBridge->getUndoStackDescriptions().size();

    // Simulate a node command (AddNodeCommand for a child node)
    CtNodeProps props;
    props.name = "temp_child";
    props.syntax = "custom-colors";
    gint64 childId = 9999;
    auto addCmd = std::make_unique<AddNodeCommand>(
        docModel.get(), childId, nodeId, -1, props, CtNodeContent{});
    pBridge->pushNodeCommand(std::move(addCmd));
    drainEvents();

    // After pushNodeCommand, an edit session should be active.
    // Insert text into the current buffer — it should be captured.
    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);
    buffer->insert(buffer->end(), "tracked");
    pBridge->endTextEditSession();
    drainEvents();

    // The undo stack should have grown by at least 2:
    // 1 for AddNodeCommand + 1 for the text edit
    size_t undoStackAfter = pBridge->getUndoStackDescriptions().size();
    EXPECT_GE(undoStackAfter, undoStackBefore + 2);

    // Undo all and redo all — the text edit must survive the round-trip
    while (pBridge->canUndo()) {
        pBridge->undo();
        drainEvents();
    }
    while (pBridge->canRedo()) {
        pBridge->redo();
        drainEvents();
    }

    // After full redo, the buffer should contain "tracked"
    buffer = pWin->curr_buffer();
    if (buffer) {
        Glib::ustring text = buffer->get_text();
        EXPECT_TRUE(text.find("tracked") != Glib::ustring::npos)
            << "Expected 'tracked' in buffer after full undo+redo, got: " << text.raw();
    }

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(CommandUndoRedoTests, PushNodeCommand_RestartsEditSession)
{
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);

    TestNodeCommandSessionApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);
}

// ─── Paste of formatted text survives undo/redo ─────────────────────────────
// Regression: pasting rich text with formatting but no widgets used the
// lightweight delta path, which captured run attributes during the insert-text
// signal (before insert_with_tags_by_name applied the tags). The paste looked
// right but its undo command stored unformatted text, so redo rebuilt the
// buffer with all formatting lost. Formatted pastes now use the XML snapshot
// path. This drives the real copy→paste code path end to end.

class TestPasteRichFormatApp : public CtApp
{
public:
    TestPasteRichFormatApp() : CtApp{"_test_paste_rich_format", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
};

void TestPasteRichFormatApp::on_activate()
{
    _on_startup();

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, "", "", UT::testPassword));

    pWin->show_all();
    pWin->hide();
    auto drainEvents = [](){ while (gtk_events_pending()) gtk_main_iteration_do(false); };
    drainEvents();

    auto pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge && pBridge->isActive());

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    ASSERT_TRUE(ctIter.get_node_is_rich_text());
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    drainEvents();

    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);

    CtPairCodeboxMainWin pair{nullptr, pWin};
    GtkTextView* pGtkTextView = GTK_TEXT_VIEW(pWin->get_text_view().mm().gobj());

    const std::string boldTagName =
        pWin->get_text_tag_name_exist_or_create(CtConst::TAG_WEIGHT, CtConst::TAG_PROP_VAL_HEAVY);
    auto boldTag = buffer->get_tag_table()->lookup(boldTagName);
    ASSERT_TRUE(boldTag);

    auto isBoldAtOffset = [&](int offset) {
        return buffer->get_iter_at_offset(offset).has_tag(boldTag);
    };
    const Glib::ustring boldWord = "BOLDWORD";
    auto countBoldWord = [&]() {
        const Glib::ustring text = buffer->get_text();
        size_t count = 0, pos = 0;
        while ((pos = text.find(boldWord, pos)) != Glib::ustring::npos) { ++count; pos += boldWord.size(); }
        return count;
    };

    // Create a bold run at the end of the buffer to act as the copy source.
    {
        Gtk::TextIter endIter = buffer->end();
        const int boldStart = endIter.get_offset();
        buffer->insert_with_tags_by_name(endIter, boldWord, std::vector<Glib::ustring>{boldTagName});
        drainEvents();
        ASSERT_TRUE(isBoldAtOffset(boldStart));
        buffer->select_range(buffer->get_iter_at_offset(boldStart),
                             buffer->get_iter_at_offset(boldStart + (int)boldWord.size()));
        drainEvents();
    }

    // Copy the bold selection (sets the CherryTree rich-text clipboard target).
    CtClipboard::on_copy_clipboard(pGtkTextView, &pair);
    drainEvents();

    // Paste at the very start of the buffer, where there is no pre-existing
    // formatting, so the bold tag at offset 0 is an unambiguous signal.
    buffer->place_cursor(buffer->begin());
    pBridge->endTextEditSession();
    drainEvents();

    const size_t boldWordsBefore = countBoldWord();
    const size_t baselineUndo = pBridge->getUndoStackDescriptions().size();

    CtClipboard::on_paste_clipboard(pGtkTextView, &pair);
    drainEvents();

    // Paste inserted a bold copy at offset 0 and pushed an undo command.
    ASSERT_EQ(boldWordsBefore + 1, countBoldWord());
    ASSERT_TRUE(isBoldAtOffset(0));
    ASSERT_GT(pBridge->getUndoStackDescriptions().size(), baselineUndo);

    // Undo removes the pasted copy.
    pBridge->undo();
    drainEvents();
    ASSERT_EQ(boldWordsBefore, countBoldWord());

    // Redo restores the paste — and the formatting must come back with it.
    pBridge->redo();
    drainEvents();
    ASSERT_EQ(boldWordsBefore + 1, countBoldWord());
    ASSERT_TRUE(isBoldAtOffset(0)) << "Pasted text lost its bold formatting after undo+redo";

    pWin->force_exit() = true;
    remove_window(*pWin);
}

TEST(CommandUndoRedoTests, PasteRichTextPreservesFormattingAfterRedo)
{
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);

    TestPasteRichFormatApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);
}
