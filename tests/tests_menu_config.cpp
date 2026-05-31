/*
 * tests_menu_config.cpp
 *
 * Tests for the configurable menu system: config string format validation,
 * XML generation, and round-trip serialization.
 */

#include "ct_const.h"
#include "ct_menu.h"
#include "ct_misc_utils.h"
#include "ct_storage_xml.h"
#include "tests_common.h"

static bool has_balanced_braces(const std::string& configStr)
{
    int depth = 0;
    for (const std::string& token : str::split(configStr, ",")) {
        if (token.empty()) continue;
        if (token.front() == '{') depth++;
        else if (token == "}") depth--;
        if (depth < 0) return false;
    }
    return depth == 0;
}

static int count_tokens(const std::string& configStr)
{
    int count = 0;
    for (const std::string& token : str::split(configStr, ",")) {
        if (!token.empty()) count++;
    }
    return count;
}

TEST(MenuConfigGroup, DefaultMenubarConstantsHaveBalancedBraces)
{
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_FILE_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_EDIT_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_INSERT_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_FORMAT_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_TOOLS_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_TREE_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_SEARCH_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_VIEW_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::MENUBAR_HELP_DEFAULT));
}

TEST(MenuConfigGroup, DefaultPopupConstantsHaveBalancedBraces)
{
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_NODE_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_TEXT_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_CODE_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_IMAGE_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_LATEX_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_ANCHOR_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_EMBFILE_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_TERMINAL_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_LINK_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_CODEBOX_DEFAULT));
    EXPECT_TRUE(has_balanced_braces(CtConst::POPUP_TABLE_CELL_DEFAULT));
}

TEST(MenuConfigGroup, DefaultConstantsAreNonEmpty)
{
    EXPECT_GT(count_tokens(CtConst::MENUBAR_FILE_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_EDIT_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_INSERT_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_FORMAT_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_TOOLS_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_TREE_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_SEARCH_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_VIEW_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::MENUBAR_HELP_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::POPUP_NODE_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::POPUP_TEXT_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::POPUP_CODE_DEFAULT), 0);
    EXPECT_GT(count_tokens(CtConst::POPUP_TABLE_CELL_DEFAULT), 0);
}

TEST(MenuConfigGroup, TopLevelOrderContainsAllExpectedMenus)
{
    std::string order = CtConst::MENUBAR_TOP_LEVEL_DEFAULT;
    EXPECT_NE(std::string::npos, order.find("FileMenu"));
    EXPECT_NE(std::string::npos, order.find("EditMenu"));
    EXPECT_NE(std::string::npos, order.find("InsertMenu"));
    EXPECT_NE(std::string::npos, order.find("FormatMenu"));
    EXPECT_NE(std::string::npos, order.find("ToolsMenu"));
    EXPECT_NE(std::string::npos, order.find("TreeMenu"));
    EXPECT_NE(std::string::npos, order.find("SearchMenu"));
    EXPECT_NE(std::string::npos, order.find("ViewMenu"));
    EXPECT_NE(std::string::npos, order.find("BookmarksMenu"));
    EXPECT_NE(std::string::npos, order.find("HelpMenu"));
}

TEST(MenuConfigGroup, GenerateMenuXmlSimpleItems)
{
    std::string xml = CtMenu::generate_menu_xml("act_undo,act_redo");
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='act_undo'/>"));
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='act_redo'/>"));
}

TEST(MenuConfigGroup, GenerateMenuXmlSeparator)
{
    std::string xml = CtMenu::generate_menu_xml("act_undo,separator,act_redo");
    EXPECT_NE(std::string::npos, xml.find("<separator/>"));
}

TEST(MenuConfigGroup, GenerateMenuXmlSubmenu)
{
    std::string xml = CtMenu::generate_menu_xml("{TestSub,item1,item2,}");
    EXPECT_NE(std::string::npos, xml.find("<menu action='TestSub'>"));
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='item1'/>"));
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='item2'/>"));
    EXPECT_NE(std::string::npos, xml.find("</menu>"));
}

