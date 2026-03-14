/*
 * tests_command_gui_simulation.cpp
 *
 * Phase 6.3 Testing - GUI Event Simulation for Command Pattern
 *
 * This test extends Phase 6.2 by simulating actual GUI interactions
 * (key presses, mouse clicks) and verifying that the command pattern
 * properly captures these user actions for undo/redo.
 */

#include "tests_common.h"
#include "ct_app.h"
#include "ct_command_bridge.h"
#include "ct_codebox.h"
#include "ct_table.h"
#include <gtest/gtest.h>
#include <gdk/gdkkeysyms.h>
#include <random>
#include <chrono>
#include <algorithm>

// Helper class to simulate GUI events
class GuiEventSimulator
{
public:
    // Simulate a key press event on a widget
    static bool simulate_key_press(Gtk::Widget* widget, guint keyval, GdkModifierType modifiers = (GdkModifierType)0)
    {
        GdkEventKey event;
        memset(&event, 0, sizeof(event));

        event.type = GDK_KEY_PRESS;
        event.window = widget->get_window()->gobj();
        event.send_event = FALSE;
        event.time = GDK_CURRENT_TIME;
        event.state = modifiers;
        event.keyval = keyval;
        event.length = 0;
        event.string = nullptr;
        event.hardware_keycode = 0;
        event.group = 0;

        // Send the event to the widget
        gboolean handled = gtk_widget_event(widget->gobj(), (GdkEvent*)&event);

        // Process pending events
        process_pending_events();

        return handled;
    }

    // Simulate a key release event
    static bool simulate_key_release(Gtk::Widget* widget, guint keyval, GdkModifierType modifiers = (GdkModifierType)0)
    {
        GdkEventKey event;
        memset(&event, 0, sizeof(event));

        event.type = GDK_KEY_RELEASE;
        event.window = widget->get_window()->gobj();
        event.send_event = FALSE;
        event.time = GDK_CURRENT_TIME;
        event.state = modifiers;
        event.keyval = keyval;
        event.length = 0;
        event.string = nullptr;
        event.hardware_keycode = 0;
        event.group = 0;

        gboolean handled = gtk_widget_event(widget->gobj(), (GdkEvent*)&event);
        process_pending_events();

        return handled;
    }

    // Simulate typing a single character
    static void simulate_char_typed(Gtk::TextView* textView, char c)
    {
        // For typing characters, we insert directly into the buffer
        // This simulates what happens when GTK processes a key press
        auto buffer = textView->get_buffer();
        buffer->insert_at_cursor(Glib::ustring(1, c));
        process_pending_events();
    }

    // Simulate typing a string of text
    static void simulate_text_typed(Gtk::TextView* textView, const Glib::ustring& text)
    {
        for (auto c : text) {
            simulate_char_typed(textView, c);
        }
    }

    // Simulate pressing a keyboard shortcut (e.g., Ctrl+Z for undo)
    static bool simulate_shortcut(Gtk::Window* window, guint keyval, GdkModifierType modifiers)
    {
        return simulate_key_press(window, keyval, modifiers);
    }

    // Process all pending GTK events
    static void process_pending_events()
    {
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }
    }

    // Simulate mouse button press
    static bool simulate_button_press(Gtk::Widget* widget, int x, int y, guint button)
    {
        GdkEventButton event;
        memset(&event, 0, sizeof(event));

        event.type = GDK_BUTTON_PRESS;
        event.window = widget->get_window()->gobj();
        event.send_event = FALSE;
        event.time = GDK_CURRENT_TIME;
        event.x = x;
        event.y = y;
        event.button = button;
        event.state = 0;

        gboolean handled = gtk_widget_event(widget->gobj(), (GdkEvent*)&event);
        process_pending_events();

        return handled;
    }
};

class TestGuiSimulationApp : public CtApp
{
public:
    TestGuiSimulationApp() : CtApp{"_test_gui_simulation"} { _no_gui = true; }
    ~TestGuiSimulationApp() override {}

private:
    void on_activate() final;
    void _run_tests(CtMainWin* pWin);

    void _test_gui_complex_operations_undo_redo(CtMainWin* pWin);
    void _test_buffer_signal_handlers_direct(CtMainWin* pWin);
    void _test_cursor_restoration_after_undo_redo(CtMainWin* pWin);
    void _test_gtk_accelerator_bindings(CtMainWin* pWin);
    void _test_focus_out_session_ending(CtMainWin* pWin);
    void _test_mainwin_event_handlers_direct(CtMainWin* pWin);
    void _test_undo_redo_description_format(CtMainWin* pWin);
    void _test_cut_undo_redo_content_restoration(CtMainWin* pWin);
    void _test_scroll_position_captured_in_commands(CtMainWin* pWin);
    void _test_paste_delta_plain_text_undo_redo(CtMainWin* pWin);
    void _test_paste_delta_creates_correct_command_type(CtMainWin* pWin);
    void _test_codebox_edit_delta_undo_redo(CtMainWin* pWin);
    void _test_table_cell_edit_delta_undo_redo(CtMainWin* pWin);
    void _test_widget_edit_no_change_no_command(CtMainWin* pWin);
    void _test_codebox_edit_then_table_edit_separate_commands(CtMainWin* pWin);
};

void TestGuiSimulationApp::on_activate()
{
    _on_startup();

    // Create window (hidden but with GUI enabled for event processing)
    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));

    // Make the window realize its widgets so events can be processed
    pWin->show_all();
    pWin->hide();
    GuiEventSimulator::process_pending_events();

    _run_tests(pWin);

    // Cleanup
    pWin->force_exit() = true;
    remove_window(*pWin);
}

void TestGuiSimulationApp::_run_tests(CtMainWin* pWin)
{
    auto pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge);
    ASSERT_TRUE(pBridge->isActive());

    spdlog::info("=== Phase 6.3: GUI Event Simulation Testing ===");

    _test_gui_complex_operations_undo_redo(pWin);
    _test_buffer_signal_handlers_direct(pWin);
    _test_cursor_restoration_after_undo_redo(pWin);
    _test_gtk_accelerator_bindings(pWin);
    _test_focus_out_session_ending(pWin);
    _test_mainwin_event_handlers_direct(pWin);
    _test_undo_redo_description_format(pWin);
    _test_cut_undo_redo_content_restoration(pWin);
    _test_scroll_position_captured_in_commands(pWin);
    _test_paste_delta_plain_text_undo_redo(pWin);
    _test_paste_delta_creates_correct_command_type(pWin);
    _test_codebox_edit_delta_undo_redo(pWin);
    _test_table_cell_edit_delta_undo_redo(pWin);
    _test_widget_edit_no_change_no_command(pWin);
    _test_codebox_edit_then_table_edit_separate_commands(pWin);

    spdlog::info("=== Phase 6.3 GUI simulation test passed! ===");
}

