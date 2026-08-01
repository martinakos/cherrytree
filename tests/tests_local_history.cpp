/*
 * tests_local_history.cpp
 *
 * GTK-free unit tests for local history offset adjustment, shift extraction,
 * and CtHistoryEntry data model.
 */

#include "ct_filesystem.h"
#include "ct_command.h"
#include "ct_text_commands.h"
#include "ct_widget_commands.h"
#include "ct_drawing_commands.h"
#include "ct_node_commands.h"
#include "ct_document_model.h"
#include "ct_types.h"
#include <gtest/gtest.h>
#include <vector>
#include <algorithm>

// Pure offset adjustment logic (mirrors CtHistoryPanel::adjustOffsetsForNode)
static int applyShifts(int offset, const std::vector<CtCommand::OffsetShift>& shifts)
{
    if (offset < 0) return offset;
    for (const auto& shift : shifts) {
        if (offset >= shift.offset) {
            offset += shift.delta;
            offset = std::max(0, offset);
        } else if (shift.delta < 0 && offset > shift.offset + shift.delta) {
            offset = shift.offset;
        }
    }
    return offset;
}

// ─── Offset Adjustment Tests ────────────────────────────────────────────────

TEST(OffsetAdjust, InsertBefore)
{
    // Insert 5 chars at offset 5, entry at offset 10 → entry shifts to 15
    std::vector<CtCommand::OffsetShift> shifts = {{5, 5}};
    EXPECT_EQ(applyShifts(10, shifts), 15);
}

TEST(OffsetAdjust, InsertAfter)
{
    // Insert at offset 15, entry at offset 10 → entry stays at 10
    std::vector<CtCommand::OffsetShift> shifts = {{15, 5}};
    EXPECT_EQ(applyShifts(10, shifts), 10);
}

TEST(OffsetAdjust, InsertAtSameOffset)
{
    // Insert 5 chars at offset 10, entry at offset 10 → entry shifts to 15
    std::vector<CtCommand::OffsetShift> shifts = {{10, 5}};
    EXPECT_EQ(applyShifts(10, shifts), 15);
}

TEST(OffsetAdjust, DeleteBefore)
{
    // Delete 5 chars at offset 0, entry at offset 10 → entry shifts to 5
    std::vector<CtCommand::OffsetShift> shifts = {{0, -5}};
    EXPECT_EQ(applyShifts(10, shifts), 5);
}

TEST(OffsetAdjust, DeleteAfter)
{
    // Delete at offset 15, entry at offset 10 → entry stays at 10
    std::vector<CtCommand::OffsetShift> shifts = {{15, -5}};
    EXPECT_EQ(applyShifts(10, shifts), 10);
}

TEST(OffsetAdjust, DeleteOverlapping)
{
    // Delete [8,12] (offset=8, delta=-4), entry at offset 10
    // 10 >= 8 → 10 + (-4) = 6
    std::vector<CtCommand::OffsetShift> shifts = {{8, -4}};
    EXPECT_EQ(applyShifts(10, shifts), 6);
}

TEST(OffsetAdjust, DeleteAtEntry)
{
    // Delete [10,15] (offset=10, delta=-5), entry at offset 10 → entry stays at 10
    std::vector<CtCommand::OffsetShift> shifts = {{10, -5}};
    EXPECT_EQ(applyShifts(10, shifts), 5);
}

TEST(OffsetAdjust, MultipleShifts)
{
    // Insert at 0 (+5), delete at 20 (-3), insert at 10 (+2) → entry at 15
    // Step 1: 15 >= 0, so 15+5=20
    // Step 2: 20 >= 20, so 20+(-3)=17
    // Step 3: 17 >= 10, so 17+2=19
    std::vector<CtCommand::OffsetShift> shifts = {{0, 5}, {20, -3}, {10, 2}};
    EXPECT_EQ(applyShifts(15, shifts), 19);
}

TEST(OffsetAdjust, NegativeClamp)
{
    // Delete 20 chars at offset 0, entry at offset 5 → clamped to 0
    std::vector<CtCommand::OffsetShift> shifts = {{0, -20}};
    EXPECT_EQ(applyShifts(5, shifts), 0);
}

TEST(OffsetAdjust, EmptyShiftLog)
{
    std::vector<CtCommand::OffsetShift> shifts;
    EXPECT_EQ(applyShifts(15, shifts), 15);
}

TEST(OffsetAdjust, MultipleEntries)
{
    std::vector<CtCommand::OffsetShift> shifts = {{5, 10}};
    EXPECT_EQ(applyShifts(3, shifts), 3);   // before insert → unchanged
    EXPECT_EQ(applyShifts(5, shifts), 15);   // at insert → shifted
    EXPECT_EQ(applyShifts(20, shifts), 30);  // after insert → shifted
}

TEST(OffsetAdjust, RegionLengthUnchanged)
{
    // Shifts affect regionOffset but not regionLength — verify no mutation
    CtHistoryEntry e;
    e.regionOffset = 10;
    e.regionLength = 5;

    std::vector<CtCommand::OffsetShift> shifts = {{0, 10}};
    e.regionOffset = applyShifts(e.regionOffset, shifts);

    EXPECT_EQ(e.regionOffset, 20);
    EXPECT_EQ(e.regionLength, 5);
}

