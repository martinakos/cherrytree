/*
 * tests_timeline_gui.cpp
 *
 * Integration tests for the timeline panel: entry creation, offset
 * adjustment, grouping, flash, and persistence.
 * Cross-comparison tests: every undo/redo operation produces a matching
 * timeline entry, and clicking that entry triggers a bounding box flash.
 */

#include "tests_common.h"
#include "ct_app.h"
#include "ct_command_bridge.h"
#include "ct_timeline_panel.h"
#include "ct_text_commands.h"
#include "ct_widget_commands.h"
#include "ct_actions.h"
#include "ct_codebox.h"
#include "ct_table.h"
#include <gtest/gtest.h>
#include <set>

class TestTimelineApp : public CtApp
{
public:
    TestTimelineApp() : CtApp{"_test_timeline", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
    void _run_tests(CtMainWin* pWin);
};

void TestTimelineApp::on_activate()
{
    _on_startup();
    auto _quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));

    pWin->show_all();
    pWin->hide();
    while (gtk_events_pending()) gtk_main_iteration_do(false);

    _run_tests(pWin);

    pWin->force_exit() = true;
    remove_window(*pWin);
}

void TestTimelineApp::_run_tests(CtMainWin* pWin)
{
    auto drainEvents = [](){
        while (gtk_events_pending()) gtk_main_iteration_do(false);
    };

    auto* pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge);
    ASSERT_TRUE(pBridge->isActive());

    auto* panel = pWin->get_timeline_panel();
    ASSERT_TRUE(panel);

    // Enable the timeline for all tests (disabled by default)
    pWin->get_ct_config()->timelineEnabled = true;

    spdlog::info("=== Timeline Panel Tests ===");

    // Select a node first
    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    drainEvents();

    // --- TimelineEntry_AddAndRetrieve ---
    {
        spdlog::info("TimelineEntry_AddAndRetrieve");
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        ASSERT_TRUE(treeIter);
        gint64 nodeId = treeIter.get_node_id();

        CtTimelineEntry e;
        e.nodeId = nodeId;
        e.timestamp = 5000;
        e.actionDescription = "Type 'hello'";
        e.actionType = "INS";
        e.regionOffset = 10;
        e.regionLength = 5;
        e.cursorPos = 15;
        e.scrollPos = 0;
        e.useDay = 1;
        panel->add_entry(e);
        drainEvents();

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].nodeId, nodeId);
        EXPECT_EQ(entries[0].actionType, "INS");
        EXPECT_EQ(entries[0].actionDescription, "Type 'hello'");
        EXPECT_EQ(entries[0].regionOffset, 10);
        EXPECT_EQ(entries[0].regionLength, 5);

        panel->clear();
        drainEvents();
    }

    // --- Chronological_NewestOnTopNodesRepeated ---
    {
        spdlog::info("Chronological_NewestOnTopNodesRepeated");
        panel->clear();
        drainEvents();

        auto f_entry = [](gint64 nodeId, gint64 ts, const char* desc) {
            CtTimelineEntry e;
            e.nodeId = nodeId;
            e.timestamp = ts;
            e.actionDescription = desc;
            e.actionType = "INS";
            e.regionOffset = 0;
            e.regionLength = 1;
            e.useDay = 1;
            return e;
        };
        // node A, then node B, then node A again
        panel->add_entry(f_entry(12345, 1000, "A first"));
        panel->add_entry(f_entry(54321, 2000, "B"));
        panel->add_entry(f_entry(12345, 3000, "A second"));

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 3u);
        EXPECT_EQ(entries[0].actionDescription, "A second");
        EXPECT_EQ(entries[1].actionDescription, "B");
        EXPECT_EQ(entries[2].actionDescription, "A first");

        auto store = panel->getTreeView().get_model();
        EXPECT_EQ(store->children().size(), 3u) << "every entry is its own row";
        for (const auto& row : store->children()) {
            EXPECT_TRUE(row.children().empty()) << "entries must not be nested under a node";
        }

        panel->clear();
        drainEvents();
    }

    // --- Chronological_LoadSortsGroupedFile ---
    {
        spdlog::info("Chronological_LoadSortsGroupedFile");
        panel->clear();
        drainEvents();

        // timeline files written when entries were grouped by node
        std::vector<CtTimelineEntry> grouped(3);
        grouped[0].nodeId = 12345; grouped[0].timestamp = 3000; grouped[0].actionDescription = "A second";
        grouped[1].nodeId = 12345; grouped[1].timestamp = 1000; grouped[1].actionDescription = "A first";
        grouped[2].nodeId = 54321; grouped[2].timestamp = 2000; grouped[2].actionDescription = "B";
        for (auto& e : grouped) { e.actionType = "INS"; e.useDay = pWin->get_ct_config()->timelineUseDay; }
        panel->load_entries(grouped);

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 3u);
        EXPECT_EQ(entries[0].actionDescription, "A second");
        EXPECT_EQ(entries[1].actionDescription, "B");
        EXPECT_EQ(entries[2].actionDescription, "A first");

        panel->clear();
        drainEvents();
    }

    // --- OffsetAdjust_E2E_InsertShiftsOlder ---
    {
        spdlog::info("OffsetAdjust_E2E_InsertShiftsOlder");
        panel->clear();
        drainEvents();

        gint64 testNodeId = 54321;

        CtTimelineEntry e1;
        e1.nodeId = testNodeId;
        e1.timestamp = 1000;
        e1.actionDescription = "Older entry";
        e1.actionType = "INS";
        e1.regionOffset = 20;
        e1.regionLength = 5;
        e1.useDay = 1;
        panel->add_entry(e1);

        // Simulate an insert at offset 5 with length 10 shifting older entries
        std::vector<CtCommand::OffsetShift> shifts = {{5, 10}};
        panel->adjustOffsetsForNode(testNodeId, shifts);

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].regionOffset, 30)
            << "Insert at 5 (+10) should shift entry at 20 to 30";

        panel->clear();
        drainEvents();
    }

    // --- OffsetAdjust_E2E_DeleteShiftsOlder ---
    {
        spdlog::info("OffsetAdjust_E2E_DeleteShiftsOlder");
        panel->clear();
        drainEvents();

        gint64 testNodeId = 54322;

        CtTimelineEntry e1;
        e1.nodeId = testNodeId;
        e1.timestamp = 1000;
        e1.actionDescription = "Older entry";
        e1.actionType = "INS";
        e1.regionOffset = 20;
        e1.regionLength = 5;
        e1.useDay = 1;
        panel->add_entry(e1);

        std::vector<CtCommand::OffsetShift> shifts = {{0, -5}};
        panel->adjustOffsetsForNode(testNodeId, shifts);

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].regionOffset, 15)
            << "Delete at 0 (-5) should shift entry at 20 to 15";

        panel->clear();
        drainEvents();
    }

    // --- OffsetAdjust_E2E_ManyActions ---
    {
        spdlog::info("OffsetAdjust_E2E_ManyActions");
        panel->clear();
        drainEvents();

        gint64 testNodeId = 99999;

        for (int i = 0; i < 5; ++i) {
            CtTimelineEntry e;
            e.nodeId = testNodeId;
            e.timestamp = 1000 + i;
            e.actionDescription = "Action " + std::to_string(i);
            e.actionType = "INS";
            e.regionOffset = i * 10;
            e.regionLength = 3;
            e.useDay = 1;
            panel->add_entry(e);
        }

        auto entries = panel->get_all_entries();
        EXPECT_EQ(entries.size(), 5u) << "5 actions in same node should all be stored";

        panel->clear();
        drainEvents();
    }

    // --- LoadEntries_PruneByUseDay ---
    {
        spdlog::info("LoadEntries_PruneByUseDay");

        auto* config = pWin->get_ct_config();
        int savedUseDay = config->timelineUseDay;
        int savedMax = config->timelineMaxUseDays;

        config->timelineUseDay = 50;
        config->timelineMaxUseDays = 10;

        std::vector<CtTimelineEntry> entries;

        CtTimelineEntry old_entry;
        old_entry.nodeId = 1;
        old_entry.timestamp = 1000;
        old_entry.actionDescription = "Old action";
        old_entry.actionType = "INS";
        old_entry.regionOffset = 5;
        old_entry.useDay = 30; // 50 - 30 = 20 > maxUseDays(10) → pruned
        entries.push_back(old_entry);

        CtTimelineEntry recent_entry;
        recent_entry.nodeId = 2;
        recent_entry.timestamp = 2000;
        recent_entry.actionDescription = "Recent action";
        recent_entry.actionType = "DEL";
        recent_entry.regionOffset = 10;
        recent_entry.useDay = 45; // 50 - 45 = 5 <= maxUseDays(10) → kept
        entries.push_back(recent_entry);

        panel->load_entries(entries);
        drainEvents();

        auto loaded = panel->get_all_entries();
        EXPECT_EQ(loaded.size(), 1u) << "Only entries within maxUseDays should survive pruning";
        if (!loaded.empty()) {
            EXPECT_EQ(loaded[0].actionDescription, "Recent action");
        }

        config->timelineUseDay = savedUseDay;
        config->timelineMaxUseDays = savedMax;
        panel->clear();
        drainEvents();
    }

    // --- UpdateCursorScroll ---
    {
        spdlog::info("UpdateCursorScroll");
        panel->clear();
        drainEvents();

        gint64 testNodeId = 77777;

        CtTimelineEntry e1;
        e1.nodeId = testNodeId;
        e1.timestamp = 1000;
        e1.actionDescription = "Test";
        e1.actionType = "INS";
        e1.regionOffset = 5;
        e1.cursorPos = 10;
        e1.scrollPos = 100;
        e1.useDay = 1;
        panel->add_entry(e1);

        panel->updateCursorScroll(testNodeId, 42, 200);

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].cursorPos, 42);
        EXPECT_EQ(entries[0].scrollPos, 200);

        panel->clear();
        drainEvents();
    }

    // --- RemoveEntriesForNode ---
    {
        spdlog::info("RemoveEntriesForNode");
        panel->clear();
        drainEvents();

        CtTimelineEntry e1;
        e1.nodeId = 111;
        e1.timestamp = 1000;
        e1.actionDescription = "Keep";
        e1.actionType = "INS";
        e1.useDay = 1;
        panel->add_entry(e1);

        CtTimelineEntry e2;
        e2.nodeId = 222;
        e2.timestamp = 2000;
        e2.actionDescription = "Remove";
        e2.actionType = "DEL";
        e2.useDay = 1;
        panel->add_entry(e2);

        panel->remove_entries_for_node(222);
        drainEvents();

        auto entries = panel->get_all_entries();
        EXPECT_EQ(entries.size(), 1u);
        if (!entries.empty()) {
            EXPECT_EQ(entries[0].nodeId, 111);
        }

        panel->clear();
        drainEvents();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Cross-comparison tests: every undoable operation creates a matching
    // timeline entry with correct actionType/regionOffset, and clicking
    // the entry triggers a bounding box flash.
    // ═══════════════════════════════════════════════════════════════════════

    auto* pActions = pWin->get_ct_actions();

    // Helper: simulate clicking a timeline panel row and drain idle callbacks
    auto activateFirstEntry = [&](CtTimelinePanel* p) {
        auto& tv = p->getTreeView();
        auto store = tv.get_model();
        auto topIter = store->children().begin();
        if (!topIter) return;
        auto children = topIter->children();
        Gtk::TreePath path;
        if (children.empty()) {
            // Only one entry — data is on the parent row
            path = store->get_path(topIter);
        } else {
            path = store->get_path(children.begin());
        }
        tv.row_activated(path, *tv.get_column(0));
        drainEvents();
        drainEvents();
        drainEvents();
    };

    // --- InsertText creates timeline entry with INS and flashes ---
    {
        spdlog::info("CrossCompare: InsertText");
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        ASSERT_TRUE(treeIter);
        gint64 nodeId = treeIter.get_node_id();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);
        int initialUndoCount = pBridge->getUndoStackDescriptions().size();

        // Type text — this goes through the edit session
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("TEST");
        drainEvents();
        // End the edit session to commit
        pBridge->onCursorMoved();
        drainEvents();

        int newUndoCount = pBridge->getUndoStackDescriptions().size();
        EXPECT_GT(newUndoCount, initialUndoCount) << "Typing should create an undo entry";

        auto entries = panel->get_all_entries();
        ASSERT_GE(entries.size(), 1u) << "Typing should create a timeline entry";

        // Find entry for this node — latest should be INS
        bool foundINS = false;
        for (const auto& e : entries) {
            if (e.nodeId == nodeId && e.actionType == "INS") {
                foundINS = true;
                EXPECT_GE(e.regionOffset, 0) << "INS entry should have valid regionOffset";
                EXPECT_GT(e.regionLength, 0) << "INS entry should have regionLength > 0";
                break;
            }
        }
        EXPECT_TRUE(foundINS) << "Should find an INS entry for the typed text";

        // Test flash: activate the entry and verify flash is active
        activateFirstEntry(panel);
        EXPECT_TRUE(panel->isReplaying()) << "Clicking INS entry should trigger replay";
        panel->clear();

        // Undo the typing
        pActions->requested_step_back();
        drainEvents();
    }

    // --- DeleteText creates timeline entry with DEL and flashes ---
    {
        spdlog::info("CrossCompare: DeleteText");
        panel->clear();
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);
        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();

        // Insert text first, then delete
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("DELME");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        panel->clear();
        drainEvents();

        // Select and delete "DELME"
        auto iterStart = buffer->begin();
        auto iterEnd = buffer->get_iter_at_offset(5);
        buffer->erase(iterStart, iterEnd);
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool foundDEL = false;
        for (const auto& e : entries) {
            if (e.nodeId == nodeId && e.actionType == "DEL") {
                foundDEL = true;
                EXPECT_GE(e.regionOffset, 0) << "DEL entry should have valid regionOffset";
                break;
            }
        }
        EXPECT_TRUE(foundDEL) << "Deleting text should create a DEL entry";

        activateFirstEntry(panel);
        EXPECT_TRUE(panel->isReplaying()) << "Clicking DEL entry should trigger replay";
        panel->clear();

        // Undo both operations
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
    }

    // --- Format (bold) creates timeline entry with FMT and flashes ---
    {
        spdlog::info("CrossCompare: FormatBold");
        panel->clear();
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);
        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();

        // Need some text to format
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("BOLDME");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        panel->clear();
        drainEvents();

        // Select text and apply bold
        auto iterStart = buffer->begin();
        auto iterEnd = buffer->get_iter_at_offset(6);
        buffer->select_range(iterStart, iterEnd);
        drainEvents();

        pActions->apply_tag_bold();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool foundFMT = false;
        for (const auto& e : entries) {
            if (e.nodeId == nodeId && (e.actionType == "FMT" || e.actionType == "RFM")) {
                foundFMT = true;
                EXPECT_GE(e.regionOffset, 0) << "FMT entry should have valid regionOffset";
                EXPECT_GT(e.regionLength, 0) << "FMT entry should have regionLength > 0";
                break;
            }
        }
        EXPECT_TRUE(foundFMT) << "Bold formatting should create a FMT/RFM entry";

        activateFirstEntry(panel);
        EXPECT_TRUE(panel->isReplaying()) << "Clicking FMT entry should trigger replay";
        panel->clear();

        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
    }

    // --- Undo creates a timeline entry ---
    {
        spdlog::info("CrossCompare: UndoCreatesEntry");
        panel->clear();
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);

        // Create an undoable action
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("UNDOME");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        size_t entriesBeforeUndo = panel->get_all_entries().size();

        // Undo
        pActions->requested_step_back();
        drainEvents();

        auto entries = panel->get_all_entries();
        EXPECT_GT(entries.size(), entriesBeforeUndo)
            << "Undo should create a new timeline entry";

        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
    }

    // --- Redo creates a timeline entry ---
    {
        spdlog::info("CrossCompare: RedoCreatesEntry");
        panel->clear();
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);

        // Create an undoable action, then undo it
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("REDOME");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        pActions->requested_step_back();
        drainEvents();

        size_t entriesBeforeRedo = panel->get_all_entries().size();

        // Redo
        pActions->requested_step_ahead();
        drainEvents();

        auto entries = panel->get_all_entries();
        EXPECT_GT(entries.size(), entriesBeforeRedo)
            << "Redo should create a new timeline entry";

        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
    }

    // --- Undo stack count matches timeline entry count ---
    {
        spdlog::info("CrossCompare: UndoStackMatchesTimeline");
        panel->clear();
        drainEvents();

        // Clear undo stack by undoing everything
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        panel->clear();
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);

        // Perform 3 distinct operations
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("AAA");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        buffer->insert_at_cursor("BBB");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        buffer->insert_at_cursor("CCC");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        size_t undoCount = pBridge->getUndoStackDescriptions().size();
        size_t timelineCount = panel->get_all_entries().size();

        EXPECT_GE(timelineCount, undoCount)
            << "Timeline should have at least as many entries as the undo stack "
               "(undo/redo also creates entries)";

        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
    }

    // --- Every entry has a non-empty actionType ---
    {
        spdlog::info("CrossCompare: AllEntriesHaveActionType");
        panel->clear();
        drainEvents();

        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        panel->clear();
        drainEvents();

        auto buffer = pWin->curr_buffer();
        ASSERT_TRUE(buffer);

        // Type, delete, format — all through the bridge
        buffer->place_cursor(buffer->begin());
        buffer->insert_at_cursor("TYPED");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto iterS = buffer->begin();
        auto iterE = buffer->get_iter_at_offset(2);
        buffer->erase(iterS, iterE);
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto entries = panel->get_all_entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            EXPECT_FALSE(entries[i].actionType.empty())
                << "Entry " << i << " ('" << entries[i].actionDescription
                << "') has empty actionType";
        }

        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
    }

    // --- Model replay requires valid deltaData ---
    {
        spdlog::info("CrossCompare: ReplayRequiresDeltaData");
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();

        // Entries without deltaData should trigger fallback highlight (not model-based replay)
        std::vector<std::string> contentTypes = {
            "INS", "DEL", "FMT", "RFM", "TED", "WIns", "WMod", "TCel", "RCel", "CBed"
        };

        for (const auto& aType : contentTypes) {
            panel->clear();
            drainEvents();

            CtTimelineEntry e;
            e.nodeId = nodeId;
            e.timestamp = std::time(nullptr);
            e.actionDescription = "Test " + aType;
            e.actionType = aType;
            e.regionOffset = 0;
            e.regionLength = 1;
            e.useDay = 1;
            panel->add_entry(e);
            drainEvents();

            activateFirstEntry(panel);
            // Fallback highlight fires (timeout connected) but no model replay
            EXPECT_TRUE(panel->isReplaying())
                << "Fallback highlight should fire for " << aType << " without deltaData";
        }
        panel->clear();
        drainEvents();
    }

    // --- Node operations: NPr, NAd, NMv create entries (no crash on click) ---
    {
        spdlog::info("CrossCompare: NodeOpsCreateEntries");

        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();

        std::vector<std::string> nodeOpTypes = {"NPr", "NAd", "NMv"};

        for (const auto& aType : nodeOpTypes) {
            panel->clear();
            drainEvents();

            CtTimelineEntry e;
            e.nodeId = nodeId;
            e.timestamp = std::time(nullptr);
            e.actionDescription = "Test " + aType;
            e.actionType = aType;
            e.regionOffset = -1;
            e.regionLength = 0;
            e.useDay = 1;
            panel->add_entry(e);
            drainEvents();

            // Click should not crash and should trigger tree row flash
            activateFirstEntry(panel);
            EXPECT_TRUE(panel->isReplaying())
                << "Flash should fire for node actionType=" << aType;
        }
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_PopulatedForINS ---
    {
        spdlog::info("DeltaData_PopulatedForINS");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        drainEvents();

        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        rBuffer->place_cursor(rBuffer->end());
        rBuffer->insert_at_cursor("DELTATEST");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool foundINS = false;
        for (const auto& e : entries) {
            if (e.actionType == "INS" && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("INS|") == 0)
                    << "INS deltaData should start with 'INS|', got: " << e.deltaData.substr(0, 20);
                foundINS = true;
                break;
            }
        }
        EXPECT_TRUE(foundINS) << "Should find an INS entry with deltaData";

        // Undo the typing
        pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_PopulatedForDEL ---
    {
        spdlog::info("DeltaData_PopulatedForDEL");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        drainEvents();

        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        // First insert some text
        rBuffer->place_cursor(rBuffer->end());
        rBuffer->insert_at_cursor("DELME");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        // Now delete it
        int len = rBuffer->get_char_count();
        auto iterS = rBuffer->get_iter_at_offset(len - 5);
        auto iterE = rBuffer->end();
        rBuffer->place_cursor(iterS);
        rBuffer->erase(iterS, iterE);
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool foundDEL = false;
        for (const auto& e : entries) {
            if (e.actionType == "DEL" && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("DEL|") == 0)
                    << "DEL deltaData should start with 'DEL|'";
                foundDEL = true;
                break;
            }
        }
        EXPECT_TRUE(foundDEL) << "Should find a DEL entry with deltaData";

        // Undo the delete and insert
        pActions->requested_step_back();
        pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_LargeDeltas ---
    {
        spdlog::info("DeltaData_LargeDeltas");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        drainEvents();

        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        // Insert a very large string (>64KB) — delta should still be populated
        Glib::ustring bigText(70000, 'X');
        rBuffer->place_cursor(rBuffer->end());
        rBuffer->insert_at_cursor(bigText);
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto entries = panel->get_all_entries();
        for (const auto& e : entries) {
            if (e.actionType == "INS") {
                EXPECT_FALSE(e.deltaData.empty())
                    << "Large delta should still be serialized for replay";
            }
        }

        // Undo
        pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- EnableDisable_ControlsTimelineCreation ---
    {
        spdlog::info("EnableDisable_ControlsTimelineCreation");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = false;
        panel->clear();
        drainEvents();

        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        // Edit with the timeline disabled — no entries should be created via bridge
        rBuffer->place_cursor(rBuffer->end());
        rBuffer->insert_at_cursor("disabled");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        auto entries = panel->get_all_entries();
        EXPECT_EQ(entries.size(), 0u) << "No entries should be created when the timeline is disabled";

        // Undo
        pActions->requested_step_back();
        drainEvents();

        // Enable and edit — entries should appear
        config->timelineEnabled = true;
        rBuffer->place_cursor(rBuffer->end());
        rBuffer->insert_at_cursor("enabled");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        entries = panel->get_all_entries();
        EXPECT_GT(entries.size(), 0u) << "Entries should be created when the timeline is enabled";

        // Disable again — no new entries
        size_t countBefore = entries.size();
        config->timelineEnabled = false;
        rBuffer->place_cursor(rBuffer->end());
        rBuffer->insert_at_cursor("disabled2");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();

        entries = panel->get_all_entries();
        EXPECT_EQ(entries.size(), countBefore)
            << "No new entries should appear after disabling";

        // Undo both inserts
        config->timelineEnabled = true;
        pActions->requested_step_back();
        pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_StoredAndRetrievedFromPanel ---
    {
        spdlog::info("DeltaData_StoredAndRetrievedFromPanel");
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        ASSERT_TRUE(treeIter);
        gint64 nodeId = treeIter.get_node_id();

        CtTimelineEntry e;
        e.nodeId = nodeId;
        e.timestamp = 9000;
        e.actionDescription = "Test delta storage";
        e.actionType = "INS";
        e.regionOffset = 0;
        e.regionLength = 4;
        e.deltaData = "INS|0|dGVzdA==|";
        panel->add_entry(e);
        drainEvents();

        auto entries = panel->get_all_entries();
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].deltaData, "INS|0|dGVzdA==|")
            << "deltaData should round-trip through the panel";

        panel->clear();
        drainEvents();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Section A: Delta serialization completeness — every document model
    // operation produces a timeline entry with correctly-prefixed deltaData.
    // ═══════════════════════════════════════════════════════════════════════

    // --- DeltaData_PopulatedForFMT ---
    {
        spdlog::info("DeltaData_PopulatedForFMT");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        panel->clear();
        drainEvents();

        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        rBuffer->place_cursor(rBuffer->begin());
        rBuffer->insert_at_cursor("FORMAT");
        drainEvents();
        pBridge->onCursorMoved();
        drainEvents();
        panel->clear();
        drainEvents();

        auto iterS = rBuffer->begin();
        auto iterE = rBuffer->get_iter_at_offset(6);
        rBuffer->select_range(iterS, iterE);
        drainEvents();
        pActions->apply_tag_bold();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool found = false;
        for (const auto& e : entries) {
            if ((e.actionType == "FMT" || e.actionType == "RFM") && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("FMT|") == 0 || e.deltaData.find("RFM|") == 0)
                    << "Format deltaData should start with FMT| or RFM|";
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Bold format should produce FMT/RFM entry with deltaData";

        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_PopulatedForTED ---
    {
        spdlog::info("DeltaData_PopulatedForTED");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();
        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        pBridge->endTextEditSession();
        pBridge->beginPaste(nodeId);
        rBuffer->place_cursor(rBuffer->begin());
        rBuffer->insert_at_cursor("PASTED");
        drainEvents();
        pBridge->endPaste();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool found = false;
        for (const auto& e : entries) {
            if (e.actionType == "TED" && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("TED|") == 0)
                    << "Paste deltaData should start with 'TED|'";
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Paste should produce TED entry with deltaData";

        pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_PopulatedForWIns ---
    {
        spdlog::info("DeltaData_PopulatedForWIns");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();
        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        pBridge->endTextEditSession();
        rBuffer->place_cursor(rBuffer->end());
        int cbOffset = rBuffer->get_insert()->get_iter().get_offset();

        CtCodebox* pCtCodebox = new CtCodebox{
            pWin, "test code", "python3", 200, 80,
            cbOffset, "", true, false, true
        };
        pCtCodebox->insertInTextBuffer(rBuffer);
        pWin->get_tree_store().addAnchoredWidgets(
            pWin->curr_tree_iter(), {pCtCodebox}, &pWin->get_text_view().mm());
        auto desc = extractWidgetDesc(pCtCodebox, cbOffset);
        auto cmd = std::make_unique<InsertWidgetDeltaCommand>(
            pBridge->getDocumentModel(), nodeId, cbOffset, desc, "Insert codebox");
        pBridge->addCommandToStack(std::move(cmd));
        auto n = pBridge->getDocumentModel()->getNodeById(nodeId);
        if (n) n->getContent().insertWidget(cbOffset, desc);
        drainEvents();

        auto entries = panel->get_all_entries();
        bool found = false;
        for (const auto& e : entries) {
            if (e.actionType == "WIns" && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("WIns|") == 0)
                    << "Widget insert deltaData should start with 'WIns|'";
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Insert codebox should produce WIns entry with deltaData";

        pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_PopulatedForCBed ---
    {
        spdlog::info("DeltaData_PopulatedForCBed");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();
        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        pBridge->endTextEditSession();
        rBuffer->place_cursor(rBuffer->end());
        int cbOffset = rBuffer->get_insert()->get_iter().get_offset();

        CtCodebox* pCtCodebox = new CtCodebox{
            pWin, "original code", "python3", 200, 80,
            cbOffset, "", true, false, true
        };
        pCtCodebox->insertInTextBuffer(rBuffer);
        pWin->get_tree_store().addAnchoredWidgets(
            pWin->curr_tree_iter(), {pCtCodebox}, &pWin->get_text_view().mm());
        auto desc = extractWidgetDesc(pCtCodebox, cbOffset);
        auto insertCmd = std::make_unique<InsertWidgetDeltaCommand>(
            pBridge->getDocumentModel(), nodeId, cbOffset, desc, "Insert codebox");
        pBridge->addCommandToStack(std::move(insertCmd));
        auto n = pBridge->getDocumentModel()->getNodeById(nodeId);
        if (n) n->getContent().insertWidget(cbOffset, desc);
        drainEvents();
        panel->clear();
        drainEvents();

        // Edit the codebox content via widget edit tracking
        pBridge->beginWidgetEdit(nodeId, pCtCodebox);
        pCtCodebox->get_buffer()->set_text("modified code");
        drainEvents();
        pBridge->endWidgetEdit();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool found = false;
        for (const auto& e : entries) {
            if (e.actionType == "CBed" && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("CBed|") == 0)
                    << "Codebox edit deltaData should start with 'CBed|'";
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Codebox edit should produce CBed entry with deltaData";

        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    // --- DeltaData_PopulatedForTCel ---
    {
        spdlog::info("DeltaData_PopulatedForTCel");
        auto* config = pWin->get_ct_config();
        bool savedEnabled = config->timelineEnabled;
        config->timelineEnabled = true;
        panel->clear();
        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        panel->clear();
        drainEvents();

        auto treeIter = pWin->curr_tree_iter();
        gint64 nodeId = treeIter.get_node_id();
        auto rBuffer = pWin->curr_buffer();
        ASSERT_TRUE(rBuffer);

        pBridge->endTextEditSession();
        rBuffer->place_cursor(rBuffer->end());
        int charOffset = rBuffer->get_insert()->get_iter().get_offset();

        CtTableMatrix tbl;
        for (int r = 0; r < 2; r++) {
            tbl.push_back(CtTableRow{});
            for (int c = 0; c < 2; c++) {
                tbl.back().push_back(new Glib::ustring{"c" + std::to_string(r) + std::to_string(c)});
            }
        }
        CtTableLight* pCtTable = new CtTableLight{pWin, tbl, 60, charOffset, "", CtTableColWidths{}};
        pCtTable->insertInTextBuffer(rBuffer);
        pWin->get_tree_store().addAnchoredWidgets(
            pWin->curr_tree_iter(), {pCtTable}, &pWin->get_text_view().mm());
        auto desc = extractWidgetDesc(pCtTable, charOffset);
        auto insertCmd = std::make_unique<InsertWidgetDeltaCommand>(
            pBridge->getDocumentModel(), nodeId, charOffset, desc, "Insert table");
        pBridge->addCommandToStack(std::move(insertCmd));
        auto n = pBridge->getDocumentModel()->getNodeById(nodeId);
        if (n) n->getContent().insertWidget(charOffset, desc);
        drainEvents();
        panel->clear();
        drainEvents();

        // Edit a table cell
        pBridge->beginWidgetEdit(nodeId, pCtTable, 0, 1);
        pCtTable->set_cell_text(0, 1, "edited");
        drainEvents();
        pBridge->endWidgetEdit();
        drainEvents();

        auto entries = panel->get_all_entries();
        bool found = false;
        for (const auto& e : entries) {
            if (e.actionType == "TCel" && !e.deltaData.empty()) {
                EXPECT_TRUE(e.deltaData.find("TCel|") == 0)
                    << "Table cell edit deltaData should start with 'TCel|'";
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Table cell edit should produce TCel entry with deltaData";

        while (pBridge->canUndo()) pActions->requested_step_back();
        drainEvents();
        config->timelineEnabled = savedEnabled;
        panel->clear();
        drainEvents();
    }

    spdlog::info("=== All Timeline Tests Passed ===");
}

TEST(TimelineGUI, AllTests)
{
    TestTimelineApp app;
    app.run(0, nullptr);
}

// Replay centres the view on the change and keeps it there while the buffer is
// rebuilt on every flash cycle. Needs a mapped window so the text view has a real layout.
class TestTimelineScrollApp : public CtApp
{
public:
    TestTimelineScrollApp() : CtApp{"_test_timeline_scroll", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
};

void TestTimelineScrollApp::on_activate()
{
    _on_startup();
    auto _quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));
    auto on_scope_exit = scope_guard([&](void*) {
        pWin->force_exit() = true;
        remove_window(*pWin);
    });

    auto drainEvents = [](){
        while (gtk_events_pending()) gtk_main_iteration_do(false);
    };
    pWin->resize(800, 600);
    pWin->show_all();
    drainEvents();

    auto* pBridge = pWin->get_command_bridge();
    auto* pActions = pWin->get_ct_actions();
    auto* panel = pWin->get_timeline_panel();
    pWin->get_ct_config()->timelineEnabled = true;

    auto ctIter = pWin->get_tree_store().get_node_from_node_name("b");
    ASSERT_TRUE(ctIter);
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(ctIter));
    drainEvents();

    auto buffer = pWin->curr_buffer();
    ASSERT_TRUE(buffer);
    Glib::ustring lines;
    for (int i = 0; i < 300; ++i) lines += "scroll test line " + std::to_string(i) + "\n";
    buffer->place_cursor(buffer->end());
    buffer->insert_at_cursor(lines);
    drainEvents();
    pBridge->onCursorMoved();
    drainEvents();

    auto vAdj = pWin->getScrolledwindowText().get_vadjustment();
    ASSERT_GT(vAdj->get_upper() - vAdj->get_page_size(), 1000.0) << "node must be long enough to scroll";

    // bold a word far down the node, then scroll back to the top
    Gtk::TextView& textView = pWin->get_text_view().mm();
    auto lineIter = buffer->get_iter_at_line(200);
    auto wordEnd = lineIter;
    wordEnd.forward_chars(6);
    const int changeOffset = lineIter.get_offset();
    panel->clear();
    buffer->select_range(lineIter, wordEnd);
    pActions->apply_tag_bold();
    drainEvents();
    vAdj->set_value(0);
    drainEvents();

    auto entries = panel->get_all_entries();
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries[0].regionOffset, changeOffset);

    auto& tv = panel->getTreeView();
    auto store = tv.get_model();
    tv.row_activated(store->get_path(store->children().begin()), *tv.get_column(0));

    // sample the frames GTK would draw (nothing left to process) through the whole replay
    double scrollFirst{-1}, maxScrollMove{0}, maxOffCentre{0};
    int hidden{0};
    bool sawReplay{false};
    const gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < deadline) {
        gtk_main_iteration_do(false);
        if (gtk_events_pending()) continue;
        if (panel->isReplaying()) {
            sawReplay = true;
            if (scrollFirst < 0) scrollFirst = vAdj->get_value();
            maxScrollMove = std::max(maxScrollMove, std::abs(vAdj->get_value() - scrollFirst));
            Gdk::Rectangle visible, change;
            textView.get_visible_rect(visible);
            textView.get_iter_location(textView.get_buffer()->get_iter_at_offset(changeOffset), change);
            const double changeCentre = change.get_y() + change.get_height() / 2.0;
            if (changeCentre < visible.get_y() or changeCentre > visible.get_y() + visible.get_height()) ++hidden;
            maxOffCentre = std::max(maxOffCentre, std::abs(changeCentre - (visible.get_y() + visible.get_height() / 2.0)));
        }
        else if (sawReplay) break;
        g_usleep(1000);
    }
    drainEvents();
    EXPECT_TRUE(sawReplay) << "activating the FMT entry should start a replay";
    EXPECT_FALSE(panel->isReplaying()) << "replay should finish";
    EXPECT_GT(scrollFirst, 1000.0) << "the view should have scrolled down to the change";
    EXPECT_EQ(hidden, 0) << "the change went out of view during the replay";
    EXPECT_LE(maxOffCentre, 30.0) << "the change should be centred in the view";
    EXPECT_LE(maxScrollMove, 1.0) << "scroll moved during replay";
    EXPECT_NEAR(vAdj->get_value(), scrollFirst, 1.0) << "scroll should stay on the change after replay";

    // after the replay the view must scroll freely again
    vAdj->set_value(0);
    drainEvents();
    EXPECT_NEAR(vAdj->get_value(), 0.0, 1.0) << "scroll fixup still pinning the view after replay";

    panel->clear();
    while (pBridge->canUndo()) pActions->requested_step_back();
    drainEvents();
}

TEST(TimelineGUI, ReplayCentresOnChange)
{
    TestTimelineScrollApp app;
    app.run(0, nullptr);
}

// Activating an entry while another replay is still flashing must not replay
// from an unsynced model: nodes of a loaded document start in the model with
// no content, so the replay would blank the node (text and images).
class TestTimelineNodeSwitchApp : public CtApp
{
public:
    TestTimelineNodeSwitchApp() : CtApp{"_test_timeline_node_switch", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
};

void TestTimelineNodeSwitchApp::on_activate()
{
    _on_startup();
    auto _quitGuard = scope_guard([this](void*) { quit(); });

    CtMainWin* pWin = _create_window(true/*start_hidden*/);
    const fs::path test_file = fs::path(UT::unitTestsDataDir) / "test_документ.ctb";
    ASSERT_TRUE(pWin->file_open(test_file, ""/*node*/, ""/*anchor*/, UT::testPassword));
    auto on_scope_exit = scope_guard([&](void*) {
        pWin->force_exit() = true;
        remove_window(*pWin);
    });

    auto drainEvents = [](){
        while (gtk_events_pending()) gtk_main_iteration_do(false);
    };
    // shown, so the text view lays out the buffers it displays
    pWin->resize(1200, 800);
    pWin->show_all();
    drainEvents();

    auto* panel = pWin->get_timeline_panel();
    pWin->get_ct_config()->timelineEnabled = true;

    // start on a node without timeline entries, as after opening the document
    auto& treeStore = pWin->get_tree_store();
    pWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(treeStore.get_node_from_node_id(1)));
    drainEvents();

    struct NodeState { int chars; size_t widgets; };
    auto f_state = [&](gint64 nodeId) {
        CtTreeIter it = treeStore.get_node_from_node_id(nodeId);
        return NodeState{it.get_node_text_buffer()->get_char_count(), it.get_anchored_widgets_fast().size()};
    };
    const NodeState bBefore = f_state(2);
    const NodeState eBefore = f_state(5);
    ASSERT_GT(bBefore.widgets, 0u);
    ASSERT_GT(eBefore.widgets, 0u);
    ASSERT_FALSE(pWin->get_file_save_needed());

    // entries as loaded from the timeline file of a previous session
    auto f_entry = [](gint64 nodeId, gint64 ts) {
        CtTimelineEntry e;
        e.nodeId = nodeId;
        e.timestamp = ts;
        e.useDay = 1;
        e.actionType = "FMT";
        e.actionDescription = "Format (bold)";
        e.regionOffset = 0;
        e.regionLength = 3;
        e.deltaData = "FMT|0|3|weight|heavy|0,3,";
        return e;
    };
    panel->load_entries({f_entry(2, 2000), f_entry(5, 1000)});
    drainEvents();

    auto& tv = panel->getTreeView();
    auto store = tv.get_model();
    // load_entries keeps the given order: row 0 is node 2, row 1 is node 5
    ASSERT_EQ(store->children().size(), 2u);
    auto f_activate = [&](int rowIdx) {
        tv.row_activated(store->get_path(store->children()[rowIdx]), *tv.get_column(0));
    };

    f_activate(0);
    drainEvents();
    ASSERT_TRUE(panel->isReplaying());
    ASSERT_EQ(pWin->curr_tree_iter().get_node_id(), 2);
    f_activate(1);  // while node 2 is still flashing

    const gint64 deadline = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < deadline) {
        drainEvents();
        if (!panel->isReplaying()) break;
        g_usleep(5000);
    }
    drainEvents();
    ASSERT_FALSE(panel->isReplaying());

    const NodeState bAfter = f_state(2);
    const NodeState eAfter = f_state(5);
    EXPECT_EQ(bAfter.chars, bBefore.chars);
    EXPECT_EQ(bAfter.widgets, bBefore.widgets);
    EXPECT_EQ(eAfter.chars, eBefore.chars) << "replayed node lost its content";
    EXPECT_EQ(eAfter.widgets, eBefore.widgets) << "replayed node lost its images";
    EXPECT_FALSE(pWin->get_file_save_needed()) << "browsing the timeline must not mark the document modified";
}

TEST(TimelineGUI, ReplayAfterNodeSwitchDuringReplayKeepsContent)
{
    TestTimelineNodeSwitchApp app;
    app.run(0, nullptr);
}