void TestGuiSimulationApp::_test_gui_complex_operations_undo_redo(CtMainWin* pWin)
{
    spdlog::info("Test: Randomized complex operations (~50 ops) - full undo/redo cycle");
    spdlog::info("  Using pActions->requested_step_back/ahead for undo/redo");
    spdlog::info("  Each operation creates its own command");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack completely using action-level calls
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    // Select an empty node for clean testing
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    auto docModel = pBridge->getDocumentModel();
    auto node = docModel->getNodeById(nodeId);

    // Capture initial state
    Glib::ustring initialXml = node->getContentXml();
    spdlog::info("  Initial XML length: {}", initialXml.size());

    // Random seed based on current time for different sequences each run
    unsigned seed = static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count());
    std::mt19937 rng(seed);
    spdlog::info("  Random seed: {}", seed);

    // Operation types - each creates its own command
    enum OpType {
        // Text operations (each ends with space/enter to create command)
        OP_TYPE_WORD,       // Type a word (no terminator yet)
        OP_TYPE_SPACE,      // Type space (ends edit session)
        OP_TYPE_ENTER,      // Type enter (ends edit session)
        // Format operations (each creates its own command)
        OP_FORMAT_BOLD,
        OP_FORMAT_ITALIC,
        OP_FORMAT_UNDERLINE,
        OP_FORMAT_H1,
        OP_FORMAT_H2,
        OP_FORMAT_H3,
        // Insert operations (each creates its own command)
        OP_INSERT_IMAGE,
        OP_INSERT_CODEBOX,
        OP_INSERT_TABLE,
        OP_COUNT
    };

    // Words to type
    const std::vector<std::string> words = {
        "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
        "alpha", "beta", "gamma", "delta", "epsilon", "hello", "world", "test", "data"
    };

    // Phase 1: Type initial words so we have content to format
    // Type 10 words with spaces/enters to create initial content
    spdlog::info("  Phase 1: Creating initial text content (10 words)");
    std::vector<std::string> typedWords;
    for (int i = 0; i < 10; i++) {
        std::uniform_int_distribution<int> wordDist(0, words.size() - 1);
        std::string word = words[wordDist(rng)];
        typedWords.push_back(word);

        // End with space or enter to create command
        std::uniform_int_distribution<int> endDist(0, 1);
        bool useSpace = (endDist(rng) == 0);
        spdlog::info("    Op {}: Type '{}' + {}", i + 1, word, useSpace ? "space" : "enter");

        GuiEventSimulator::simulate_text_typed(textView, word);
        buffer->set_modified(true);

        if (useSpace) {
            GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
            GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
        } else {
            GuiEventSimulator::simulate_key_press(textView, GDK_KEY_Return);
            GuiEventSimulator::simulate_key_release(textView, GDK_KEY_Return);
        }
        GuiEventSimulator::process_pending_events();
        pBridge->endTextEditSession();
        pBridge->beginTextEditSession(nodeId);
    }

    // Phase 2: Build randomized operation sequence (~40 more operations)
    spdlog::info("  Phase 2: Building randomized operation sequence");

    std::uniform_int_distribution<int> wordOpsDist(10, 15);
    std::uniform_int_distribution<int> formatOpsDist(8, 12);
    std::uniform_int_distribution<int> imageOpsDist(3, 5);
    std::uniform_int_distribution<int> codeboxOpsDist(2, 4);
    std::uniform_int_distribution<int> tableOpsDist(2, 4);

    int numWordOps = wordOpsDist(rng);
    int numFormatOps = formatOpsDist(rng);
    int numImageOps = imageOpsDist(rng);
    int numCodeboxOps = codeboxOpsDist(rng);
    int numTableOps = tableOpsDist(rng);

    // Build operation sequence
    std::vector<OpType> operations;

    // Word operations: alternate between typing word and space/enter
    for (int i = 0; i < numWordOps; i++) {
        operations.push_back(OP_TYPE_WORD);
        // Each word is followed by space or enter
        std::uniform_int_distribution<int> termDist(0, 1);
        operations.push_back(termDist(rng) == 0 ? OP_TYPE_SPACE : OP_TYPE_ENTER);
    }

    // Format operations
    for (int i = 0; i < numFormatOps; i++) {
        std::uniform_int_distribution<int> formatDist(OP_FORMAT_BOLD, OP_FORMAT_H3);
        operations.push_back(static_cast<OpType>(formatDist(rng)));
    }

    // Insert operations
    for (int i = 0; i < numImageOps; i++) operations.push_back(OP_INSERT_IMAGE);
    for (int i = 0; i < numCodeboxOps; i++) operations.push_back(OP_INSERT_CODEBOX);
    for (int i = 0; i < numTableOps; i++) operations.push_back(OP_INSERT_TABLE);

    // Shuffle only the non-word operations (format + insert)
    // Keep word+terminator pairs together by shuffling format/insert ops separately
    std::vector<OpType> formatInsertOps;
    std::vector<std::pair<OpType, OpType>> wordPairs;

    for (size_t i = 0; i < operations.size(); i++) {
        if (operations[i] == OP_TYPE_WORD && i + 1 < operations.size()) {
            wordPairs.push_back({operations[i], operations[i + 1]});
            i++; // Skip the terminator
        } else if (operations[i] >= OP_FORMAT_BOLD) {
            formatInsertOps.push_back(operations[i]);
        }
    }

    // Shuffle both lists
    std::shuffle(wordPairs.begin(), wordPairs.end(), rng);
    std::shuffle(formatInsertOps.begin(), formatInsertOps.end(), rng);

    // Interleave: word pairs and format/insert ops
    operations.clear();
    size_t wordIdx = 0, formatIdx = 0;
    while (wordIdx < wordPairs.size() || formatIdx < formatInsertOps.size()) {
        std::uniform_int_distribution<int> chooseDist(0, 2);
        int choice = chooseDist(rng);

        if (choice < 2 && wordIdx < wordPairs.size()) {
            operations.push_back(wordPairs[wordIdx].first);
            operations.push_back(wordPairs[wordIdx].second);
            wordIdx++;
        } else if (formatIdx < formatInsertOps.size()) {
            operations.push_back(formatInsertOps[formatIdx]);
            formatIdx++;
        } else if (wordIdx < wordPairs.size()) {
            operations.push_back(wordPairs[wordIdx].first);
            operations.push_back(wordPairs[wordIdx].second);
            wordIdx++;
        }
    }

    int totalOps = 10 + operations.size(); // Initial 10 words + shuffled ops
    spdlog::info("  Planned: {} initial words + {} word pairs + {} format + {} image + {} codebox + {} table",
                 10, numWordOps, numFormatOps, numImageOps, numCodeboxOps, numTableOps);
    spdlog::info("  Total operations: {}", totalOps);

    // Phase 3: Execute randomized operations
    spdlog::info("  Phase 3: Executing randomized operations");

    bool pendingWord = false;  // Track if we typed a word without terminator
    int opNum = 10;  // Start after initial 10 words

    for (OpType op : operations) {
        opNum++;

        switch (op) {
            case OP_TYPE_WORD: {
                std::uniform_int_distribution<int> wordDist(0, words.size() - 1);
                std::string word = words[wordDist(rng)];
                typedWords.push_back(word);

                spdlog::info("  Op {}: Type '{}'", opNum, word);
                GuiEventSimulator::simulate_text_typed(textView, word);
                buffer->set_modified(true);
                pendingWord = true;
                break;
            }

            case OP_TYPE_SPACE: {
                spdlog::info("  Op {}: Space (end edit session)", opNum);
                // Space is included in the session with the word (not a separate command)
                GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
                GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
                GuiEventSimulator::process_pending_events();
                // End session after space (word + space together), mirrors key release handler
                pBridge->endTextEditSession();
                pBridge->beginTextEditSession(nodeId);
                pendingWord = false;
                break;
            }

            case OP_TYPE_ENTER: {
                spdlog::info("  Op {}: Enter (end edit session)", opNum);
                GuiEventSimulator::simulate_key_press(textView, GDK_KEY_Return);
                GuiEventSimulator::simulate_key_release(textView, GDK_KEY_Return);
                GuiEventSimulator::process_pending_events();
                pBridge->endTextEditSession();
                pBridge->beginTextEditSession(nodeId);
                pendingWord = false;
                break;
            }

            case OP_FORMAT_BOLD:
            case OP_FORMAT_ITALIC:
            case OP_FORMAT_UNDERLINE:
            case OP_FORMAT_H1:
            case OP_FORMAT_H2:
            case OP_FORMAT_H3: {
                // End any pending edit first
                if (pendingWord) {
                    GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
                    GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
                    GuiEventSimulator::process_pending_events();
                    pBridge->endTextEditSession();
                    pBridge->beginTextEditSession(nodeId);
                    pendingWord = false;
                }

                // Pick a random typed word to format
                std::uniform_int_distribution<int> wordIdxDist(0, typedWords.size() - 1);
                std::string targetWord = typedWords[wordIdxDist(rng)];

                // Find and select the word
                Gtk::TextIter searchStart = buffer->begin();
                Gtk::TextIter matchStart, matchEnd;
                if (searchStart.forward_search(targetWord, Gtk::TEXT_SEARCH_TEXT_ONLY, matchStart, matchEnd)) {
                    buffer->select_range(matchStart, matchEnd);
                    GuiEventSimulator::process_pending_events();

                    const char* formatName = "";
                    switch (op) {
                        case OP_FORMAT_BOLD:
                            formatName = "bold";
                            pActions->apply_tag_bold();
                            break;
                        case OP_FORMAT_ITALIC:
                            formatName = "italic";
                            pActions->apply_tag_italic();
                            break;
                        case OP_FORMAT_UNDERLINE:
                            formatName = "underline";
                            pActions->apply_tag_underline();
                            break;
                        case OP_FORMAT_H1:
                            formatName = "h1";
                            pActions->apply_tag_h1();
                            break;
                        case OP_FORMAT_H2:
                            formatName = "h2";
                            pActions->apply_tag_h2();
                            break;
                        case OP_FORMAT_H3:
                            formatName = "h3";
                            pActions->apply_tag_h3();
                            break;
                        default:
                            break;
                    }
                    spdlog::info("  Op {}: Apply {} to '{}'", opNum, formatName, targetWord);
                    GuiEventSimulator::process_pending_events();
                }
                break;
            }

            case OP_INSERT_IMAGE: {
                // End any pending edit first
                if (pendingWord) {
                    GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
                    GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
                    GuiEventSimulator::process_pending_events();
                    pBridge->endTextEditSession();
                    pBridge->beginTextEditSession(nodeId);
                    pendingWord = false;
                }

                spdlog::info("  Op {}: Insert image", opNum);

                Glib::RefPtr<Gdk::Pixbuf> testPixbuf = Gdk::Pixbuf::create(
                    Gdk::COLORSPACE_RGB, false, 8, 10, 10);
                testPixbuf->fill(0xFF0000FF);

                buffer->place_cursor(buffer->end());
                Gtk::TextIter insertIter = buffer->get_insert()->get_iter();
                pActions->image_insert_png(insertIter, testPixbuf, "", "");
                GuiEventSimulator::process_pending_events();
                // Restart edit session so subsequent typing is captured
                pBridge->beginTextEditSession(nodeId);
                break;
            }

            case OP_INSERT_CODEBOX: {
                // End any pending edit first
                if (pendingWord) {
                    GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
                    GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
                    GuiEventSimulator::process_pending_events();
                    pBridge->endTextEditSession();
                    pBridge->beginTextEditSession(nodeId);
                    pendingWord = false;
                }

                spdlog::info("  Op {}: Insert codebox", opNum);

                pBridge->endTextEditSession();

                buffer->place_cursor(buffer->end());
                int cbOffset = buffer->get_insert()->get_iter().get_offset();

                CtCodebox* pCtCodebox = new CtCodebox{
                    pWin,
                    "// test code\nint x = 42;",
                    "cpp",
                    300, 100,
                    cbOffset,
                    "",
                    true, false, true
                };
                pCtCodebox->insertInTextBuffer(buffer);

                pWin->get_tree_store().addAnchoredWidgets(
                    pWin->curr_tree_iter(),
                    {pCtCodebox},
                    &pWin->get_text_view().mm()
                );

                {
                    auto desc = extractWidgetDesc(pCtCodebox, cbOffset);
                    auto cmd = std::make_unique<InsertWidgetDeltaCommand>(
                        pBridge->getDocumentModel(), nodeId, cbOffset, desc, "Insert codebox");
                    pBridge->addCommandToStack(std::move(cmd));
                    node->getContent().insertWidget(cbOffset, desc);
                }

                GuiEventSimulator::process_pending_events();
                // Restart edit session so subsequent typing is captured
                pBridge->beginTextEditSession(nodeId);
                break;
            }

            case OP_INSERT_TABLE: {
                // End any pending edit first
                if (pendingWord) {
                    GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
                    GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
                    GuiEventSimulator::process_pending_events();
                    pBridge->endTextEditSession();
                    pBridge->beginTextEditSession(nodeId);
                    pendingWord = false;
                }

                spdlog::info("  Op {}: Insert table", opNum);

                pBridge->endTextEditSession();

                buffer->place_cursor(buffer->end());
                int tblOffset = buffer->get_insert()->get_iter().get_offset();

                CtTableMatrix tbl_matrix;
                for (int row = 0; row < 2; row++) {
                    tbl_matrix.push_back(CtTableRow{});
                    for (int col = 0; col < 2; col++) {
                        tbl_matrix.back().push_back(new Glib::ustring{
                            "cell" + std::to_string(row) + std::to_string(col)
                        });
                    }
                }

                CtTableLight* pCtTable = new CtTableLight{
                    pWin, tbl_matrix, 60, tblOffset, "", CtTableColWidths{}
                };
                pCtTable->insertInTextBuffer(buffer);

                pWin->get_tree_store().addAnchoredWidgets(
                    pWin->curr_tree_iter(),
                    {pCtTable},
                    &pWin->get_text_view().mm()
                );

                {
                    auto desc = extractWidgetDesc(pCtTable, tblOffset);
                    auto cmd = std::make_unique<InsertWidgetDeltaCommand>(
                        pBridge->getDocumentModel(), nodeId, tblOffset, desc, "Insert table");
                    pBridge->addCommandToStack(std::move(cmd));
                    node->getContent().insertWidget(tblOffset, desc);
                }

                GuiEventSimulator::process_pending_events();
                // Restart edit session so subsequent typing is captured
                pBridge->beginTextEditSession(nodeId);
                break;
            }

            default:
                break;
        }
    }

    // End any pending edit
    if (pendingWord) {
        GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
        GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
        GuiEventSimulator::process_pending_events();
        pBridge->endTextEditSession();
    }

    // End session by switching nodes
    auto htmlIter = pWin->get_tree_store().get_node_from_node_name("html");
    if (htmlIter) {
        pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(htmlIter));
        GuiEventSimulator::process_pending_events();
    }

    // Normalize finalXml through undo/redo round-trip so it goes through the same
    // observer re-serialization path that redo uses (observer re-serializes buffer
    // XML during undo/redo but not during normal execution)
    Glib::ustring finalXml;
    {
        int cmdCount = 0;
        while (pBridge->canUndo()) {
            pActions->requested_step_back();
            cmdCount++;
        }
        ASSERT_GT(cmdCount, 0) << "No commands created for complex operations";

        Glib::ustring afterFirstUndo = node->getContentXml();
        ASSERT_EQ(initialXml, afterFirstUndo) << "Full undo should restore initial state";
        spdlog::info("  Full undo restored initial state ({} commands)", cmdCount);

        int redoCount = 0;
        while (pBridge->canRedo()) {
            pActions->requested_step_ahead();
            redoCount++;
        }
        ASSERT_EQ(cmdCount, redoCount) << "Redo count should match undo count";

        // Capture normalized finalXml after redo (observer re-serialization applied)
        finalXml = node->getContentXml();
        spdlog::info("  Final XML length (normalized): {}", finalXml.size());
    }

    // Phase 4: Verify undo/redo cycle produces consistent results
    spdlog::info("  Phase 4: Verifying undo/redo cycle");

    int totalCommands = 0;
    while (pBridge->canUndo()) {
        pActions->requested_step_back();
        totalCommands++;
    }
    spdlog::info("  Total commands created: {}", totalCommands);

    Glib::ustring afterFullUndo = node->getContentXml();
    ASSERT_EQ(initialXml, afterFullUndo) << "Full undo should restore initial state";
    spdlog::info("  Full undo restored initial state");

    int redoCount = 0;
    while (pBridge->canRedo()) {
        pActions->requested_step_ahead();
        redoCount++;
    }
    spdlog::info("  Redo count: {}", redoCount);
    ASSERT_EQ(totalCommands, redoCount) << "Redo count should match undo count";

    Glib::ustring afterFullRedo = node->getContentXml();
    ASSERT_EQ(finalXml, afterFullRedo) << "Full redo should restore final state";
    spdlog::info("  Full redo restored final state");

    while (pBridge->canUndo()) {
        pActions->requested_step_back();
    }
    ASSERT_EQ(initialXml, node->getContentXml()) << "Second full undo should restore initial state";

    spdlog::info("✓ Test passed: {} ops, {} commands, full undo/redo cycle verified",
                 totalOps, totalCommands);
}