TEST(MenuConfigGroup, GenerateMenuXmlNestedSubmenus)
{
    std::string xml = CtMenu::generate_menu_xml("{Outer,{Inner,item,},}");
    EXPECT_NE(std::string::npos, xml.find("<menu action='Outer'>"));
    EXPECT_NE(std::string::npos, xml.find("<menu action='Inner'>"));
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='item'/>"));
    // Should have two </menu> closings
    size_t pos1 = xml.find("</menu>");
    EXPECT_NE(std::string::npos, pos1);
    size_t pos2 = xml.find("</menu>", pos1 + 1);
    EXPECT_NE(std::string::npos, pos2);
}

TEST(MenuConfigGroup, GenerateMenuXmlEmptyString)
{
    std::string xml = CtMenu::generate_menu_xml("");
    EXPECT_TRUE(xml.empty());
}

TEST(MenuConfigGroup, GenerateMenuXmlTrailingCommas)
{
    std::string xml = CtMenu::generate_menu_xml("item1,item2,");
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='item1'/>"));
    EXPECT_NE(std::string::npos, xml.find("<menuitem action='item2'/>"));
}

TEST(MenuConfigGroup, ConfigRoundTripSimple)
{
    std::string input = "act_undo,separator,act_redo";
    std::vector<std::string> tokens;
    for (const std::string& t : str::split(input, ",")) {
        if (!t.empty()) tokens.push_back(t);
    }
    std::string output = str::join(tokens, ",");
    EXPECT_EQ(input, output);
}

TEST(MenuConfigGroup, ConfigRoundTripWithSubmenus)
{
    std::string input = "{Sub,item1,item2,},separator,item3";
    std::vector<std::string> tokens;
    for (const std::string& t : str::split(input, ",")) {
        if (!t.empty()) tokens.push_back(t);
    }
    std::string output = str::join(tokens, ",");
    EXPECT_EQ(input, output);
}

TEST(MenuConfigGroup, AllDefaultConfigsProduceValidXml)
{
    auto testConfig = [](const char* configStr) {
        std::string xml = "<popup>" + CtMenu::generate_menu_xml(configStr) + "</popup>";
        xmlpp::DomParser parser;
        EXPECT_TRUE(CtXmlHelper::safe_parse_memory(parser, xml.c_str()))
            << "Failed to parse XML from config: " << configStr;
    };
    testConfig(CtConst::POPUP_NODE_DEFAULT);
    testConfig(CtConst::POPUP_TEXT_DEFAULT);
    testConfig(CtConst::POPUP_CODE_DEFAULT);
    testConfig(CtConst::POPUP_IMAGE_DEFAULT);
    testConfig(CtConst::POPUP_LATEX_DEFAULT);
    testConfig(CtConst::POPUP_ANCHOR_DEFAULT);
    testConfig(CtConst::POPUP_EMBFILE_DEFAULT);
    testConfig(CtConst::POPUP_TERMINAL_DEFAULT);
    testConfig(CtConst::POPUP_LINK_DEFAULT);
    testConfig(CtConst::POPUP_CODEBOX_DEFAULT);
    testConfig(CtConst::POPUP_TABLE_CELL_DEFAULT);
    testConfig(CtConst::MENUBAR_FILE_DEFAULT);
    testConfig(CtConst::MENUBAR_EDIT_DEFAULT);
    testConfig(CtConst::MENUBAR_INSERT_DEFAULT);
    testConfig(CtConst::MENUBAR_FORMAT_DEFAULT);
    testConfig(CtConst::MENUBAR_TOOLS_DEFAULT);
    testConfig(CtConst::MENUBAR_TREE_DEFAULT);
    testConfig(CtConst::MENUBAR_SEARCH_DEFAULT);
    testConfig(CtConst::MENUBAR_VIEW_DEFAULT);
    testConfig(CtConst::MENUBAR_HELP_DEFAULT);
}

