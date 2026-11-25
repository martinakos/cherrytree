/*
 * tests_agent_server.cpp
 *
 * The AI agent (MCP) server: the JSON-RPC protocol, each tool against a real
 * document, undo of the agent's edits, and a round trip through the socket.
 */

#include "tests_common.h"
#include "ct_app.h"
#include "ct_agent_server.h"
#include "ct_command_bridge.h"
#include "ct_node_commands.h"
#include "ct_codebox.h"
#include "ct_storage_control.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

using json = nlohmann::json;

class TestAgentServerApp : public CtApp
{
public:
    TestAgentServerApp() : CtApp{"_test_agent", Gio::APPLICATION_NON_UNIQUE} { _no_gui = true; }

private:
    void on_activate() final;
    void _run_tests(CtMainWin* pWin);
};

void TestAgentServerApp::on_activate()
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

static void drain_events()
{
    while (gtk_events_pending()) gtk_main_iteration_do(false);
}

// Runs a tool that must succeed and returns its result
static json call_ok(CtMainWin* pWin, const std::string& tool, const json& args)
{
    bool isError{true};
    const std::string result = CtAgentServer::call_tool(pWin, tool, args.dump(), isError);
    drain_events();
    EXPECT_FALSE(isError) << tool << ": " << result;
    return json::parse(result);
}

// Runs a tool that must refuse and returns its error message
static std::string call_err(CtMainWin* pWin, const std::string& tool, const json& args)
{
    bool isError{false};
    const std::string result = CtAgentServer::call_tool(pWin, tool, args.dump(), isError);
    drain_events();
    EXPECT_TRUE(isError) << tool << ": " << result;
    return json::parse(result).value("error", "");
}

static int sibling_position_for_test(CtTreeStore& store, const gint64 nodeId)
{
    int pos{0};
    Gtk::TreeModel::iterator tmp = store.get_node_from_node_id(nodeId);
    while (--tmp) ++pos;
    return pos;
}

// Text inside the <note_content> marker that tools put around document text
static std::string unwrap_note(const std::string& text)
{
    const size_t open = text.find('>');
    const size_t close = text.rfind("</");
    if (0 != text.rfind("<note_content", 0) or std::string::npos == open or std::string::npos == close or close < open) {
        ADD_FAILURE() << "not inside a note_content marker: " << text;
        return text;
    }
    return text.substr(open + 1, close - open - 1);
}

static std::string node_text(CtMainWin* pWin, const gint64 nodeId)
{
    return unwrap_note(call_ok(pWin, "read_node", {{"id", nodeId}}).value("text", ""));
}