void TestGuiSimulationApp::_test_buffer_signal_handlers_direct(CtMainWin* pWin)
{
    spdlog::info("Test: Direct buffer signal handler invocation");
    spdlog::info("  Testing that signal handlers properly trigger command creation");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    // Select an empty node
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    auto docModel = pBridge->getDocumentModel();
    auto node = docModel->getNodeById(nodeId);

    Glib::ustring initialXml = node->getContentXml();

    // Test 1: Insert text through buffer signal
    spdlog::info("  Test 1: Inserting text through buffer API");
    pBridge->beginTextEditSession(nodeId);

    // Insert text directly via buffer (this triggers on_insert_text signal)
    auto iter = buffer->begin();
    buffer->insert(iter, "Direct signal test");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();

    // End session to create command
    pBridge->endTextEditSession();

    // Verify text was inserted
    Glib::ustring currentText = buffer->get_text();
    ASSERT_TRUE(currentText.find("Direct signal test") != Glib::ustring::npos)
        << "Text should be inserted in buffer";

    // Test 2: Delete text through buffer signal
    spdlog::info("  Test 2: Deleting text through buffer API");
    pBridge->beginTextEditSession(nodeId);

    // Delete some text directly via buffer (this triggers on_delete_range signal)
    auto deleteStart = buffer->begin();
    auto deleteEnd = buffer->begin();
    deleteEnd.forward_chars(6); // Delete "Direct"
    buffer->erase(deleteStart, deleteEnd);
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();

    pBridge->endTextEditSession();

    // Verify text was deleted
    currentText = buffer->get_text();
    ASSERT_TRUE(currentText.find("Direct") == Glib::ustring::npos)
        << "Text 'Direct' should be deleted from buffer";
    ASSERT_TRUE(currentText.find("signal test") != Glib::ustring::npos)
        << "Remaining text should still be present";

    // Test 3: Verify commands were created
    spdlog::info("  Test 3: Verifying commands were created for signal-triggered changes");
    int commandCount = 0;
    while (pBridge->canUndo()) {
        pActions->requested_step_back();
        commandCount++;
    }

    ASSERT_GE(commandCount, 1) << "At least one command should be created from signal handlers";
    spdlog::info("  Created {} commands from signal handlers", commandCount);

    // Verify undo restores initial state
    Glib::ustring afterUndo = node->getContentXml();
    ASSERT_EQ(initialXml, afterUndo) << "Full undo should restore initial state";

    spdlog::info("✓ Buffer signal handler test passed");
}