TEST(OffsetAdjust, NegativeOffsetUntouched)
{
    // Entry with regionOffset=-1 (no position) should remain -1
    std::vector<CtCommand::OffsetShift> shifts = {{0, 10}};
    EXPECT_EQ(applyShifts(-1, shifts), -1);
}

// ─── Shift Extraction Tests ────────────────────────────────────────────────

class ShiftExtractTest : public ::testing::Test {
protected:
    std::shared_ptr<CtDocumentModel> docModel;

    void SetUp() override {
        docModel = std::make_shared<CtDocumentModel>();
    }
};

TEST_F(ShiftExtractTest, InsertText)
{
    InsertTextCommand cmd(docModel, 1, 10, "hello", {});
    std::vector<CtCommand::OffsetShift> log;
    cmd.appendShifts(log, false);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].offset, 10);
    EXPECT_EQ(log[0].delta, 5);
}

TEST_F(ShiftExtractTest, DeleteRange)
{
    DeleteRangeCommand cmd(docModel, 1, 5, 3);
    std::vector<CtCommand::OffsetShift> log;
    cmd.appendShifts(log, false);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].offset, 5);
    EXPECT_EQ(log[0].delta, -3);
}

TEST_F(ShiftExtractTest, InsertTextUndo)
{
    InsertTextCommand cmd(docModel, 1, 10, "hello", {});
    std::vector<CtCommand::OffsetShift> log;
    cmd.appendShifts(log, true);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].offset, 10);
    EXPECT_EQ(log[0].delta, -5);
}

TEST_F(ShiftExtractTest, DeleteRangeUndo)
{
    DeleteRangeCommand cmd(docModel, 1, 5, 3);
    std::vector<CtCommand::OffsetShift> log;
    cmd.appendShifts(log, true);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].offset, 5);
    EXPECT_EQ(log[0].delta, 3);
}

TEST_F(ShiftExtractTest, CompoundCommand)
{
    auto compound = std::make_unique<CompoundCommand>("test compound");
    compound->addCommand(std::make_unique<InsertTextCommand>(docModel, 1, 0, "abc", std::map<std::string, std::string>{}));
    compound->addCommand(std::make_unique<InsertTextCommand>(docModel, 1, 10, "de", std::map<std::string, std::string>{}));
    compound->addCommand(std::make_unique<DeleteRangeCommand>(docModel, 1, 5, 2));

    std::vector<CtCommand::OffsetShift> log;
    compound->appendShifts(log, false);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0].offset, 0);  EXPECT_EQ(log[0].delta, 3);
    EXPECT_EQ(log[1].offset, 10); EXPECT_EQ(log[1].delta, 2);
    EXPECT_EQ(log[2].offset, 5);  EXPECT_EQ(log[2].delta, -2);
}

TEST_F(ShiftExtractTest, CompoundUndo)
{
    auto compound = std::make_unique<CompoundCommand>("test compound");
    compound->addCommand(std::make_unique<InsertTextCommand>(docModel, 1, 0, "abc", std::map<std::string, std::string>{}));
    compound->addCommand(std::make_unique<InsertTextCommand>(docModel, 1, 10, "de", std::map<std::string, std::string>{}));
    compound->addCommand(std::make_unique<DeleteRangeCommand>(docModel, 1, 5, 2));

    std::vector<CtCommand::OffsetShift> log;
    compound->appendShifts(log, true);

    // Reverse iteration, reversed deltas
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0].offset, 5);  EXPECT_EQ(log[0].delta, 2);   // delete undone → insert
    EXPECT_EQ(log[1].offset, 10); EXPECT_EQ(log[1].delta, -2);  // insert undone → delete
    EXPECT_EQ(log[2].offset, 0);  EXPECT_EQ(log[2].delta, -3);  // insert undone → delete
}

TEST_F(ShiftExtractTest, FormatCommand)
{
    ApplyFormatCommand cmd(docModel, 1, 10, 5, "weight", "bold");
    std::vector<CtCommand::OffsetShift> log;
    cmd.appendShifts(log, false);
    EXPECT_TRUE(log.empty());
}

TEST_F(ShiftExtractTest, RemoveFormatCommand)
{
    RemoveFormatCommand cmd(docModel, 1, 10, 5, "weight");
    std::vector<CtCommand::OffsetShift> log;
    cmd.appendShifts(log, false);
    EXPECT_TRUE(log.empty());
}

// ─── Command Accessor Tests ────────────────────────────────────────────────

TEST_F(ShiftExtractTest, InsertTextAccessors)
{
    InsertTextCommand cmd(docModel, 1, 10, "hello", {});
    EXPECT_EQ(cmd.getActionType(), "INS");
    EXPECT_EQ(cmd.getRegionOffset(), 10);
    EXPECT_EQ(cmd.getRegionLength(), 5);
    EXPECT_EQ(cmd.getCanvasIndex(), -1);
}