TEST(MenuConfigGroup, SubmenuRemovalAlgorithmForward)
{
    // Simulates removing a submenu block when the open marker is selected.
    // Config: "a,{Sub,b,c,},d" — removing {Sub should remove {Sub,b,c,} leaving "a,d"
    std::vector<std::string> items = {"a", "{Sub", "b", "c", "}", "d"};
    int selIdx = 1; // {Sub
    int depth = 1;
    int count = 1;
    for (size_t i = selIdx + 1; i < items.size(); i++) {
        count++;
        if (!items[i].empty() && items[i][0] == '{') depth++;
        else if (items[i] == "}") { if (--depth == 0) break; }
    }
    items.erase(items.begin() + selIdx, items.begin() + selIdx + count);
    EXPECT_EQ(2u, items.size());
    EXPECT_EQ("a", items[0]);
    EXPECT_EQ("d", items[1]);
}

TEST(MenuConfigGroup, SubmenuRemovalAlgorithmBackward)
{
    // Simulates removing a submenu block when the close marker is selected.
    // Config: "a,{Sub,b,c,},d" — removing } should also remove {Sub,b,c,}
    std::vector<std::string> items = {"a", "{Sub", "b", "c", "}", "d"};
    int selIdx = 4; // }
    int depth = 1;
    int openIdx = -1;
    for (int i = selIdx - 1; i >= 0; i--) {
        if (items[i] == "}") depth++;
        else if (!items[i].empty() && items[i][0] == '{') { if (--depth == 0) { openIdx = i; break; } }
    }
    ASSERT_GE(openIdx, 0);
    int count = selIdx - openIdx + 1;
    items.erase(items.begin() + openIdx, items.begin() + openIdx + count);
    EXPECT_EQ(2u, items.size());
    EXPECT_EQ("a", items[0]);
    EXPECT_EQ("d", items[1]);
}

TEST(MenuConfigGroup, NestedSubmenuRemovalFromOuter)
{
    // Config: "a,{Outer,{Inner,x,},y,},b" — removing {Outer removes everything up to matching }
    std::vector<std::string> items = {"a", "{Outer", "{Inner", "x", "}", "y", "}", "b"};
    int selIdx = 1; // {Outer
    int depth = 1;
    int count = 1;
    for (size_t i = selIdx + 1; i < items.size(); i++) {
        count++;
        if (!items[i].empty() && items[i][0] == '{') depth++;
        else if (items[i] == "}") { if (--depth == 0) break; }
    }
    items.erase(items.begin() + selIdx, items.begin() + selIdx + count);
    EXPECT_EQ(2u, items.size());
    EXPECT_EQ("a", items[0]);
    EXPECT_EQ("b", items[1]);
}

TEST(MenuConfigGroup, NestedSubmenuRemovalFromInner)
{
    // Config: "a,{Outer,{Inner,x,},y,},b" — removing {Inner removes only the inner submenu
    std::vector<std::string> items = {"a", "{Outer", "{Inner", "x", "}", "y", "}", "b"};
    int selIdx = 2; // {Inner
    int depth = 1;
    int count = 1;
    for (size_t i = selIdx + 1; i < items.size(); i++) {
        count++;
        if (!items[i].empty() && items[i][0] == '{') depth++;
        else if (items[i] == "}") { if (--depth == 0) break; }
    }
    items.erase(items.begin() + selIdx, items.begin() + selIdx + count);
    EXPECT_EQ(5u, items.size());
    EXPECT_EQ("a", items[0]);
    EXPECT_EQ("{Outer", items[1]);
    EXPECT_EQ("y", items[2]);
    EXPECT_EQ("}", items[3]);
    EXPECT_EQ("b", items[4]);
}