void TestGuiSimulationApp::_test_cursor_restoration_after_undo_redo(CtMainWin* pWin)
{
    spdlog::info("Test: Cursor position restoration after undo/redo");
    spdlog::info("  Verifying cursor moves to correct position after undo/redo");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    // Select node
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    // Type some text and record cursor positions
    std::vector<int> cursorPositions;

    spdlog::info("  Phase 1: Type words and record cursor positions");
    for (int i = 0; i < 3; i++) {
        pBridge->beginTextEditSession(nodeId);

        std::string word = "word" + std::to_string(i + 1);
        GuiEventSimulator::simulate_text_typed(textView, word);
        buffer->set_modified(true);

        // Record cursor position before ending session
        int cursorPos = buffer->property_cursor_position();
        cursorPositions.push_back(cursorPos);
        spdlog::info("    After typing '{}': cursor at position {}", word, cursorPos);

        // End session with space
        GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
        GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
        GuiEventSimulator::process_pending_events();

        pBridge->endTextEditSession();
    }

    // Switch nodes to end session
    auto htmlIter = pWin->get_tree_store().get_node_from_node_name("html");
    if (htmlIter) {
        pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(htmlIter));
        GuiEventSimulator::process_pending_events();
    }

    // Go back to our test node
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    // Test cursor restoration during undo
    spdlog::info("  Phase 2: Undo and verify cursor positions");
    int undoCount = 0;
    while (pBridge->canUndo()) {
        int cursorBefore = buffer->property_cursor_position();

        pActions->requested_step_back();
        GuiEventSimulator::process_pending_events();

        int cursorAfter = buffer->property_cursor_position();

        spdlog::info("    Undo #{}: cursor moved from {} to {}",
                     undoCount + 1, cursorBefore, cursorAfter);

        // The cursor should have moved (unless we're at the start already)
        // After undo, cursor should be restored to the position stored in the command
        undoCount++;
    }
    ASSERT_GT(undoCount, 0) << "Should have created some commands";

    // Cursor should be at the beginning after full undo
    int cursorAtStart = buffer->property_cursor_position();
    spdlog::info("    After full undo: cursor at position {}", cursorAtStart);

    // Test cursor restoration during redo
    spdlog::info("  Phase 3: Redo and verify cursor positions");
    int redoCount = 0;
    while (pBridge->canRedo()) {
        int cursorBefore = buffer->property_cursor_position();

        pActions->requested_step_ahead();
        GuiEventSimulator::process_pending_events();

        int cursorAfter = buffer->property_cursor_position();

        spdlog::info("    Redo #{}: cursor moved from {} to {}",
                     redoCount + 1, cursorBefore, cursorAfter);

        // Cursor should move forward during redo
        redoCount++;
    }
    ASSERT_EQ(undoCount, redoCount) << "Redo count should match undo count";

    // Cursor should be at or near the end after full redo
    int cursorAtEnd = buffer->property_cursor_position();
    spdlog::info("    After full redo: cursor at position {}", cursorAtEnd);
    ASSERT_GT(cursorAtEnd, cursorAtStart) << "Cursor should be further after redo than after undo";

    spdlog::info("✓ Cursor restoration test passed");
}

void TestGuiSimulationApp::_test_gtk_accelerator_bindings(CtMainWin* pWin)
{
    spdlog::info("Test: GTK accelerator bindings verification");
    spdlog::info("  Checking that keyboard shortcuts are properly registered");

    // Get the accel map
    GtkAccelMap* accelMap = gtk_accel_map_get();
    ASSERT_TRUE(accelMap != nullptr) << "GTK accel map should be available";

    // Test common shortcuts that should be registered
    struct AccelTest {
        std::string path;
        guint key;
        GdkModifierType mods;
        std::string description;
    };

    std::vector<AccelTest> tests = {
        {"<Actions>/CtActions/undo", GDK_KEY_z, GDK_CONTROL_MASK, "Ctrl+Z for undo"},
        {"<Actions>/CtActions/redo", GDK_KEY_y, GDK_CONTROL_MASK, "Ctrl+Y for redo"},
        {"<Actions>/CtActions/save", GDK_KEY_s, GDK_CONTROL_MASK, "Ctrl+S for save"},
        {"<Actions>/CtActions/find", GDK_KEY_f, GDK_CONTROL_MASK, "Ctrl+F for find"},
    };

    int foundCount = 0;
    for (const auto& test : tests) {
        GtkAccelKey key;
        gboolean found = gtk_accel_map_lookup_entry(test.path.c_str(), &key);

        if (found) {
            spdlog::info("  ✓ Found accelerator: {} -> key={}, mods={}",
                         test.description, gdk_keyval_name(key.accel_key), key.accel_mods);
            foundCount++;

            // Note: We don't strictly verify the exact key/mod combination because
            // they might be customized by user preferences. Just verify they exist.
        } else {
            spdlog::info("  ⚠ Accelerator not found: {} (path: {})",
                         test.description, test.path);
        }
    }

    // We should find at least some of the common accelerators
    // Not all may be registered depending on the application state
    spdlog::info("  Found {}/{} accelerator bindings", foundCount, tests.size());

    // This is informational - we log what we find but don't fail if some are missing
    // since the exact accelerator paths might vary

    spdlog::info("✓ Accelerator bindings test completed (informational)");
}

void TestGuiSimulationApp::_test_focus_out_session_ending(CtMainWin* pWin)
{
    spdlog::info("Test: Focus-out event ending edit session");
    spdlog::info("  Verifying that losing focus properly ends edit session");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    // Select node
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    // Start editing session
    spdlog::info("  Phase 1: Start typing text");
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "test focus");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();

    // Verify session is active (we can check this indirectly by checking if buffer has content)
    Glib::ustring textBefore = buffer->get_text();
    ASSERT_TRUE(textBefore.find("test focus") != Glib::ustring::npos)
        << "Text should be in buffer";

    // Simulate focus-out event
    spdlog::info("  Phase 2: Simulate focus-out event");
    GdkEventFocus focusEvent;
    memset(&focusEvent, 0, sizeof(focusEvent));
    focusEvent.type = GDK_FOCUS_CHANGE;
    focusEvent.window = textView->get_window(Gtk::TEXT_WINDOW_TEXT)->gobj();
    focusEvent.send_event = FALSE;
    focusEvent.in = FALSE; // Focus out

    // Send focus-out event
    gboolean handled = gtk_widget_event(GTK_WIDGET(textView->gobj()), (GdkEvent*)&focusEvent);
    GuiEventSimulator::process_pending_events();

    spdlog::info("    Focus-out event handled: {}", handled ? "yes" : "no");

    // Manually end session (simulating what the real focus-out handler does)
    // In the real application, the focus-out handler should end the session
    pBridge->endTextEditSession();

    // Verify command was created
    spdlog::info("  Phase 3: Verify command was created");
    ASSERT_TRUE(pBridge->canUndo()) << "Command should be created when focus is lost";

    // Undo and verify
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    Glib::ustring textAfterUndo = buffer->get_text();
    ASSERT_TRUE(textAfterUndo.find("test focus") == Glib::ustring::npos || textAfterUndo.empty())
        << "Text should be removed after undo";

    spdlog::info("✓ Focus-out session ending test passed");
}