TEST_F(ShiftExtractTest, DeleteRangeAccessors)
{
    DeleteRangeCommand cmd(docModel, 1, 5, 3);
    EXPECT_EQ(cmd.getActionType(), "DEL");
    EXPECT_EQ(cmd.getRegionOffset(), 5);
    EXPECT_EQ(cmd.getRegionLength(), 0);
    EXPECT_EQ(cmd.getCanvasIndex(), -1);
}

TEST_F(ShiftExtractTest, FormatAccessors)
{
    ApplyFormatCommand cmd(docModel, 1, 10, 5, "weight", "bold");
    EXPECT_EQ(cmd.getActionType(), "FMT");
    EXPECT_EQ(cmd.getRegionOffset(), 10);
    EXPECT_EQ(cmd.getRegionLength(), 5);
}

TEST_F(ShiftExtractTest, RemoveFormatAccessors)
{
    RemoveFormatCommand cmd(docModel, 1, 10, 5, "weight");
    EXPECT_EQ(cmd.getActionType(), "RFM");
    EXPECT_EQ(cmd.getRegionOffset(), 10);
    EXPECT_EQ(cmd.getRegionLength(), 5);
}

// ─── CompoundCommand Accessor Delegation Tests ────────────────────────────────

TEST_F(ShiftExtractTest, CompoundAccessors_DelegatesToLastChild)
{
    auto compound = std::make_unique<CompoundCommand>("test compound");
    compound->addCommand(std::make_unique<InsertTextCommand>(docModel, 1, 0, "abc", std::map<std::string, std::string>{}));
    compound->addCommand(std::make_unique<DeleteRangeCommand>(docModel, 1, 10, 3));

    EXPECT_EQ(compound->getActionType(), "DEL");
    EXPECT_EQ(compound->getRegionOffset(), 10);
    EXPECT_EQ(compound->getRegionLength(), 0);
    EXPECT_EQ(compound->getCanvasIndex(), -1);
}

TEST_F(ShiftExtractTest, CompoundAccessors_SingleInsertChild)
{
    auto compound = std::make_unique<CompoundCommand>("single insert");
    compound->addCommand(std::make_unique<InsertTextCommand>(docModel, 1, 5, "hello", std::map<std::string, std::string>{}));

    EXPECT_EQ(compound->getActionType(), "INS");
    EXPECT_EQ(compound->getRegionOffset(), 5);
    EXPECT_EQ(compound->getRegionLength(), 5);
}

TEST_F(ShiftExtractTest, CompoundAccessors_FormatChild)
{
    auto compound = std::make_unique<CompoundCommand>("format");
    compound->addCommand(std::make_unique<ApplyFormatCommand>(docModel, 1, 10, 5, "weight", "bold"));

    EXPECT_EQ(compound->getActionType(), "FMT");
    EXPECT_EQ(compound->getRegionOffset(), 10);
    EXPECT_EQ(compound->getRegionLength(), 5);
}

TEST_F(ShiftExtractTest, CompoundAccessors_Empty)
{
    auto compound = std::make_unique<CompoundCommand>("empty");

    EXPECT_EQ(compound->getActionType(), "");
    EXPECT_EQ(compound->getRegionOffset(), -1);
    EXPECT_EQ(compound->getRegionLength(), 0);
    EXPECT_EQ(compound->getCanvasIndex(), -1);
}

// ─── CtHistoryEntry Data Model Tests ────────────────────────────────────────

TEST(HistoryEntry, DefaultValues)
{
    CtHistoryEntry e;
    EXPECT_EQ(e.nodeId, 0);
    EXPECT_EQ(e.timestamp, 0);
    EXPECT_EQ(e.useDay, 0);
    EXPECT_EQ(e.cursorPos, 0);
    EXPECT_EQ(e.scrollPos, 0);
    EXPECT_TRUE(e.actionDescription.empty());
    EXPECT_TRUE(e.actionType.empty());
    EXPECT_EQ(e.regionOffset, -1);
    EXPECT_EQ(e.regionLength, 0);
    EXPECT_EQ(e.canvasIdx, -1);
}

TEST(HistoryEntry, FieldAssignment)
{
    CtHistoryEntry e;
    e.nodeId = 42;
    e.timestamp = 1722500000;
    e.useDay = 15;
    e.cursorPos = 100;
    e.scrollPos = 250;
    e.actionDescription = "Type 'hello'";
    e.actionType = "INS";
    e.regionOffset = 50;
    e.regionLength = 5;
    e.canvasIdx = -1;

    EXPECT_EQ(e.nodeId, 42);
    EXPECT_EQ(e.timestamp, 1722500000);
    EXPECT_EQ(e.useDay, 15);
    EXPECT_EQ(e.cursorPos, 100);
    EXPECT_EQ(e.scrollPos, 250);
    EXPECT_EQ(e.actionDescription, "Type 'hello'");
    EXPECT_EQ(e.actionType, "INS");
    EXPECT_EQ(e.regionOffset, 50);
    EXPECT_EQ(e.regionLength, 5);
    EXPECT_EQ(e.canvasIdx, -1);
}