void TestAgentServerApp::_run_tests(CtMainWin* pWin)
{
    CtCommandBridge* pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge and pBridge->isActive());
    CtTreeStore& store = pWin->get_tree_store();
    CtAgentServer& server = CtAgentServer::instance();

    // the backup before the first agent change goes to a temporary folder, not next to the test document
    gchar* pBackupDir = g_dir_make_tmp("ct_agent_test_XXXXXX", nullptr);
    ASSERT_TRUE(pBackupDir);
    const std::string backupDir{pBackupDir};
    g_free(pBackupDir);
    CtConfig* pConfig = pWin->get_ct_config();
    pConfig->customBackupDirOn = true;
    pConfig->customBackupDir = backupDir;
    // the confirmation dialog is answered here, and the clock is ours
    int answer{1};
    std::vector<std::string> asked;
    CtAgentServer::set_confirm_hook([&answer, &asked](const std::string& summary, const std::vector<std::string>& details) {
        asked.push_back(summary + (details.empty() ? "" : " | " + details.front()));
        return answer;
    });
    gint64 fakeNow = g_get_monotonic_time();
    CtAgentServer::set_clock_hook([&fakeNow]() { return fakeNow; });
    CtAgentServer::reset_session();
    auto scopeGuard = scope_guard([&backupDir](void*) {
        CtAgentServer::set_confirm_hook(nullptr);
        CtAgentServer::set_clock_hook(nullptr);
        CtAgentServer::reset_session();
        fs::remove_all(backupDir);
    });

    // --- MCP protocol ---
    {
        const json init = json::parse(server.handle_message(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
        EXPECT_EQ(1, init["id"]);
        EXPECT_EQ("2025-06-18", init["result"]["protocolVersion"]);
        EXPECT_EQ("cherrytree", init["result"]["serverInfo"]["name"]);
        EXPECT_TRUE(init["result"]["capabilities"].contains("tools"));
        EXPECT_FALSE(init["result"].value("instructions", "").empty());

        EXPECT_EQ("", server.handle_message(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));

        const json list = json::parse(server.handle_message(R"({"jsonrpc":"2.0","id":"a","method":"tools/list"})"));
        EXPECT_EQ("a", list["id"]);
        ASSERT_EQ(24u, list["result"]["tools"].size());
        for (const json& tool : list["result"]["tools"]) {
            EXPECT_EQ("object", tool["inputSchema"]["type"]) << tool["name"];
            EXPECT_FALSE(tool.value("description", "").empty()) << tool["name"];
        }

        const json call = json::parse(server.handle_message(
            R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_current_node","arguments":{}}})"));
        EXPECT_FALSE(call["result"]["isError"].get<bool>());
        const json callResult = json::parse(call["result"]["content"][0]["text"].get<std::string>());
        EXPECT_NE(std::string::npos, callResult.value("document", "").find("test_документ.ctb"));

        const json unknownTool = json::parse(server.handle_message(
            R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"no_such_tool","arguments":{}}})"));
        EXPECT_TRUE(unknownTool["result"]["isError"].get<bool>());

        EXPECT_EQ(-32601, json::parse(server.handle_message(R"({"jsonrpc":"2.0","id":4,"method":"nope"})"))["error"]["code"]);
        EXPECT_EQ(-32700, json::parse(server.handle_message("{not json"))["error"]["code"]);
    }

    // --- list_tree ---
    const json tree = call_ok(pWin, "list_tree", json::object());
    ASSERT_GT(tree["count"].get<int>(), 0);
    EXPECT_EQ(0, tree["nodes"][0]["depth"]);
    EXPECT_EQ(1u, call_ok(pWin, "list_tree", {{"root_id", tree["nodes"][0]["id"]}, {"max_depth", 0}})["count"].get<size_t>());
    EXPECT_TRUE(tree["nodes"][0].contains("created"));
    EXPECT_TRUE(tree["nodes"][0].contains("modified"));
    {
        // sort_by modified: the two nodes changed last come first, newest first
        ASSERT_GE(tree["count"].get<int>(), 2);
        const gint64 olderId = tree["nodes"][0]["id"];
        const gint64 newerId = tree["nodes"][1]["id"];
        store.get_node_from_node_id(olderId).set_node_modification_time(4102444700);
        store.get_node_from_node_id(newerId).set_node_modification_time(4102444800);
        const json byModified = call_ok(pWin, "list_tree", {{"sort_by", "modified"}});
        EXPECT_EQ(tree["count"], byModified["count"]);
        EXPECT_EQ(newerId, byModified["nodes"][0]["id"].get<gint64>());
        EXPECT_EQ(olderId, byModified["nodes"][1]["id"].get<gint64>());

        // sort_by created: newest first
        const json byCreated = call_ok(pWin, "list_tree", {{"sort_by", "created"}});
        EXPECT_EQ(tree["count"], byCreated["count"]);
        for (size_t i = 1; i < byCreated["nodes"].size(); ++i) {
            EXPECT_GE(store.get_node_from_node_id(byCreated["nodes"][i - 1]["id"]).get_node_creating_time(),
                      store.get_node_from_node_id(byCreated["nodes"][i]["id"]).get_node_creating_time());
        }
        EXPECT_NE(std::string::npos, call_err(pWin, "list_tree", {{"sort_by", "name"}}).find("sort_by must be"));
    }

    // --- add_node with Markdown ---
    const json added = call_ok(pWin, "add_node", {{"name", "Agent Test"}, {"content", "# Title\n\nSome **bold** text\n"}});
    const gint64 parentId = added["id"];
    EXPECT_EQ("rich_text", added["type"]);
    {
        CtTreeIter iter = store.get_node_from_node_id(parentId);
        ASSERT_TRUE(iter);
        EXPECT_EQ("Agent Test", iter.get_node_name());
        const std::string text = node_text(pWin, parentId);
        EXPECT_NE(std::string::npos, text.find("Title")) << text;
        EXPECT_NE(std::string::npos, text.find("Some bold text")) << text;
        EXPECT_EQ(std::string::npos, text.find("**")) << text;
        // the Markdown became formatting
        bool hasBold{false};
        const CtNodeContent content = buildContentFromBuffer(iter.get_node_text_buffer(), iter.get_anchored_widgets());
        for (const CtContentElement& element : content.getElements()) {
            if (element.isTextSpan() and element.textSpan.text == "bold" and element.textSpan.hasAttribute("weight")) {
                hasBold = true;
            }
        }
        EXPECT_TRUE(hasBold);
    }

    // --- Markdown: _italic_ and a code block ---
    {
        const gint64 mdId = call_ok(pWin, "add_node", {{"name", "Md"}, {"content", "An _italic_ word\n```python\nprint(2)\n```\n"}})["id"];
        const std::string text = node_text(pWin, mdId);
        EXPECT_NE(std::string::npos, text.find("An italic word")) << text;
        EXPECT_NE(std::string::npos, text.find("\u27e6#0 codebox python ")) << text;
        EXPECT_NE(std::string::npos, text.find("print(2)")) << text;
        CtTreeIter iter = store.get_node_from_node_id(mdId);
        bool hasItalic{false};
        const CtNodeContent content = buildContentFromBuffer(iter.get_node_text_buffer(), iter.get_anchored_widgets());
        for (const CtContentElement& element : content.getElements()) {
            if (element.isTextSpan() and element.textSpan.text == "italic" and element.textSpan.getAttribute("style") == "italic") hasItalic = true;
        }
        EXPECT_TRUE(hasItalic);
        call_ok(pWin, "delete_node", {{"id", mdId}});
    }

    // --- Markdown: brackets in bold, tables, lists, code boxes after non-ASCII text ---
    {
        const std::string markdown =
            "# How it works\n"
            "1. **The agent (Claude Code).** It starts.\n"
            "- choose **Tools > Allow/Deny**. Then\n"
            "  - under `/run/user/<id>/` (usually) and snake_case_name\n"
            "- [ ] a task with [a link](https://example.com/a_(b)) and <https://example.org>\n"
            "\n"
            "| Tool | What it does |\n"
            "| --- | --- |\n"
            "| `list_tree` | List (all) the nodes |\n"
            "| read_node | Read \\| a node |\n"
            "\n"
            "Ünïcödé ⟦ text\n"
            "```\n"
            "a  ->  b\n"
            "```\n"
            "\n"
            "```sh\n"
            "claude mcp add x -- /p --mcp-stdio\n"
            "```\n"
            "\n"
            "Last ***words*** and ~~gone~~\n";
        const gint64 mdId = call_ok(pWin, "add_node", {{"name", "Md2"}, {"content", markdown}})["id"];
        const std::string text = node_text(pWin, mdId);
        // code boxes from Markdown take the default size of new code boxes and fit their height
        const CtConfig* pConfig = pWin->get_ct_config();
        const std::string codeboxSize = fmt::format("{}{}x{}", static_cast<int>(pConfig->codeboxWidth),
                                                    pConfig->codeboxWidthPixels ? "" : "%", static_cast<int>(pConfig->codeboxHeight));
        const std::string expected =
            "How it works\n"
            "1. The agent (Claude Code). It starts.\n"
            "• choose Tools > Allow/Deny. Then\n"
            "\t◇ under /run/user/<id>/ (usually) and snake_case_name\n"
            "☐ a task with a link and https://example.org\n"
            "\n"
            "⟦#0 table 3x2⟧\nTool | What it does\nlist_tree | List (all) the nodes\nread_node | Read | a node\n⟦/table⟧\n"
            "\n"
            "Ünïcödé ⟦ text\n"
            "⟦#1 codebox plain-text " + codeboxSize + " fit⟧\na  ->  b\n⟦/codebox⟧\n"
            "\n"
            "⟦#2 codebox sh " + codeboxSize + " fit⟧\nclaude mcp add x -- /p --mcp-stdio\n⟦/codebox⟧\n"
            "\n"
            "Last words and gone\n";
        EXPECT_EQ(expected, text);

        CtTreeIter iter = store.get_node_from_node_id(mdId);
        std::map<std::string, std::vector<std::string>> attrsOf; // text of a span -> its attributes
        const CtNodeContent content = buildContentFromBuffer(iter.get_node_text_buffer(), iter.get_anchored_widgets());
        for (const CtContentElement& element : content.getElements()) {
            if (not element.isTextSpan()) continue;
            for (const auto& attr : element.textSpan.attributes) {
                attrsOf[element.textSpan.text.raw()].push_back(attr.first + "=" + attr.second);
            }
        }
        auto has = [&](const std::string& spanText, const std::string& attr) {
            const auto& attrs = attrsOf[spanText];
            return std::find(attrs.begin(), attrs.end(), attr) != attrs.end();
        };
        EXPECT_TRUE(has("How it works", "scale=h1"));
        EXPECT_TRUE(has("The agent (Claude Code).", "weight=heavy"));
        EXPECT_TRUE(has("Tools > Allow/Deny", "weight=heavy"));
        EXPECT_TRUE(has("/run/user/<id>/", "family=monospace"));
        EXPECT_TRUE(has("a link", "link=webs https://example.com/a_(b)"));
        EXPECT_TRUE(has("https://example.org", "link=webs https://example.org"));
        EXPECT_TRUE(has("words", "weight=heavy"));
        EXPECT_TRUE(has("words", "style=italic"));
        EXPECT_TRUE(has("gone", "strikethrough=true"));
        call_ok(pWin, "delete_node", {{"id", mdId}});
    }

    // --- add_node: plain text child at position 0, code node, bad type ---
    const json child = call_ok(pWin, "add_node", {{"name", "Child"}, {"parent_id", parentId}, {"type", "plain_text"},
                                                  {"content", "**not markdown**"}, {"tags", "t1 t2"}});
    const gint64 childId = child["id"];
    EXPECT_EQ("**not markdown**", node_text(pWin, childId));
    EXPECT_EQ("t1 t2", child["tags"]);
    const json codeNode = call_ok(pWin, "add_node", {{"name", "Code"}, {"parent_id", parentId}, {"position", 0}, {"type", "python"},
                                                     {"content", "print(1)"}});
    EXPECT_EQ("python", codeNode["type"]);
    EXPECT_EQ(0, sibling_position_for_test(store, codeNode["id"]));
    EXPECT_NE(std::string::npos, call_err(pWin, "add_node", {{"name", "X"}, {"type", "no-such-language"}}).find("Unknown node type"));
    EXPECT_NE(std::string::npos, call_err(pWin, "add_node", {{"name", "X"}, {"parent_id", 99999999}}).find("not found"));

    // --- replace_text ---
    call_ok(pWin, "replace_text", {{"id", parentId}, {"old_text", "Some"}, {"new_text", "Any"}});
    EXPECT_NE(std::string::npos, node_text(pWin, parentId).find("Any bold text"));
    EXPECT_NE(std::string::npos, call_err(pWin, "replace_text", {{"id", parentId}, {"old_text", "zzz"}, {"new_text", "y"}}).find("not found"));
    call_ok(pWin, "insert_text", {{"id", parentId}, {"text", "\nbold again"}, {"markdown", false}});
    EXPECT_NE(std::string::npos, call_err(pWin, "replace_text", {{"id", parentId}, {"old_text", "bold"}, {"new_text", "y"}}).find("2 times"));
    EXPECT_EQ(2, call_ok(pWin, "replace_text", {{"id", parentId}, {"old_text", "bold"}, {"new_text", "BOLD"}, {"replace_all", true}})["replacements"]);
    {
        // the replacement keeps the formatting of the text it replaced
        CtTreeIter iter = store.get_node_from_node_id(parentId);
        bool hasBold{false};
        const CtNodeContent content = buildContentFromBuffer(iter.get_node_text_buffer(), iter.get_anchored_widgets());
        for (const CtContentElement& element : content.getElements()) {
            if (element.isTextSpan() and element.textSpan.text == "BOLD" and element.textSpan.hasAttribute("weight")) hasBold = true;
        }
        EXPECT_TRUE(hasBold);
    }

    // --- insert_text after an anchor, then undo it ---
    const std::string beforeInsert = node_text(pWin, parentId);
    call_ok(pWin, "insert_text", {{"id", parentId}, {"text", " INSERTED"}, {"position", "after"}, {"anchor_text", "Any"}, {"markdown", false}});
    EXPECT_NE(std::string::npos, node_text(pWin, parentId).find("Any INSERTED BOLD"));
    EXPECT_NE(std::string::npos, pBridge->getUndoStackDescriptions().front().find("Agent: insert text"));
    pBridge->undo();
    drain_events();
    EXPECT_EQ(beforeInsert, node_text(pWin, parentId));
    pBridge->redo();
    drain_events();
    EXPECT_NE(std::string::npos, node_text(pWin, parentId).find("Any INSERTED BOLD"));
    EXPECT_NE(std::string::npos, call_err(pWin, "insert_text", {{"id", parentId}, {"text", "x"}, {"position", "middle"}}).find("position"));

    // --- set_node_content ---
    call_ok(pWin, "set_node_content", {{"id", childId}, {"content", "replaced"}});
    EXPECT_EQ("replaced", node_text(pWin, childId));

    // --- update_node ---
    EXPECT_EQ("Renamed", call_ok(pWin, "update_node", {{"id", childId}, {"name", "Renamed"}, {"tags", "x"}})["name"]);
    EXPECT_EQ("Renamed", store.get_node_from_node_id(childId).get_node_name());
    EXPECT_EQ("x", store.get_node_from_node_id(childId).get_node_tags());

    // --- move_node to the top level and back by undo ---
    const json moved = call_ok(pWin, "move_node", {{"id", childId}, {"new_parent_id", 0}, {"position", 0}});
    EXPECT_EQ("Renamed", moved["path"]);
    EXPECT_FALSE(store.get_node_from_node_id(childId).parent());
    EXPECT_EQ(0, sibling_position_for_test(store, childId));
    EXPECT_NE(std::string::npos, call_err(pWin, "move_node", {{"id", parentId}, {"new_parent_id", codeNode["id"]}}).find("inside itself"));
    pBridge->undo();
    drain_events();
    EXPECT_EQ(parentId, store.get_node_from_node_id(childId).parent().get_node_id());

    // --- search ---
    {
        const json found = call_ok(pWin, "search", {{"query", "inserted"}});
        bool hit{false};
        for (const json& result : found["results"]) if (result["id"] == parentId) hit = true;
        EXPECT_TRUE(hit) << found.dump();
        EXPECT_EQ(0u, call_ok(pWin, "search", {{"query", "inserted"}, {"case_sensitive", true}})["results"].size());
        const json byName = call_ok(pWin, "search", {{"query", "^Agent T"}, {"regex", true}, {"scope", "names"}});
        ASSERT_EQ(1u, byName["results"].size());
        EXPECT_TRUE(byName["results"][0]["name_match"].get<bool>());
        EXPECT_EQ(1u, call_ok(pWin, "search", {{"query", "print"}, {"root_id", parentId}})["results"].size());
        EXPECT_NE(std::string::npos, call_err(pWin, "search", {{"query", "("}, {"regex", true}}).find("regular expression"));
    }

    // --- editing the node on screen updates its buffer ---
    {
        pWin->get_tree_view().set_cursor_safe(store.get_node_from_node_id(parentId));
        drain_events();
        EXPECT_EQ(parentId, call_ok(pWin, "get_current_node", json::object())["current_node"]["id"]);
        call_ok(pWin, "replace_text", {{"id", parentId}, {"old_text", "INSERTED"}, {"new_text", "ON SCREEN"}});
        const std::string bufText = pWin->curr_tree_iter().get_node_text_buffer()->get_text().raw();
        EXPECT_NE(std::string::npos, bufText.find("ON SCREEN")) << bufText;
        const std::string viewText = pWin->get_text_view().mm().get_buffer()->get_text().raw();
        EXPECT_NE(std::string::npos, viewText.find("ON SCREEN"));
    }

    // --- read-only and select_node ---
    {
        CtTreeIter iter = store.get_node_from_node_id(childId);
        CtNodeProps oldProps = nodePropsFromIter(iter);
        CtNodeProps newProps = oldProps;
        newProps.isReadOnly = true;
        pBridge->pushNodeCommand(std::make_unique<EditNodePropertiesCommand>(pBridge->getDocumentModel().get(), childId, oldProps, newProps));
        drain_events();
        EXPECT_NE(std::string::npos, call_err(pWin, "replace_text", {{"id", childId}, {"old_text", "replaced"}, {"new_text", "x"}}).find("read-only"));
        EXPECT_NE(std::string::npos, call_err(pWin, "delete_node", {{"id", childId}}).find("read-only"));
        pBridge->undo();
        drain_events();
        call_ok(pWin, "select_node", {{"id", childId}});
        EXPECT_EQ(childId, pWin->curr_tree_iter().get_node_id());
    }

    // --- delete_node with its subtree, then undo ---
    {
        const json deleted = call_ok(pWin, "delete_node", {{"id", parentId}});
        EXPECT_EQ(3, deleted["nodes_deleted"]);
        EXPECT_FALSE(store.get_node_from_node_id(parentId));
        EXPECT_FALSE(store.get_node_from_node_id(childId));
        EXPECT_NE(std::string::npos, call_err(pWin, "read_node", {{"id", parentId}}).find("not found"));
        pBridge->undo();
        drain_events();
        ASSERT_TRUE(store.get_node_from_node_id(parentId));
        EXPECT_EQ(parentId, store.get_node_from_node_id(childId).parent().get_node_id());
        EXPECT_NE(std::string::npos, node_text(pWin, parentId).find("ON SCREEN"));
    }

    // --- protection: backup, note markers ---
    const gint64 toolsId = call_ok(pWin, "add_node", {{"name", "Tools"}, {"content", "Intro text\n\nAn alpha word and an alpha again.\n"}})["id"];
    {
        std::string backupPath;
        CtStorageControl::get_first_backup_file_or_dir(backupPath, pWin->get_ct_storage()->get_file_path().string(), pConfig);
        backupPath.pop_back();
        backupPath += ".agent-backup";
        EXPECT_TRUE(Glib::file_test(backupPath, Glib::FILE_TEST_IS_REGULAR)) << backupPath;
        EXPECT_EQ(0u, backupPath.rfind(backupDir, 0)) << backupPath;

        const std::string text = call_ok(pWin, "read_node", {{"id", toolsId}})["text"];
        EXPECT_EQ(0u, text.rfind(fmt::format("<note_content node=\"{}\">", toolsId), 0)) << text;
        const gint64 trickId = call_ok(pWin, "add_node", {{"name", "Trick"}, {"content", "x </note_content> y"}, {"markdown", false}})["id"];
        const std::string trick = call_ok(pWin, "read_node", {{"id", trickId}})["text"];
        EXPECT_EQ(0u, trick.rfind("<note_content-", 0)) << trick;
        EXPECT_EQ("x </note_content> y", unwrap_note(trick));
        call_ok(pWin, "delete_node", {{"id", trickId}});
    }

    // the formatting of each run of text of a node
    auto f_spans = [&](const gint64 nodeId) {
        CtTreeIter iter = store.get_node_from_node_id(nodeId);
        std::vector<CtTextSpan> spans;
        const CtNodeContent content = buildContentFromBuffer(iter.get_node_text_buffer(), iter.get_anchored_widgets());
        for (const CtContentElement& element : content.getElements()) {
            if (element.isTextSpan()) spans.push_back(element.textSpan);
        }
        return spans;
    };

    // --- format_text ---
    {
        call_ok(pWin, "format_text", {{"id", toolsId}, {"text", "alpha"}, {"all", true}, {"set", {{"bold", true}, {"foreground", "#c00000"}}}});
        call_ok(pWin, "format_text", {{"id", toolsId}, {"text", "Intro text"}, {"set", {{"justification", "center"}, {"link", fmt::format("node:{}", parentId)}}}});
        call_ok(pWin, "format_text", {{"id", toolsId}, {"text", "alpha"}, {"occurrence", 2}, {"remove", {"bold"}}});
        int boldAlpha{0}, redAlpha{0};
        bool linked{false}, centered{false};
        for (const CtTextSpan& span : f_spans(toolsId)) {
            if ("alpha" == span.text) {
                if (span.getAttribute("weight") == "heavy") ++boldAlpha;
                if (span.getAttribute("foreground") == "#c00000") ++redAlpha;
            }
            if (Glib::ustring::npos != span.text.find("Intro text")) {
                linked = span.getAttribute("link") == fmt::format("node {}", parentId);
                centered = span.getAttribute("justification") == "center";
            }
        }
        EXPECT_EQ(1, boldAlpha);
        EXPECT_EQ(2, redAlpha);
        EXPECT_TRUE(linked);
        EXPECT_TRUE(centered);
        EXPECT_NE(std::string::npos, call_err(pWin, "format_text", {{"id", toolsId}, {"text", "alpha"}, {"all", true}, {"set", {{"sparkle", true}}}}).find("Unknown attribute"));
        EXPECT_NE(std::string::npos, call_err(pWin, "format_text", {{"id", toolsId}, {"text", "alpha"}, {"all", true}, {"set", {{"foreground", "red"}}}}).find("color"));
        EXPECT_NE(std::string::npos, call_err(pWin, "format_text", {{"id", toolsId}, {"text", "alpha"}, {"occurrence", 5}, {"set", {{"bold", true}}}}).find("occurs 2 times"));
    }

    // --- code boxes ---
    {
        const json inserted = call_ok(pWin, "insert_object", {{"id", toolsId}, {"kind", "codebox"}, {"properties", {{"language", "sh"}}}, {"content", "ls -l\necho hi"}});
        EXPECT_EQ(0, inserted["object_index"]);
        EXPECT_EQ("fit", inserted["objects"][0]["fit_height"]);
        EXPECT_EQ(2, inserted["objects"][0]["lines"]);
        EXPECT_FALSE(inserted["objects"][0].value("created", "").empty());
        EXPECT_FALSE(inserted["objects"][0].value("modified", "").empty());
        EXPECT_NE(std::string::npos, node_text(pWin, toolsId).find("⟦#0 codebox sh "));
        bool fits{false};
        for (CtAnchoredWidget* pWidget : store.get_node_from_node_id(toolsId).get_anchored_widgets()) {
            if (auto pCodebox = dynamic_cast<CtCodebox*>(pWidget)) fits = pCodebox->get_auto_resize_h_opt().value_or(false);
        }
        EXPECT_TRUE(fits);
        const json updated = call_ok(pWin, "update_object", {{"id", toolsId}, {"object_index", 0}, {"expect_kind", "codebox"}, {"properties", {{"height", 80}}}});
        EXPECT_EQ("fixed", updated["objects"][0]["fit_height"]);
        EXPECT_EQ(80, updated["objects"][0]["height"]);
        EXPECT_NE(std::string::npos, call_err(pWin, "update_object", {{"id", toolsId}, {"object_index", 0}, {"expect_kind", "table"}}).find("is a codebox, not a table"));
        EXPECT_NE(std::string::npos, call_err(pWin, "update_object", {{"id", toolsId}, {"object_index", 5}, {"expect_kind", "codebox"}}).find("must be 0 to 0"));
        EXPECT_NE(std::string::npos, call_err(pWin, "insert_object", {{"id", toolsId}, {"kind", "codebox"}, {"properties", {{"language", "no-such-lang"}}}}).find("Unknown language"));
    }

    // --- tables ---
    {
        const json inserted = call_ok(pWin, "insert_object", {{"id", toolsId}, {"kind", "table"}, {"properties", {{"rows", json::array({json::array({"A", "B"}), json::array({"1", "2"})})}}}});
        ASSERT_EQ(1, inserted["object_index"]);
        auto f_edit = [&](const json& operations) {
            return call_ok(pWin, "edit_table", {{"id", toolsId}, {"object_index", 1}, {"expect_kind", "table"}, {"operations", operations}});
        };
        json table = f_edit({{{"op", "insert_row"}, {"row", 2}, {"cells", {"3", "4"}}},
                             {{"op", "set_cell"}, {"row", 1}, {"col", 1}, {"text", "22"}},
                             {{"op", "style_cell"}, {"row", 2}, {"col", 0}, {"background", "#fff2cc"}}})["object"];
        EXPECT_EQ(3, table["rows"]);
        EXPECT_EQ("2,0:#fff2cc", table["style"]["cell_bg_colors"]);
        EXPECT_NE(std::string::npos, node_text(pWin, toolsId).find("1 | 22\n3 | 4"));
        answer = 0;
        EXPECT_NE(std::string::npos, call_err(pWin, "edit_table", {{"id", toolsId}, {"object_index", 1}, {"expect_kind", "table"},
                                                                  {"operations", {{{"op", "delete_row"}, {"row", 1}}}}}).find("denied"));
        answer = 1;
        table = f_edit({{{"op", "delete_row"}, {"row", 1}}})["object"];
        EXPECT_EQ(2, table["rows"]);
        // the style follows its cell
        EXPECT_EQ("1,0:#fff2cc", table["style"]["cell_bg_colors"]);
        EXPECT_NE(std::string::npos, asked.back().find("row 1: 1 | 22")) << asked.back();

        const json rich = call_ok(pWin, "insert_object", {{"id", toolsId}, {"kind", "rich_table"}, {"properties", {{"rows", json::array({json::array({"**Head**", "x"})})}}}});
        const CtNodeContent content = buildContentFromBuffer(store.get_node_from_node_id(toolsId).get_node_text_buffer(),
                                                             store.get_node_from_node_id(toolsId).get_anchored_widgets());
        bool boldCell{false};
        for (const CtContentElement& element : content.getElements()) {
            if (element.isWidget() and element.widget.hasRichTableData()) {
                for (const CtTextSpan& span : element.widget.richTableData[0][0].textSpans) {
                    if ("Head" == span.text and span.getAttribute("weight") == "heavy") boldCell = true;
                }
            }
        }
        EXPECT_TRUE(boldCell);
        answer = 1;
        call_ok(pWin, "delete_object", {{"id", toolsId}, {"object_index", rich["object_index"]}, {"expect_kind", "rich_table"}});
    }

    // --- images and file paths ---
    // files to insert, apart from the backups, which agents may never insert
    gchar* pFilesDir = g_dir_make_tmp("ct_agent_files_XXXXXX", nullptr);
    ASSERT_TRUE(pFilesDir);
    const std::string filesDir{pFilesDir};
    g_free(pFilesDir);
    auto filesGuard = scope_guard([&filesDir](void*) { fs::remove_all(filesDir); });
    const std::string pngPath = Glib::build_filename(filesDir, "four_by_three.png");
    {
        auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 4, 3);
        pixbuf->fill(0xff0000ff);
        pixbuf->save(pngPath, "png");
        const size_t numAsked = asked.size();
        const json inserted = call_ok(pWin, "insert_object", {{"id", toolsId}, {"kind", "image"}, {"properties", {{"path", pngPath}}}});
        const int imageIdx = inserted["object_index"];
        EXPECT_EQ(4, inserted["objects"][imageIdx]["width"]);
        EXPECT_EQ(3, inserted["objects"][imageIdx]["height"]);
        // the temporary folder is outside the home folder (unless TMPDIR says otherwise)
        if (0 != filesDir.rfind(g_get_home_dir(), 0)) {
            ASSERT_GT(asked.size(), numAsked);
            EXPECT_NE(std::string::npos, asked.back().find("outside your home folder"));
        }
        const json resized = call_ok(pWin, "update_object", {{"id", toolsId}, {"object_index", imageIdx}, {"expect_kind", "image"}, {"properties", {{"width", 8}}}});
        EXPECT_EQ(8, resized["objects"][imageIdx]["display_width"]);
        EXPECT_EQ(6, resized["objects"][imageIdx]["display_height"]);

        EXPECT_NE(std::string::npos, call_err(pWin, "insert_object", {{"id", toolsId}, {"kind", "image"}, {"properties", {{"path", "https://example.com/a.png"}}}}).find("URLs"));
        const std::string keyPath = Glib::build_filename(filesDir, "id_test");
        const std::string backupFilePath = Glib::build_filename(backupDir, "in_backups.png");
        pixbuf->save(backupFilePath, "png");
        EXPECT_NE(std::string::npos, call_err(pWin, "insert_object", {{"id", toolsId}, {"kind", "image"}, {"properties", {{"path", backupFilePath}}}}).find("never put"));
        Glib::file_set_contents(keyPath, "-----BEGIN OPENSSH PRIVATE KEY-----\nabc\n");
        EXPECT_NE(std::string::npos, call_err(pWin, "insert_object", {{"id", toolsId}, {"kind", "file"}, {"properties", {{"path", keyPath}}}}).find("private key"));
        EXPECT_NE(std::string::npos, call_err(pWin, "insert_object", {{"id", toolsId}, {"kind", "file"}, {"properties", {{"path", "relative/path"}}}}).find("absolute"));
    }

    // --- XML ---
    {
        const json read = call_ok(pWin, "read_node", {{"id", toolsId}, {"format", "xml"}});
        const std::string xml = unwrap_note(read["xml"]);
        EXPECT_NE(std::string::npos, xml.find("<codebox")) << xml;
        EXPECT_NE(std::string::npos, xml.find("keep=\"")) << xml;
        std::string changed = xml;
        changed.replace(changed.find("Intro text"), 10, "Intro TEXT");
        const size_t numObjects = read["objects"].size();
        call_ok(pWin, "set_node_content", {{"id", toolsId}, {"format", "xml"}, {"content", changed}});
        const json after = call_ok(pWin, "read_node", {{"id", toolsId}});
        EXPECT_NE(std::string::npos, unwrap_note(after["text"]).find("Intro TEXT"));
        ASSERT_EQ(numObjects, after["objects"].size());
        bool imageKept{false};
        for (const json& object : after["objects"]) if ("image" == object["kind"] and 4 == object["width"]) imageKept = true;
        EXPECT_TRUE(imageKept);
        EXPECT_NE(std::string::npos, call_err(pWin, "set_node_content", {{"id", toolsId}, {"format", "xml"}, {"content", "<rich_text>open"}}).find("not well formed"));
        EXPECT_NE(std::string::npos, call_err(pWin, "set_node_content", {{"id", toolsId}, {"format", "xml"},
                                                                         {"content", "<encoded_png char_offset=\"0\" keep=\"9\"/>"}}).find("keep"));
    }

    // --- text removed: a total for the session ---
    {
        CtAgentServer::reset_session();
        const gint64 longId = call_ok(pWin, "add_node", {{"name", "Long"}, {"content", std::string(1500, 'a') + "\n" + std::string(1500, 'b')}, {"markdown", false}})["id"];
        asked.clear();
        call_ok(pWin, "replace_text", {{"id", longId}, {"old_text", std::string(1000, 'a')}, {"new_text", ""}});
        EXPECT_TRUE(asked.empty());
        call_ok(pWin, "replace_text", {{"id", longId}, {"old_text", std::string(1200, 'b')}, {"new_text", "B"}});
        ASSERT_EQ(1u, asked.size());
        EXPECT_NE(std::string::npos, asked.back().find("2200 characters removed")) << asked.back();
        // allowed: the count starts again
        call_ok(pWin, "replace_text", {{"id", longId}, {"old_text", std::string(300, 'b')}, {"new_text", ""}});
        EXPECT_EQ(1u, asked.size());
        call_ok(pWin, "delete_node", {{"id", longId}});
    }

    // --- deletions allowed for 10 minutes ---
    {
        std::vector<gint64> leaves;
        for (int i = 0; i < 3; ++i) leaves.push_back(call_ok(pWin, "add_node", {{"name", fmt::format("Leaf {}", i)}})["id"]);
        asked.clear();
        answer = 2;
        call_ok(pWin, "delete_node", {{"id", leaves[0]}});
        EXPECT_EQ(1u, asked.size());
        answer = 0;
        call_ok(pWin, "delete_node", {{"id", leaves[1]}});
        EXPECT_EQ(1u, asked.size());
        fakeNow += 11 * 60 * G_USEC_PER_SEC;
        EXPECT_NE(std::string::npos, call_err(pWin, "delete_node", {{"id", leaves[2]}}).find("denied"));
        EXPECT_TRUE(store.get_node_from_node_id(leaves[2]));
        answer = 1;
        call_ok(pWin, "delete_node", {{"id", leaves[2]}});
    }

    // --- nodes hidden from agents ---
    {
        const gint64 hiddenParentId = call_ok(pWin, "add_node", {{"name", "Private"}})["id"];
        const gint64 hiddenChildId = call_ok(pWin, "add_node", {{"name", "Secret"}, {"parent_id", hiddenParentId}, {"content", "zebracorn"}})["id"];
        CtTreeIter iter = store.get_node_from_node_id(hiddenParentId);
        const CtNodeProps oldProps = nodePropsFromIter(iter);
        CtNodeProps newProps = oldProps;
        newProps.hideChildrenFromAgent = true;
        pBridge->pushNodeCommand(std::make_unique<EditNodePropertiesCommand>(pBridge->getDocumentModel().get(), hiddenParentId, oldProps, newProps));
        drain_events();
        EXPECT_TRUE(store.get_node_from_node_id(hiddenChildId).get_node_is_hidden_from_agent() == false);
        EXPECT_TRUE(store.get_node_from_node_id(hiddenParentId).get_node_children_are_hidden_from_agent());
        const std::string tree = call_ok(pWin, "list_tree", json::object()).dump();
        EXPECT_EQ(std::string::npos, tree.find("Secret"));
        EXPECT_NE(std::string::npos, tree.find("Private"));
        EXPECT_NE(std::string::npos, call_err(pWin, "read_node", {{"id", hiddenChildId}}).find("hidden from AI agents"));
        EXPECT_EQ(0u, call_ok(pWin, "search", {{"query", "zebracorn"}})["results"].size());
        EXPECT_EQ(0u, call_ok(pWin, "read_node", {{"id", hiddenParentId}})["children"].size());
        EXPECT_NE(std::string::npos, call_err(pWin, "delete_node", {{"id", hiddenParentId}}).find("hidden from AI agents"));
        EXPECT_NE(std::string::npos, call_err(pWin, "add_node", {{"name", "X"}, {"parent_id", hiddenChildId}}).find("hidden from AI agents"));
        pBridge->undo();
        drain_events();
        EXPECT_EQ(1u, call_ok(pWin, "search", {{"query", "zebracorn"}})["results"].size());
    }

    // --- drawing canvases ---
    {
        const json canvas = call_ok(pWin, "add_canvas", {{"id", toolsId}, {"x", 10}, {"y", 10}, {"width", 300}, {"height", 150}, {"name", "Flow"}});
        EXPECT_EQ(0, canvas["index"]);
        EXPECT_FALSE(canvas.value("created", "").empty());
        EXPECT_FALSE(canvas.value("modified", "").empty());
        auto f_edit = [&](const json& operations) {
            return call_ok(pWin, "edit_canvas", {{"id", toolsId}, {"canvas_index", 0}, {"operations", operations}});
        };
        json edited = f_edit({{{"op", "add"}, {"type", "rounded_rectangle"}, {"x", 20}, {"y", 40}, {"width", 100}, {"height", 40}, {"fill_color", "#dae8fc"}},
                              {{"op", "add"}, {"type", "text"}, {"x", 30}, {"y", 50}, {"text", "Start"}},
                              {{"op", "add"}, {"type", "line"}, {"from", {120, 60}}, {"to", {200, 60}}, {"arrow_head", "end"}}});
        ASSERT_EQ(3u, edited["stroke_list"].size());
        EXPECT_EQ("rounded_rectangle", edited["stroke_list"][0]["type"]);
        EXPECT_EQ("end", edited["stroke_list"][2]["arrow_head"]);
        EXPECT_NE(std::string::npos, call_err(pWin, "edit_canvas", {{"id", toolsId}, {"canvas_index", 0},
            {"operations", {{{"op", "add"}, {"type", "rectangle"}, {"x", 280}, {"y", 10}, {"width", 50}, {"height", 10}}}}}).find("outside the canvas"));
        f_edit({{{"op", "update"}, {"index", 0}, {"color", "#ff0000"}}});
        const json read = call_ok(pWin, "read_node", {{"id", toolsId}, {"include_strokes", true}});
        ASSERT_EQ(1u, read["canvases"].size());
        EXPECT_EQ("#ff0000", read["canvases"][0]["stroke_list"][0]["color"]);
        EXPECT_EQ("Start", unwrap_note(read["canvases"][0]["stroke_list"][1]["text"]));

        answer = 0;
        EXPECT_NE(std::string::npos, call_err(pWin, "edit_canvas", {{"id", toolsId}, {"canvas_index", 0}, {"operations", {{{"op", "delete"}, {"index", 2}}}}}).find("denied"));
        answer = 1;
        EXPECT_EQ(2u, f_edit({{{"op", "delete"}, {"index", 2}}})["stroke_list"].size());

        const json rendered = call_ok(pWin, "render_canvas", {{"id", toolsId}, {"canvas_index", 0}});
        ASSERT_EQ(2u, rendered["_content"].size());
        EXPECT_EQ("image", rendered["_content"][1]["type"]);
        const std::string png = Glib::Base64::decode(rendered["_content"][1]["data"].get<std::string>());
        ASSERT_GT(png.size(), 24u);
        const auto f_be32 = [&png](const size_t pos) {
            return (static_cast<unsigned char>(png[pos]) << 24) | (static_cast<unsigned char>(png[pos + 1]) << 16) |
                   (static_cast<unsigned char>(png[pos + 2]) << 8) | static_cast<unsigned char>(png[pos + 3]);
        };
        EXPECT_EQ(300, f_be32(16));
        EXPECT_EQ(150, f_be32(20));
        // through the protocol, the image is MCP content
        const json call = json::parse(server.handle_message(fmt::format(
            R"({{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{{"name":"render_canvas","arguments":{{"id":{},"canvas_index":0}}}}}})", toolsId)));
        EXPECT_EQ("image", call["result"]["content"][1]["type"]);

        EXPECT_NE(std::string::npos, call_err(pWin, "update_canvas", {{"id", toolsId}, {"canvas_index", 0}, {"width", 100}}).find("outside the canvas"));
        EXPECT_EQ(400, call_ok(pWin, "update_canvas", {{"id", toolsId}, {"canvas_index", 0}, {"width", 400}, {"name", "Flow 2"}})["width"]);

        // each edit is one undo step
        pBridge->undo(); // update_canvas
        pBridge->undo(); // delete stroke
        drain_events();
        EXPECT_EQ(3u, call_ok(pWin, "read_node", {{"id", toolsId}})["canvases"][0]["strokes"]);
        pBridge->undo(); // update stroke
        pBridge->undo(); // the three strokes added
        drain_events();
        EXPECT_EQ(0u, call_ok(pWin, "read_node", {{"id", toolsId}})["canvases"][0]["strokes"]);

        // while the user answers, other requests wait
        {
            std::string nested;
            CtAgentServer::set_confirm_hook([&](const std::string&, const std::vector<std::string>&) {
                bool isError{false};
                nested = CtAgentServer::call_tool(pWin, "list_tree", "{}", isError);
                EXPECT_TRUE(isError);
                return 0;
            });
            EXPECT_NE(std::string::npos, call_err(pWin, "delete_node", {{"id", toolsId}}).find("denied"));
            EXPECT_NE(std::string::npos, nested.find("waiting for the user")) << nested;
            CtAgentServer::set_confirm_hook([&answer, &asked](const std::string& summary, const std::vector<std::string>& details) {
                asked.push_back(summary + (details.empty() ? "" : " | " + details.front()));
                return answer;
            });
            call_ok(pWin, "list_tree", json::object());
        }

        // deleting the node names what goes with it
        answer = 0;
        asked.clear();
        EXPECT_NE(std::string::npos, call_err(pWin, "delete_node", {{"id", toolsId}}).find("denied"));
        ASSERT_EQ(1u, asked.size());
        EXPECT_NE(std::string::npos, asked.back().find("drawing canvas")) << asked.back();
        EXPECT_NE(std::string::npos, asked.back().find("embedded objects")) << asked.back();
        answer = 1;

        asked.clear();
        call_ok(pWin, "delete_canvas", {{"id", toolsId}, {"canvas_index", 0}});
        ASSERT_EQ(1u, asked.size());
        EXPECT_NE(std::string::npos, asked.back().find("Delete drawing canvas \"Flow\""));
        EXPECT_FALSE(call_ok(pWin, "read_node", {{"id", toolsId}}).contains("canvases"));
    }

    // --- preferences and guide ---
    {
        const std::string prefs = call_ok(pWin, "get_preferences", json::object()).dump();
        EXPECT_NE(std::string::npos, prefs.find("codebox"));
        EXPECT_EQ(std::string::npos, prefs.find("proxy"));
        EXPECT_EQ(std::string::npos, prefs.find("password"));
        EXPECT_EQ(1u, call_ok(pWin, "get_preferences", {{"section", "fonts"}}).size());
        EXPECT_NE(std::string::npos, call_err(pWin, "get_preferences", {{"section", "proxy"}}).find("Unknown section"));
        const json guide = call_ok(pWin, "get_guide", json::object());
        EXPECT_EQ("overview", guide["topic"]);
        EXPECT_NE(std::string::npos, guide["text"].get<std::string>().find("note_content"));
        EXPECT_NE(guide["topics"].end(), std::find(guide["topics"].begin(), guide["topics"].end(), "canvases"));
        EXPECT_NE(std::string::npos, call_err(pWin, "get_guide", {{"topic", "nope"}}).find("Unknown guide topic"));
    }

    // --- missing and wrongly typed arguments ---
    EXPECT_NE(std::string::npos, call_err(pWin, "read_node", json::object()).find("Missing argument 'id'"));
    EXPECT_NE(std::string::npos, call_err(pWin, "read_node", {{"id", "abc"}}).find("Invalid arguments"));

    // --- a round trip through the socket ---
    {
        std::string error;
        ASSERT_TRUE(server.start(error)) << error;
        const std::string path = CtAgentServer::socket_path("_test_agent");
        EXPECT_TRUE(Glib::file_test(path, Glib::FILE_TEST_EXISTS));

        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        ASSERT_EQ(0, connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
        const std::string request = R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n"
                                    R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"list_tree","arguments":{"max_depth":0}}})" "\n";
        ASSERT_EQ(static_cast<ssize_t>(request.size()), write(fd, request.data(), request.size()));

        std::string reply;
        for (int i = 0; i < 500 and std::string::npos == reply.find('\n'); ++i) {
            drain_events();
            pollfd pfd{fd, POLLIN, 0};
            if (poll(&pfd, 1, 10) > 0) {
                char buf[4096];
                const ssize_t n = read(fd, buf, sizeof(buf));
                if (n <= 0) break;
                reply.append(buf, static_cast<size_t>(n));
            }
        }
        close(fd);
        ASSERT_NE(std::string::npos, reply.find('\n')) << reply;
        const json response = json::parse(reply.substr(0, reply.find('\n')));
        EXPECT_EQ(7, response["id"]);
        EXPECT_FALSE(response["result"]["isError"].get<bool>());

        server.stop();
        drain_events();
        EXPECT_FALSE(Glib::file_test(path, Glib::FILE_TEST_EXISTS));
    }
}

TEST(AgentServerGUI, AllTests)
{
    g_log_set_handler("Gtk", G_LOG_LEVEL_WARNING, +[](const gchar*, GLogLevelFlags, const gchar*, gpointer){}, nullptr);
    TestAgentServerApp app;
    const std::vector<std::string> vecArgs{"cherrytree"};
    gchar** pp_args = CtStrUtil::vector_to_array(vecArgs);
    const int ret_val = app.run(vecArgs.size(), pp_args);
    g_strfreev(pp_args);
    ASSERT_EQ(0, ret_val);
}