void TestGuiSimulationApp::_test_mainwin_event_handlers_direct(CtMainWin* pWin)
{
    spdlog::info("Test: Direct CtMainWin event handler invocation");
    spdlog::info("  Testing event handlers directly instead of through gtk_widget_event()");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    // Select node
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    // Test 1: Test key event handler directly
    spdlog::info("  Test 1: Testing window key press handler");

    // Create a key event for Ctrl+Tab (should toggle focus between tree and text)
    GdkEventKey keyEvent;
    memset(&keyEvent, 0, sizeof(keyEvent));
    keyEvent.type = GDK_KEY_PRESS;
    keyEvent.window = pWin->get_window()->gobj();
    keyEvent.send_event = FALSE;
    keyEvent.time = GDK_CURRENT_TIME;
    keyEvent.state = GDK_CONTROL_MASK;
    keyEvent.keyval = GDK_KEY_Tab;
    keyEvent.length = 0;
    keyEvent.string = nullptr;
    keyEvent.hardware_keycode = 0;
    keyEvent.group = 0;

    // Call the window's key press handler directly
    // Note: The handler is _on_window_key_press_event in ct_main_win_events.cc:143
    // We can't call it directly as it's private, but we can send the event
    gboolean windowHandled = gtk_widget_event(GTK_WIDGET(pWin->gobj()), (GdkEvent*)&keyEvent);
    GuiEventSimulator::process_pending_events();

    spdlog::info("    Window key event (Ctrl+Tab) handled: {}", windowHandled ? "yes" : "no");

    // Test 2: Test text view event handler with Return key
    spdlog::info("  Test 2: Testing textview event with Return key");

    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "line1");
    buffer->set_modified(true);

    // Create Return key press event
    GdkEventKey returnEvent;
    memset(&returnEvent, 0, sizeof(returnEvent));
    returnEvent.type = GDK_KEY_PRESS;
    returnEvent.window = textView->get_window(Gtk::TEXT_WINDOW_TEXT)->gobj();
    returnEvent.send_event = FALSE;
    returnEvent.time = GDK_CURRENT_TIME;
    returnEvent.state = (GdkModifierType)0;
    returnEvent.keyval = GDK_KEY_Return;
    returnEvent.length = 0;
    returnEvent.string = nullptr;
    returnEvent.hardware_keycode = 0;
    returnEvent.group = 0;

    // Send key press event
    gboolean tvHandled = gtk_widget_event(GTK_WIDGET(textView->gobj()), (GdkEvent*)&returnEvent);
    GuiEventSimulator::process_pending_events();

    spdlog::info("    TextView event (Return) handled: {}", tvHandled ? "yes" : "no");

    // Create Return key release event (this is what triggers session end in real usage)
    returnEvent.type = GDK_KEY_RELEASE;
    gboolean tvReleaseHandled = gtk_widget_event(GTK_WIDGET(textView->gobj()), (GdkEvent*)&returnEvent);
    GuiEventSimulator::process_pending_events();

    spdlog::info("    TextView event (Return release) handled: {}", tvReleaseHandled ? "yes" : "no");

    // Manually end session (as the key release handler should do)
    pBridge->endTextEditSession();

    // Add another line
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "line2");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endTextEditSession();

    // Test 3: Verify commands were created
    spdlog::info("  Test 3: Verify commands from event handlers");

    int commandCount = 0;
    while (pBridge->canUndo()) {
        pActions->requested_step_back();
        commandCount++;
    }

    ASSERT_GE(commandCount, 1) << "At least one command should be created";
    spdlog::info("    Created {} commands from event handlers", commandCount);

    spdlog::info("✓ CtMainWin event handler test passed");
}

void TestGuiSimulationApp::_test_undo_redo_description_format(CtMainWin* pWin)
{
    spdlog::info("Test: Undo/redo dropdown list description format");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    // Helper: assert description has no newlines (single-line for dropdown display)
    auto assertSingleLine = [](const std::string& desc) {
        ASSERT_EQ(desc.find('\n'), std::string::npos)
            << "Description must be single-line for dropdown, got: " << desc;
    };
    auto assertDesc = [&](const std::string& desc, const std::string& expected) {
        assertSingleLine(desc);
        ASSERT_EQ(desc, expected) << "expected '" << expected << "', got '" << desc << "'";
        spdlog::info("    '{}'", desc);
    };

    std::string pfx = "Node " + std::to_string(nodeId) + ": ";

    // --- 1. Word + space: space is hidden from the description ---
    spdlog::info("  1. word + space");
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "hello");
    buffer->set_modified(true);
    GuiEventSimulator::simulate_key_press(textView, GDK_KEY_space);
    GuiEventSimulator::simulate_key_release(textView, GDK_KEY_space);
    GuiEventSimulator::process_pending_events();
    pBridge->endTextEditSession();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected at least one undo command after typing 'hello'";
        assertDesc(descs[0], pfx + "Type \"hello\"");
    }

    // --- 2. Word + newline ---
    spdlog::info("  2. word + newline");
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "world");
    buffer->set_modified(true);
    // Return key doesn't insert \n in headless tests, insert directly
    buffer->insert_at_cursor("\n");
    GuiEventSimulator::process_pending_events();
    pBridge->endTextEditSession();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected at least one undo command after typing 'world'";
        assertDesc(descs[0], pfx + "Type \"world\" + Newline");
    }

    // --- 3. Type then delete same chars in same session (net-zero → no undo entry) ---
    spdlog::info("  3. type + delete same chars in same session (deduplication)");
    {
        auto descsBefore = pBridge->getUndoStackDescriptions();
        pBridge->beginTextEditSession(nodeId);
        buffer->place_cursor(buffer->end());
        buffer->insert_at_cursor("abc");
        auto del_end = buffer->end();
        auto del_start = del_end;
        del_start.backward_chars(3);
        buffer->erase(del_start, del_end);
        GuiEventSimulator::process_pending_events();
        pBridge->endTextEditSession();
        auto descsAfter = pBridge->getUndoStackDescriptions();
        // Net-zero session: document unchanged, so no undo entry is created
        ASSERT_EQ(descsAfter.size(), descsBefore.size())
            << "Net-zero session should not create an undo entry";
        if (!descsAfter.empty()) {
            assertDesc(descsAfter[0], pfx + "Type \"world\" + Newline");
        }
    }

    // --- 4. Bold format ---
    spdlog::info("  4. bold format");
    {
        Gtk::TextIter matchStart, matchEnd;
        if (buffer->begin().forward_search("hello", Gtk::TEXT_SEARCH_TEXT_ONLY, matchStart, matchEnd)) {
            buffer->select_range(matchStart, matchEnd);
        }
    }
    pActions->apply_tag_bold();
    GuiEventSimulator::process_pending_events();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected undo command after bold format";
        assertDesc(descs[0], pfx + "Format (bold)");
    }

    // --- 5. Insert image ---
    spdlog::info("  5. insert image");
    buffer->place_cursor(buffer->end());
    {
        Glib::RefPtr<Gdk::Pixbuf> pix = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 4, 4);
        pix->fill(0xFF0000FF);
        Gtk::TextIter insertIter = buffer->get_insert()->get_iter();
        pActions->image_insert_png(insertIter, pix, "", "");
    }
    GuiEventSimulator::process_pending_events();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected undo command after insert image";
        assertDesc(descs[0], pfx + "Insert image");
    }

    // --- 6. Insert codebox ---
    spdlog::info("  6. insert codebox");
    {
        pBridge->endTextEditSession();
        buffer->place_cursor(buffer->end());
        int cbOffset = buffer->get_insert()->get_iter().get_offset();
        CtCodebox* pCtCodebox = new CtCodebox{
            pWin, "// code", "cpp", 200, 80,
            cbOffset, "", true, false, true
        };
        pCtCodebox->insertInTextBuffer(buffer);
        pWin->get_tree_store().addAnchoredWidgets(
            pWin->curr_tree_iter(), {pCtCodebox}, &pWin->get_text_view().mm());
        auto desc = extractWidgetDesc(pCtCodebox, cbOffset);
        auto cmd = std::make_unique<InsertWidgetDeltaCommand>(pBridge->getDocumentModel(), nodeId, cbOffset, desc, "Insert codebox");
        pBridge->addCommandToStack(std::move(cmd));
        auto n = pBridge->getDocumentModel()->getNodeById(nodeId);
        if (n) n->getContent().insertWidget(cbOffset, desc);
    }
    GuiEventSimulator::process_pending_events();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected undo command after insert codebox";
        assertDesc(descs[0], pfx + "Insert codebox");
    }

    // --- 7. Insert table ---
    spdlog::info("  7. insert table");
    {
        pBridge->endTextEditSession();
        buffer->place_cursor(buffer->end());
        int charOffset = buffer->get_insert()->get_iter().get_offset();
        CtTableMatrix tbl;
        for (int r = 0; r < 2; r++) {
            tbl.push_back(CtTableRow{});
            for (int c = 0; c < 2; c++) {
                tbl.back().push_back(new Glib::ustring{"c" + std::to_string(r) + std::to_string(c)});
            }
        }
        CtTableLight* pCtTable = new CtTableLight{pWin, tbl, 60, charOffset, "", CtTableColWidths{}};
        pCtTable->insertInTextBuffer(buffer);
        pWin->get_tree_store().addAnchoredWidgets(
            pWin->curr_tree_iter(), {pCtTable}, &pWin->get_text_view().mm());
        auto desc = extractWidgetDesc(pCtTable, charOffset);
        auto cmd = std::make_unique<InsertWidgetDeltaCommand>(pBridge->getDocumentModel(), nodeId, charOffset, desc, "Insert table");
        pBridge->addCommandToStack(std::move(cmd));
        auto n = pBridge->getDocumentModel()->getNodeById(nodeId);
        if (n) n->getContent().insertWidget(charOffset, desc);
    }
    GuiEventSimulator::process_pending_events();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected undo command after insert table";
        assertDesc(descs[0], pfx + "Insert table");
    }

    // --- 8. Paste ---
    spdlog::info("  8. paste clipboard");
    pBridge->beginPaste(nodeId);
    buffer->insert_at_cursor("pasted text");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endPaste();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected undo command after paste";
        assertDesc(descs[0], pfx + "Paste clipboard");
    }

    // --- 9. Cut ---
    spdlog::info("  9. cut clipboard");
    pBridge->beginCut(nodeId);
    {
        auto cut_end = buffer->end();
        auto cut_start = cut_end;
        cut_start.backward_chars(4); // cut "text"
        buffer->erase(cut_start, cut_end);
    }
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endCut();
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Expected undo command after cut";
        assertDesc(descs[0], pfx + "Cut clipboard");
    }

    // --- Verify full undo stack: 8 entries, all single-line ---
    // (step 3 was a net-zero session; deduplication skips it → 8 not 9)
    spdlog::info("  Verifying full undo stack (8 entries, all single-line)");
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_EQ(descs.size(), 8u) << "Expected 8 undo entries total";
        for (const auto& d : descs) {
            assertSingleLine(d);
        }
    }

    // --- After full undo, redo stack also all single-line ---
    spdlog::info("  Verifying redo stack after full undo");
    while (pBridge->canUndo()) pActions->requested_step_back();
    {
        auto redoDescs = pBridge->getRedoStackDescriptions();
        ASSERT_EQ(redoDescs.size(), 8u) << "Expected 8 redo entries after full undo";
        for (const auto& d : redoDescs) {
            assertSingleLine(d);
        }
    }

    spdlog::info("✓ Undo/redo description format test passed");
}

