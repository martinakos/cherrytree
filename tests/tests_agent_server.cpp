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

static std::string node_text(CtMainWin* pWin, const gint64 nodeId)
{
    return call_ok(pWin, "read_node", {{"id", nodeId}}).value("text", "");
}

void TestAgentServerApp::_run_tests(CtMainWin* pWin)
{
    CtCommandBridge* pBridge = pWin->get_command_bridge();
    ASSERT_TRUE(pBridge and pBridge->isActive());
    CtTreeStore& store = pWin->get_tree_store();
    CtAgentServer& server = CtAgentServer::instance();

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
        ASSERT_EQ(12u, list["result"]["tools"].size());
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
        EXPECT_NE(std::string::npos, text.find("\u27e6codebox python\u27e7")) << text;
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
        const std::string expected =
            "How it works\n"
            "1. The agent (Claude Code). It starts.\n"
            "• choose Tools > Allow/Deny. Then\n"
            "\t◇ under /run/user/<id>/ (usually) and snake_case_name\n"
            "☐ a task with a link and https://example.org\n"
            "\n"
            "⟦table⟧\nTool | What it does\nlist_tree | List (all) the nodes\nread_node | Read | a node\n⟦/table⟧\n"
            "\n"
            "Ünïcödé ⟦ text\n"
            "⟦codebox plain-text⟧\na  ->  b\n⟦/codebox⟧\n"
            "\n"
            "⟦codebox sh⟧\nclaude mcp add x -- /p --mcp-stdio\n⟦/codebox⟧\n"
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