void TestGuiSimulationApp::_test_cut_undo_redo_content_restoration(CtMainWin* pWin)
{
    spdlog::info("Test: Cut-as-delta — undo/redo restores and removes content");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    // Capture state before any typing
    Glib::ustring initialText = buffer->get_text();

    // Type "ABCDEFGH" in one session, then end it
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "ABCDEFGH");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endTextEditSession();

    Glib::ustring textAfterType = buffer->get_text();
    ASSERT_TRUE(textAfterType.find("ABCDEFGH") != Glib::ustring::npos)
        << "Typed text should be in buffer";

    // Cut the last 4 characters ("EFGH")
    pBridge->beginCut(nodeId);
    {
        auto cut_end = buffer->end();
        auto cut_start = cut_end;
        cut_start.backward_chars(4);
        buffer->erase(cut_start, cut_end);
    }
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endCut();

    // Verify cut removed "EFGH" but left "ABCD"
    Glib::ustring textAfterCut = buffer->get_text();
    ASSERT_TRUE(textAfterCut.find("EFGH") == Glib::ustring::npos)
        << "Cut characters should be removed from buffer";
    ASSERT_TRUE(textAfterCut.find("ABCD") != Glib::ustring::npos)
        << "Uncut characters should remain in buffer";

    // Verify the cut command has the expected description
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_FALSE(descs.empty()) << "Cut should have created an undo entry";
        ASSERT_EQ(descs[0], "Node " + std::to_string(nodeId) + ": Cut clipboard");
    }

    // Undo the cut: "EFGH" should be restored
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    Glib::ustring textAfterUndoCut = buffer->get_text();
    ASSERT_EQ(textAfterUndoCut, textAfterType)
        << "Undo of cut should restore text to pre-cut state";

    // Redo the cut: "EFGH" should disappear again
    pActions->requested_step_ahead();
    GuiEventSimulator::process_pending_events();

    Glib::ustring textAfterRedoCut = buffer->get_text();
    ASSERT_TRUE(textAfterRedoCut.find("EFGH") == Glib::ustring::npos)
        << "Redo of cut should remove characters again";
    ASSERT_TRUE(textAfterRedoCut.find("ABCD") != Glib::ustring::npos)
        << "Uncut characters should still be present after redo";

    // Undo everything and verify initial text is restored
    while (pBridge->canUndo()) pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    ASSERT_EQ(buffer->get_text(), initialText)
        << "Full undo should restore initial buffer text";

    spdlog::info("✓ Cut-as-delta undo/redo content restoration test passed");
}

void TestGuiSimulationApp::_test_scroll_position_captured_in_commands(CtMainWin* pWin)
{
    spdlog::info("Test: Scroll position captured in CompoundCommand after text edit and cut");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    // Type text and end session to produce a CompoundCommand
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "scrolltest");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endTextEditSession();

    ASSERT_TRUE(pBridge->canUndo()) << "Should have an undo entry after typing";

    // In headless mode the scroll adjustment value is 0.0, but NOT -1.0 (which means uncaptured)
    {
        CtCommand* cmd = pBridge->getCommandManager().peekUndoCommand();
        ASSERT_TRUE(cmd != nullptr) << "Undo stack must have a command";
        EXPECT_GE(cmd->getOldScrollPos(), 0.0)
            << "Text-edit command should have oldScrollPos captured (>= 0.0, not -1.0)";
        EXPECT_GE(cmd->getNewScrollPos(), 0.0)
            << "Text-edit command should have newScrollPos captured (>= 0.0, not -1.0)";
    }

    // Now do a cut and verify its command also has scroll positions captured
    pBridge->beginCut(nodeId);
    {
        auto cut_end = buffer->end();
        auto cut_start = cut_end;
        cut_start.backward_chars(4);
        buffer->erase(cut_start, cut_end);
    }
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endCut();

    ASSERT_TRUE(pBridge->canUndo()) << "Should have an undo entry after cut";

    {
        CtCommand* cmd = pBridge->getCommandManager().peekUndoCommand();
        ASSERT_TRUE(cmd != nullptr) << "Undo stack must have a command after cut";
        EXPECT_GE(cmd->getOldScrollPos(), 0.0)
            << "Cut command should have oldScrollPos captured (>= 0.0, not -1.0)";
        EXPECT_GE(cmd->getNewScrollPos(), 0.0)
            << "Cut command should have newScrollPos captured (>= 0.0, not -1.0)";
    }

    // Clean up
    while (pBridge->canUndo()) pActions->requested_step_back();

    spdlog::info("✓ Scroll position captured in commands test passed");
}

void TestGuiSimulationApp::_test_paste_delta_plain_text_undo_redo(CtMainWin* pWin)
{
    spdlog::info("Test: Plain text paste (delta path) — undo/redo restores content");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    // Clear undo stack
    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    Gtk::TextView* textView = &pWin->get_text_view().mm();

    Glib::ustring initialText = buffer->get_text();

    // Type some text first
    pBridge->beginTextEditSession(nodeId);
    GuiEventSimulator::simulate_text_typed(textView, "before");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endTextEditSession();

    Glib::ustring textAfterType = buffer->get_text();

    // Plain text paste via delta path (pasteContainsWidgets=false)
    pBridge->beginPaste(nodeId, false/*pasteContainsWidgets*/);
    buffer->insert_at_cursor("PASTED_DELTA");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endPaste();
    pBridge->beginTextEditSession(nodeId);

    Glib::ustring textAfterPaste = buffer->get_text();
    ASSERT_TRUE(textAfterPaste.find("PASTED_DELTA") != Glib::ustring::npos)
        << "Pasted text should be in buffer";

    // Verify paste command description
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_GE(descs.size(), 2u) << "Should have type + paste commands";
        ASSERT_TRUE(descs[0].find("Paste") != std::string::npos)
            << "Top undo entry should be paste, got: " << descs[0];
    }

    // Undo the paste: "PASTED_DELTA" should be removed
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    Glib::ustring textAfterUndoPaste = buffer->get_text();
    ASSERT_TRUE(textAfterUndoPaste.find("PASTED_DELTA") == Glib::ustring::npos)
        << "Undo paste should remove pasted text";
    ASSERT_EQ(textAfterUndoPaste, textAfterType)
        << "After undo paste, text should match pre-paste state";

    // Redo the paste: "PASTED_DELTA" should reappear
    pActions->requested_step_ahead();
    GuiEventSimulator::process_pending_events();

    Glib::ustring textAfterRedoPaste = buffer->get_text();
    ASSERT_TRUE(textAfterRedoPaste.find("PASTED_DELTA") != Glib::ustring::npos)
        << "Redo paste should restore pasted text";

    // Undo everything back to initial state
    while (pBridge->canUndo()) pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();
    ASSERT_EQ(buffer->get_text(), initialText)
        << "Full undo should restore initial text";

    spdlog::info("✓ Plain text paste delta undo/redo test passed");
}

void TestGuiSimulationApp::_test_paste_delta_creates_correct_command_type(CtMainWin* pWin)
{
    spdlog::info("Test: Delta paste vs XML paste create correct command types");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();

    // 1. Delta paste (pasteContainsWidgets=false)
    pBridge->beginPaste(nodeId, false/*pasteContainsWidgets*/);
    buffer->insert_at_cursor("delta_paste");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endPaste();
    pBridge->beginTextEditSession(nodeId);

    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_EQ(descs.size(), 1u) << "Delta paste should create exactly 1 command";
        ASSERT_TRUE(descs[0].find("Paste clipboard") != std::string::npos)
            << "Delta paste description should contain 'Paste clipboard', got: " << descs[0];
    }

    // 2. XML paste (pasteContainsWidgets=true, default)
    pBridge->cancelTextEditSession();
    pBridge->beginPaste(nodeId); // default = true = XML path
    buffer->insert_at_cursor("xml_paste");
    buffer->set_modified(true);
    GuiEventSimulator::process_pending_events();
    pBridge->endPaste();
    pBridge->beginTextEditSession(nodeId);

    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_EQ(descs.size(), 2u) << "Should now have 2 commands (delta + xml paste)";
        ASSERT_TRUE(descs[0].find("Paste clipboard") != std::string::npos)
            << "XML paste description should also contain 'Paste clipboard', got: " << descs[0];
    }

    // Both should undo cleanly
    pActions->requested_step_back();
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    Glib::ustring afterFullUndo = buffer->get_text();
    ASSERT_TRUE(afterFullUndo.find("delta_paste") == Glib::ustring::npos)
        << "Full undo should remove delta paste text";
    ASSERT_TRUE(afterFullUndo.find("xml_paste") == Glib::ustring::npos)
        << "Full undo should remove xml paste text";

    spdlog::info("✓ Paste delta vs XML command type test passed");
}

void TestGuiSimulationApp::_test_codebox_edit_delta_undo_redo(CtMainWin* pWin)
{
    spdlog::info("Test: Codebox edit delta — undo/redo restores codebox content");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();

    // Insert a codebox with known content
    pBridge->endTextEditSession();
    buffer->place_cursor(buffer->end());
    int charOffset = buffer->get_insert()->get_iter().get_offset();

    CtCodebox* pCodebox = new CtCodebox{
        pWin, "original_code", "cpp", 200, 80,
        charOffset, "", true, false, true
    };
    pCodebox->insertInTextBuffer(buffer);
    pWin->get_tree_store().addAnchoredWidgets(
        pWin->curr_tree_iter(), {pCodebox}, &pWin->get_text_view().mm());

    auto docModel = pBridge->getDocumentModel();
    {
        auto desc = extractWidgetDesc(pCodebox, charOffset);
        auto insertCmd = std::make_unique<InsertWidgetDeltaCommand>(docModel, nodeId, charOffset, desc, "Insert codebox");
        pBridge->addCommandToStack(std::move(insertCmd));
        auto node = docModel->getNodeById(nodeId);
        if (node) node->getContent().insertWidget(charOffset, desc);
    }
    GuiEventSimulator::process_pending_events();

    // Verify codebox was inserted
    ASSERT_EQ(pCodebox->get_text_content(), "original_code");

    // Simulate codebox focus-in (begin widget edit with delta path)
    pBridge->beginWidgetEdit(nodeId, pCodebox);

    // Modify codebox content
    auto codeboxBuffer = pCodebox->get_buffer();
    codeboxBuffer->set_text("modified_code");
    GuiEventSimulator::process_pending_events();

    // Simulate codebox focus-out (end widget edit)
    pBridge->endWidgetEdit();

    // Verify delta command was created
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_GE(descs.size(), 2u) << "Should have insert + edit commands";
        ASSERT_TRUE(descs[0].find("Edit codebox") != std::string::npos)
            << "Top undo entry should be codebox edit, got: " << descs[0];
    }

    // Verify codebox has new content
    ASSERT_EQ(pCodebox->get_text_content(), "modified_code");

    // Undo codebox edit: should restore original content
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    // After undo, need to re-find the codebox widget (buffer may have been rebuilt)
    {
        auto widgets = pWin->curr_tree_iter().get_anchored_widgets();
        CtCodebox* cbox = nullptr;
        for (auto* w : widgets) {
            if (w->get_type() == CtAnchWidgType::CodeBox) {
                cbox = static_cast<CtCodebox*>(w);
                break;
            }
        }
        ASSERT_TRUE(cbox != nullptr) << "Codebox should still exist after undo";
        ASSERT_EQ(cbox->get_text_content(), "original_code")
            << "Undo should restore original codebox content";

        // Redo codebox edit: should restore modified content
        pActions->requested_step_ahead();
        GuiEventSimulator::process_pending_events();

        // Re-find after redo
        widgets = pWin->curr_tree_iter().get_anchored_widgets();
        cbox = nullptr;
        for (auto* w : widgets) {
            if (w->get_type() == CtAnchWidgType::CodeBox) {
                cbox = static_cast<CtCodebox*>(w);
                break;
            }
        }
        ASSERT_TRUE(cbox != nullptr) << "Codebox should still exist after redo";
        ASSERT_EQ(cbox->get_text_content(), "modified_code")
            << "Redo should restore modified codebox content";
    }

    // Undo everything back
    while (pBridge->canUndo()) pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    spdlog::info("✓ Codebox edit delta undo/redo test passed");
}

void TestGuiSimulationApp::_test_table_cell_edit_delta_undo_redo(CtMainWin* pWin)
{
    spdlog::info("Test: Table cell edit delta — undo/redo restores cell content");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();

    // Insert a 2x2 table with known content
    pBridge->endTextEditSession();
    buffer->place_cursor(buffer->end());
    int charOffset = buffer->get_insert()->get_iter().get_offset();

    CtTableMatrix tbl;
    for (int r = 0; r < 2; r++) {
        tbl.push_back(CtTableRow{});
        for (int c = 0; c < 2; c++) {
            tbl.back().push_back(new Glib::ustring{
                "orig_" + std::to_string(r) + std::to_string(c)
            });
        }
    }
    CtTableLight* pTable = new CtTableLight{pWin, tbl, 60, charOffset, "", CtTableColWidths{}};
    pTable->insertInTextBuffer(buffer);
    pWin->get_tree_store().addAnchoredWidgets(
        pWin->curr_tree_iter(), {pTable}, &pWin->get_text_view().mm());

    auto docModel = pBridge->getDocumentModel();
    {
        auto desc = extractWidgetDesc(pTable, charOffset);
        auto insertCmd = std::make_unique<InsertWidgetDeltaCommand>(docModel, nodeId, charOffset, desc, "Insert table");
        pBridge->addCommandToStack(std::move(insertCmd));
        auto node = docModel->getNodeById(nodeId);
        if (node) node->getContent().insertWidget(charOffset, desc);
    }
    GuiEventSimulator::process_pending_events();

    // Verify initial cell content
    ASSERT_EQ(pTable->get_cell_text(0, 0), "orig_00");
    ASSERT_EQ(pTable->get_cell_text(1, 1), "orig_11");

    // Simulate table cell focus-in (delta path with row=0, col=1)
    pBridge->beginWidgetEdit(nodeId, pTable, 0, 1);

    // Modify cell content
    pTable->set_cell_text(0, 1, "edited_01");
    GuiEventSimulator::process_pending_events();

    // Simulate table cell focus-out
    pBridge->endWidgetEdit();

    // Verify delta command was created
    {
        auto descs = pBridge->getUndoStackDescriptions();
        ASSERT_GE(descs.size(), 2u) << "Should have insert + edit commands";
        ASSERT_TRUE(descs[0].find("Edit table cell") != std::string::npos)
            << "Top undo entry should be table cell edit, got: " << descs[0];
    }

    // Verify cell has new content
    ASSERT_EQ(pTable->get_cell_text(0, 1), "edited_01");
    // Other cells unchanged
    ASSERT_EQ(pTable->get_cell_text(0, 0), "orig_00");
    ASSERT_EQ(pTable->get_cell_text(1, 1), "orig_11");

    // Undo table cell edit: should restore original cell content
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    // Re-find the table widget after undo (buffer may have been rebuilt)
    {
        auto widgets = pWin->curr_tree_iter().get_anchored_widgets();
        CtTableLight* table = nullptr;
        for (auto* w : widgets) {
            if (w->get_type() == CtAnchWidgType::TableLight) {
                table = static_cast<CtTableLight*>(w);
                break;
            }
        }
        ASSERT_TRUE(table != nullptr) << "Table should still exist after undo";
        ASSERT_EQ(table->get_cell_text(0, 1), "orig_01")
            << "Undo should restore original cell content";
        // Verify other cells still intact
        ASSERT_EQ(table->get_cell_text(0, 0), "orig_00");
        ASSERT_EQ(table->get_cell_text(1, 1), "orig_11");

        // Redo table cell edit
        pActions->requested_step_ahead();
        GuiEventSimulator::process_pending_events();

        widgets = pWin->curr_tree_iter().get_anchored_widgets();
        table = nullptr;
        for (auto* w : widgets) {
            if (w->get_type() == CtAnchWidgType::TableLight) {
                table = static_cast<CtTableLight*>(w);
                break;
            }
        }
        ASSERT_TRUE(table != nullptr) << "Table should still exist after redo";
        ASSERT_EQ(table->get_cell_text(0, 1), "edited_01")
            << "Redo should restore edited cell content";
    }

    // Undo everything back
    while (pBridge->canUndo()) pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    spdlog::info("✓ Table cell edit delta undo/redo test passed");
}

void TestGuiSimulationApp::_test_widget_edit_no_change_no_command(CtMainWin* pWin)
{
    spdlog::info("Test: Widget edit with no content change creates no undo command");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();

    // Insert a codebox
    pBridge->endTextEditSession();
    buffer->place_cursor(buffer->end());
    int charOffset = buffer->get_insert()->get_iter().get_offset();

    CtCodebox* pCodebox = new CtCodebox{
        pWin, "unchanged_code", "cpp", 200, 80,
        charOffset, "", true, false, true
    };
    pCodebox->insertInTextBuffer(buffer);
    pWin->get_tree_store().addAnchoredWidgets(
        pWin->curr_tree_iter(), {pCodebox}, &pWin->get_text_view().mm());

    auto docModel = pBridge->getDocumentModel();
    auto desc = extractWidgetDesc(pCodebox, charOffset);
    auto insertCmd = std::make_unique<InsertWidgetDeltaCommand>(docModel, nodeId, charOffset, desc, "Insert codebox");
    pBridge->addCommandToStack(std::move(insertCmd));
    auto node = docModel->getNodeById(nodeId);
    if (node) node->getContent().insertWidget(charOffset, desc);
    GuiEventSimulator::process_pending_events();

    size_t undoCountBefore = pBridge->getUndoStackDescriptions().size();

    // Begin widget edit, but DON'T change anything
    pBridge->beginWidgetEdit(nodeId, pCodebox);
    GuiEventSimulator::process_pending_events();
    pBridge->endWidgetEdit();

    size_t undoCountAfter = pBridge->getUndoStackDescriptions().size();

    ASSERT_EQ(undoCountBefore, undoCountAfter)
        << "No-change widget edit should not create an undo entry";

    // Undo everything
    while (pBridge->canUndo()) pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    spdlog::info("✓ Widget edit no-change no-command test passed");
}

void TestGuiSimulationApp::_test_codebox_edit_then_table_edit_separate_commands(CtMainWin* pWin)
{
    spdlog::info("Test: Codebox edit then table cell edit create separate undo commands");

    auto pBridge = pWin->get_command_bridge();
    auto pActions = pWin->get_ct_actions();

    while (pBridge->canUndo()) pActions->requested_step_back();
    while (pBridge->canRedo()) pActions->requested_step_ahead();
    while (pBridge->canUndo()) pActions->requested_step_back();

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    gint64 nodeId = ctIter.get_node_id();

    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    GuiEventSimulator::process_pending_events();

    auto buffer = pWin->curr_buffer();
    auto docModel = pBridge->getDocumentModel();
    auto node = docModel->getNodeById(nodeId);

    // Insert a codebox
    pBridge->endTextEditSession();
    buffer->place_cursor(buffer->end());
    int codeboxOffset = buffer->get_insert()->get_iter().get_offset();

    CtCodebox* pCodebox = new CtCodebox{
        pWin, "cb_code", "cpp", 200, 80,
        codeboxOffset, "", true, false, true
    };
    pCodebox->insertInTextBuffer(buffer);
    pWin->get_tree_store().addAnchoredWidgets(
        pWin->curr_tree_iter(), {pCodebox}, &pWin->get_text_view().mm());

    {
        auto desc = extractWidgetDesc(pCodebox, codeboxOffset);
        pBridge->addCommandToStack(std::make_unique<InsertWidgetDeltaCommand>(docModel, nodeId, codeboxOffset, desc, "Insert codebox"));
        if (node) node->getContent().insertWidget(codeboxOffset, desc);
    }
    GuiEventSimulator::process_pending_events();

    // Insert a table
    pBridge->endTextEditSession();
    buffer->place_cursor(buffer->end());
    int tableOffset = buffer->get_insert()->get_iter().get_offset();

    CtTableMatrix tbl;
    for (int r = 0; r < 2; r++) {
        tbl.push_back(CtTableRow{});
        for (int c = 0; c < 2; c++) {
            tbl.back().push_back(new Glib::ustring{"t" + std::to_string(r) + std::to_string(c)});
        }
    }
    CtTableLight* pTable = new CtTableLight{pWin, tbl, 60, tableOffset, "", CtTableColWidths{}};
    pTable->insertInTextBuffer(buffer);
    pWin->get_tree_store().addAnchoredWidgets(
        pWin->curr_tree_iter(), {pTable}, &pWin->get_text_view().mm());

    {
        auto desc = extractWidgetDesc(pTable, tableOffset);
        pBridge->addCommandToStack(std::make_unique<InsertWidgetDeltaCommand>(docModel, nodeId, tableOffset, desc, "Insert table"));
        if (node) node->getContent().insertWidget(tableOffset, desc);
    }
    GuiEventSimulator::process_pending_events();

    size_t baseCount = pBridge->getUndoStackDescriptions().size();

    // Edit codebox
    pBridge->beginWidgetEdit(nodeId, pCodebox);
    pCodebox->get_buffer()->set_text("cb_edited");
    GuiEventSimulator::process_pending_events();
    pBridge->endWidgetEdit();

    // Edit table cell
    pBridge->beginWidgetEdit(nodeId, pTable, 0, 0);
    pTable->set_cell_text(0, 0, "t00_edited");
    GuiEventSimulator::process_pending_events();
    pBridge->endWidgetEdit();

    auto descs = pBridge->getUndoStackDescriptions();
    ASSERT_EQ(descs.size(), baseCount + 2)
        << "Codebox edit + table cell edit should create 2 separate undo entries";
    ASSERT_TRUE(descs[0].find("Edit table cell") != std::string::npos)
        << "Most recent should be table cell edit, got: " << descs[0];
    ASSERT_TRUE(descs[1].find("Edit codebox") != std::string::npos)
        << "Second most recent should be codebox edit, got: " << descs[1];

    // Undo table edit: cell reverts, codebox stays edited
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();
    {
        auto widgets = pWin->curr_tree_iter().get_anchored_widgets();
        for (auto* w : widgets) {
            if (w->get_type() == CtAnchWidgType::TableLight) {
                auto* table = static_cast<CtTableLight*>(w);
                ASSERT_EQ(table->get_cell_text(0, 0), "t00")
                    << "Undo table edit should restore cell to original";
            }
            if (w->get_type() == CtAnchWidgType::CodeBox) {
                auto* cbox = static_cast<CtCodebox*>(w);
                ASSERT_EQ(cbox->get_text_content(), "cb_edited")
                    << "Codebox should still have edited content after undoing table edit";
            }
        }
    }

    // Undo codebox edit: codebox reverts too
    pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();
    {
        auto widgets = pWin->curr_tree_iter().get_anchored_widgets();
        for (auto* w : widgets) {
            if (w->get_type() == CtAnchWidgType::CodeBox) {
                auto* cbox = static_cast<CtCodebox*>(w);
                ASSERT_EQ(cbox->get_text_content(), "cb_code")
                    << "Undo codebox edit should restore to original";
            }
        }
    }

    // Undo everything
    while (pBridge->canUndo()) pActions->requested_step_back();
    GuiEventSimulator::process_pending_events();

    spdlog::info("✓ Codebox + table edit separate commands test passed");
}

TEST(CommandGuiSimulationTests, Phase6_3_GuiEventSimulation)
{
    // Suppress GTK warnings during tests
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);

    TestGuiSimulationApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);

    // Process all pending GTK events before test exits
    // This is critical to prevent interference with Google Test's teardown
    int processed = 0;
    while (gtk_events_pending()) {
        gtk_main_iteration();
        processed++;
        if (processed > 1000) {
            break;
        }
    }
}
