/*
 * ct_agent_server.cc
 *
 * Copyright 2009-2025
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_agent_server.h"
#include "ct_main_win.h"
#include "ct_actions.h"
#include "ct_command_bridge.h"
#include "ct_node_commands.h"
#include "ct_text_commands.h"
#include "ct_parser.h"
#include "ct_protected_area.h"
#include "ct_storage_control.h"
#include "ct_const.h"
#include "ct_logging.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <ctime>
#include <functional>

#ifndef _WIN32
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace {

const char* const APP_ID_BASE{"net.giuspen.cherrytree_docmodel"};
const char* const MCP_PROTOCOL_LATEST{"2025-06-18"};

const char* const AGENT_INSTRUCTIONS{
    "These tools work on the document open in the running CherryTree window. Changes are live: "
    "they show at once, the user can undo each one (Ctrl+Z), and they are saved with the document.\n"
    "- A document is a tree of nodes, each with a numeric id. parent_id 0 is the top level.\n"
    "- Node types: rich_text (formatted text, write Markdown), plain_text, or a code language id such as python.\n"
    "- Markdown that works: # headings, **bold**, _italic_ (not *italic*), `code`, ``` code blocks, "
    "- lists, [links](url), | tables |.\n"
    "- Call read_node before editing a node. The text tools match exact text, so copy it from read_node.\n"
    "- Embedded objects (images, code boxes, tables) show as ⟦...⟧ blocks; the text tools cannot change them.\n"
    "- To find notes about an idea, call search several times with different keywords and synonyms, "
    "then read the candidates.\n"
    "- Use select_node to show the user a node."};

class CtAgentError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct CtAgentCtx {
    CtMainWin*       pWin;
    CtTreeStore&     store;
    CtCommandBridge* pBridge;
    CtDocumentModel* pModel;
};

CtAgentCtx make_ctx(CtMainWin* pWin)
{
    CtCommandBridge* pBridge = pWin->get_command_bridge();
    if (not pBridge or not pBridge->isActive()) {
        throw CtAgentError{"No document is open in CherryTree"};
    }
    return CtAgentCtx{pWin, pWin->get_tree_store(), pBridge, pBridge->getDocumentModel().get()};
}

template<typename T> T arg_req(const json& args, const char* key)
{
    auto it = args.find(key);
    if (it == args.end() or it->is_null()) {
        throw CtAgentError{fmt::format("Missing argument '{}'", key)};
    }
    return it->get<T>();
}

template<typename T> T arg_opt(const json& args, const char* key, const T& defaultValue)
{
    auto it = args.find(key);
    if (it == args.end() or it->is_null()) {
        return defaultValue;
    }
    return it->get<T>();
}

std::string type_from_syntax(const std::string& syntax)
{
    if (CtConst::RICH_TEXT_ID == syntax) return "rich_text";
    if (CtConst::PLAIN_TEXT_ID == syntax) return "plain_text";
    return syntax;
}

std::string syntax_from_type(CtMainWin* pWin, const std::string& type)
{
    if ("rich_text" == type) return CtConst::RICH_TEXT_ID;
    if ("plain_text" == type) return CtConst::PLAIN_TEXT_ID;
    if (not gtk_source_language_manager_get_language(pWin->get_language_manager(), type.c_str())) {
        throw CtAgentError{fmt::format("Unknown node type '{}': use rich_text, plain_text or a code language id such as python", type)};
    }
    return type;
}

std::string format_time(const gint64 unixTime)
{
    if (unixTime <= 0) return "";
    GDateTime* pDateTime = g_date_time_new_from_unix_local(unixTime);
    if (not pDateTime) return "";
    gchar* pStr = g_date_time_format(pDateTime, "%Y-%m-%d %H:%M");
    std::string ret{pStr ? pStr : ""};
    g_free(pStr);
    g_date_time_unref(pDateTime);
    return ret;
}

CtTreeIter node_iter(CtAgentCtx& ctx, const gint64 nodeId)
{
    CtTreeIter iter = ctx.store.get_node_from_node_id(nodeId);
    if (not iter) {
        throw CtAgentError{fmt::format("Node {} not found", nodeId)};
    }
    return iter;
}

gint64 parent_id(const CtTreeIter& iter)
{
    CtTreeIter parentIter = iter.parent();
    return parentIter ? parentIter.get_node_id() : 0;
}

int sibling_position(const CtTreeIter& iter)
{
    int pos{0};
    Gtk::TreeModel::iterator tmp = iter;
    while (--tmp) ++pos;
    return pos;
}

std::string node_path(CtTreeIter iter)
{
    std::string path;
    for (; iter; iter = iter.parent()) {
        path = path.empty() ? iter.get_node_name().raw() : iter.get_node_name().raw() + " / " + path;
    }
    return path;
}

bool is_locked(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    return ctx.pWin->get_protected_areas().is_locked(iter.get_node_id());
}

void check_unlocked(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    if (is_locked(ctx, iter)) {
        throw CtAgentError{fmt::format("Node {} is a locked password protected area: the user must unlock it first", iter.get_node_id())};
    }
}

bool is_shared(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    if (iter.get_node_shared_master_id() > 0) return true;
    CtSharedNodesMap sharedNodesMap;
    ctx.store.populate_shared_nodes_map(sharedNodesMap);
    return sharedNodesMap.count(iter.get_node_id()) > 0;
}

void check_idle(CtAgentCtx& ctx)
{
    if (ctx.pBridge->isInUndoRedo() or ctx.pBridge->isSuppressingTextEdits()) {
        throw CtAgentError{"CherryTree is busy (undo, redo or a timeline replay): try again in a moment"};
    }
}

bool is_current(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    CtTreeIter currIter = ctx.pWin->curr_tree_iter();
    return currIter and currIter.get_node_id() == iter.get_node_id();
}

// The content of a node as it is now. It comes from the buffer: the model may not hold
// the content of a node that was never loaded, and the buffer has the latest typing.
CtNodeContent node_content(const CtTreeIter& iter)
{
    return buildContentFromBuffer(iter.get_node_text_buffer(), iter.get_anchored_widgets());
}

// The content of a node that is about to be edited
CtNodeContent content_for_edit(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    check_idle(ctx);
    check_unlocked(ctx, iter);
    if (iter.get_node_read_only()) {
        throw CtAgentError{fmt::format("Node {} is read-only", iter.get_node_id())};
    }
    if (is_shared(ctx, iter)) {
        throw CtAgentError{fmt::format("Node {} is a shared node: editing shared nodes is not supported", iter.get_node_id())};
    }
    if (is_current(ctx, iter)) {
        // what the user typed so far goes on the undo stack before the agent's edit
        ctx.pBridge->endTextEditSession();
    }
    return node_content(iter);
}

template<typename CELLS> std::string join_cells(const CELLS& cells)
{
    std::string row;
    for (const auto& cell : cells) {
        if (not row.empty()) row += " | ";
        row += Glib::ustring{cell}.raw();
    }
    return row;
}

// The text of a node as the agent sees it: each embedded object is written as a ⟦…⟧ block
std::string render_text(const CtNodeContent& content)
{
    std::string out;
    for (const CtContentElement& element : content.getElements()) {
        if (element.isTextSpan()) {
            out += element.textSpan.text.raw();
            continue;
        }
        const CtWidgetDesc& widget = element.widget;
        switch (widget.type) {
            case CtAnchWidgType::CodeBox:
                out += "⟦codebox " + widget.getSyntaxHighlighting() + "⟧\n" + widget.getContent() + "\n⟦/codebox⟧";
                break;
            case CtAnchWidgType::TableHeavy:
            case CtAnchWidgType::TableLight:
            case CtAnchWidgType::TableRich:
                out += "⟦table⟧\n";
                if (widget.hasRichTableData()) {
                    for (const auto& row : widget.richTableData) {
                        std::vector<Glib::ustring> cells;
                        for (const CtCellContent& cell : row) cells.push_back(cell.getPlainText());
                        out += join_cells(cells) + "\n";
                    }
                }
                else {
                    for (const auto& row : widget.tableData) out += join_cells(row) + "\n";
                }
                out += "⟦/table⟧";
                break;
            case CtAnchWidgType::ImageLatex:
                out += "⟦latex⟧" + widget.getContent() + "⟦/latex⟧";
                break;
            case CtAnchWidgType::ImageAnchor:
                out += "⟦anchor " + widget.getAnchorName() + "⟧";
                break;
            case CtAnchWidgType::ImageEmbFile:
                out += "⟦file " + widget.getFileName() + "⟧";
                break;
            case CtAnchWidgType::ImagePng:
                out += "⟦image⟧";
                break;
            default:
                out += "⟦object⟧";
                break;
        }
    }
    return out;
}

// Character offsets of every occurrence of needle in haystack, not overlapping
std::vector<int> find_all(const Glib::ustring& haystack, const Glib::ustring& needle)
{
    std::vector<int> offsets;
    if (needle.empty()) return offsets;
    for (auto pos = haystack.find(needle); Glib::ustring::npos != pos; pos = haystack.find(needle, pos + needle.size())) {
        offsets.push_back(static_cast<int>(pos));
    }
    return offsets;
}

int find_unique(const Glib::ustring& haystack, const Glib::ustring& needle, const char* argName)
{
    const std::vector<int> offsets = find_all(haystack, needle);
    if (offsets.empty()) {
        throw CtAgentError{fmt::format("{} not found in the node: copy it exactly from read_node, "
                                       "it cannot span an embedded ⟦...⟧ object", argName)};
    }
    if (offsets.size() > 1u) {
        throw CtAgentError{fmt::format("{} occurs {} times in the node: include more of the "
                                       "surrounding text so that it is unique", argName, offsets.size())};
    }
    return offsets.front();
}

CtNodeContent content_from_markdown(CtMainWin* pWin, const std::string& markdown)
{
    CtMDParser parser{pWin->get_ct_config()};
    parser.feed(markdown);
    if (not parser.last_error().empty()) {
        throw CtAgentError{"The Markdown could not be converted: " + parser.last_error()};
    }
    const std::string xml = parser.doc().document()->write_to_string();
    // the builder writes <root><slot>...</slot></root>, CtNodeContent::fromXml reads <node>...</node>
    const size_t begin = xml.find("<slot>");
    const size_t end = xml.rfind("</slot>");
    if (std::string::npos == begin or std::string::npos == end) {
        return CtNodeContent{};
    }
    return CtNodeContent::fromXml("<node>" + xml.substr(begin + 6, end - begin - 6) + "</node>", pWin);
}

// Content to put in a node: Markdown in a rich text node unless markdown is false, else the text as is
CtNodeContent content_from_text(CtMainWin* pWin, const bool isRichText, const std::string& text, const bool markdown)
{
    if (isRichText and markdown) {
        return content_from_markdown(pWin, text);
    }
    CtNodeContent content;
    if (not text.empty()) {
        content.insertText(0, text, {});
    }
    return content;
}

void splice_content(CtNodeContent& content, const int offset, const CtNodeContent& inserted)
{
    CtDeletedContent toInsert{offset};
    toInsert.elements = inserted.getElements();
    content.reinsertContent(toInsert);
}

// Runs cmd as one undo step, labelled as the agent's
void push_command(CtAgentCtx& ctx,
                  std::unique_ptr<CtCommand> cmd,
                  const gint64 nodeId,
                  const std::string& what,
                  const int cursorPos = -1)
{
    auto compound = std::make_unique<CompoundCommand>("[" + std::to_string(nodeId) + "] Agent: " + what);
    compound->setNodeId(nodeId);
    compound->setOldCursorPos(cursorPos);
    compound->setNewCursorPos(cursorPos);
    compound->addCommand(std::move(cmd));
    ctx.pBridge->pushNodeCommand(std::move(compound));
}

void commit_content(CtAgentCtx& ctx,
                    CtTreeIter iter,
                    CtNodeContent oldContent,
                    CtNodeContent newContent,
                    const int cursorPos,
                    const std::string& what)
{
    if (oldContent == newContent) return;
    const gint64 nodeId = iter.get_node_id();
    if (is_current(ctx, iter)) {
        // the node on screen is rebuilt from the model only when a cursor position is pending
        ctx.pBridge->setPendingCursorPos(std::max(0, cursorPos));
        auto adj = ctx.pWin->getScrolledwindowText().get_vadjustment();
        if (adj) ctx.pBridge->setPendingScrollPos(adj->get_value());
    }
    push_command(ctx,
                 std::make_unique<TextEditCommand>(ctx.pBridge->getDocumentModel(), nodeId,
                                                   std::move(oldContent), std::move(newContent),
                                                   cursorPos, cursorPos),
                 nodeId, what, cursorPos);
    ctx.pWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, false/*new_machine_state*/, &iter);
}

json node_summary(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    json node{{"id", iter.get_node_id()},
              {"name", iter.get_node_name().raw()},
              {"type", type_from_syntax(iter.get_node_syntax_highlighting())}};
    const std::string tags = iter.get_node_tags().raw();
    if (not tags.empty()) node["tags"] = tags;
    if (iter.get_node_read_only()) node["read_only"] = true;
    if (is_locked(ctx, iter)) node["locked"] = true;
    if (iter.get_node_shared_master_id() > 0) node["shared_with"] = iter.get_node_shared_master_id();
    int numChildren{0};
    for (CtTreeIter child = iter.first_child(); child; ++child) ++numChildren;
    if (numChildren > 0) node["children"] = numChildren;
    return node;
}

json node_summary_with_path(CtAgentCtx& ctx, const CtTreeIter& iter)
{
    json node = node_summary(ctx, iter);
    node["path"] = node_path(iter);
    return node;
}

// ─── Tools ────────────────────────────────────────────────────────────────────

json tool_get_current_node(CtAgentCtx& ctx, const json& /*args*/)
{
    json ret;
    CtStorageControl* pStorage = ctx.pWin->get_ct_storage();
    if (pStorage and not pStorage->get_file_path().empty()) ret["document"] = pStorage->get_file_path().string();
    else ret["document"] = nullptr;
    CtTreeIter currIter = ctx.pWin->curr_tree_iter();
    if (currIter) ret["current_node"] = node_summary_with_path(ctx, currIter);
    else ret["current_node"] = nullptr;
    return ret;
}

json tool_list_tree(CtAgentCtx& ctx, const json& args)
{
    const gint64 rootId = arg_opt<gint64>(args, "root_id", 0);
    const int maxDepth = arg_opt<int>(args, "max_depth", -1);
    json nodes = json::array();
    std::function<void(CtTreeIter, int, bool)> f_add;
    f_add = [&](CtTreeIter iter, const int depth, const bool withSiblings) {
        for (; iter; ++iter) {
            json node = node_summary(ctx, iter);
            node["depth"] = depth;
            node["parent_id"] = parent_id(iter);
            nodes.push_back(std::move(node));
            if (maxDepth < 0 or depth < maxDepth) {
                f_add(iter.first_child(), depth + 1, true);
            }
            if (not withSiblings) break;
        }
    };
    if (0 == rootId) f_add(ctx.store.get_ct_iter_first(), 0, true);
    else f_add(node_iter(ctx, rootId), 0, false);
    const size_t count = nodes.size();
    return json{{"nodes", std::move(nodes)}, {"count", count}};
}

json tool_read_node(CtAgentCtx& ctx, const json& args)
{
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    check_unlocked(ctx, iter);
    json node = node_summary_with_path(ctx, iter);
    node["parent_id"] = parent_id(iter);
    json children = json::array();
    for (CtTreeIter child = iter.first_child(); child; ++child) {
        children.push_back(json{{"id", child.get_node_id()}, {"name", child.get_node_name().raw()}});
    }
    node["children"] = std::move(children);
    node["created"] = format_time(iter.get_node_creating_time());
    node["modified"] = format_time(iter.get_node_modification_time());
    node["text"] = render_text(node_content(iter));
    return node;
}

json tool_search(CtAgentCtx& ctx, const json& args)
{
    const std::string query = arg_req<std::string>(args, "query");
    const bool useRegex = arg_opt<bool>(args, "regex", false);
    const bool caseSensitive = arg_opt<bool>(args, "case_sensitive", false);
    const std::string scope = arg_opt<std::string>(args, "scope", "all");
    const gint64 rootId = arg_opt<gint64>(args, "root_id", 0);
    const size_t maxResults = arg_opt<size_t>(args, "max_results", 50);
    const bool includeExcluded = arg_opt<bool>(args, "include_excluded", false);
    if (query.empty()) throw CtAgentError{"query cannot be empty"};
    if ("all" != scope and "names" != scope and "content" != scope) {
        throw CtAgentError{"scope must be one of all, names, content"};
    }

    std::unique_ptr<GRegex, decltype(&g_regex_unref)> uRegex{nullptr, g_regex_unref};
    if (useRegex) {
        GError* pError{nullptr};
        uRegex.reset(g_regex_new(query.c_str(),
                                 caseSensitive ? static_cast<GRegexCompileFlags>(0) : G_REGEX_CASELESS,
                                 static_cast<GRegexMatchFlags>(0), &pError));
        if (not uRegex) {
            const std::string message{pError ? pError->message : ""};
            if (pError) g_error_free(pError);
            throw CtAgentError{"Invalid regular expression: " + message};
        }
    }
    const Glib::ustring queryFolded = caseSensitive ? Glib::ustring{query} : Glib::ustring{query}.casefold();
    auto f_match = [&](const Glib::ustring& text) -> bool {
        if (uRegex) return g_regex_match(uRegex.get(), text.c_str(), static_cast<GRegexMatchFlags>(0), nullptr);
        return Glib::ustring::npos != (caseSensitive ? text : text.casefold()).find(queryFolded);
    };

    json results = json::array();
    bool truncated{false};
    std::function<void(CtTreeIter, bool)> f_walk;
    f_walk = [&](CtTreeIter iter, const bool withSiblings) {
        for (; iter and not truncated; ++iter) {
            if (is_locked(ctx, iter)) {
                if (not withSiblings) break;
                continue;
            }
            if (includeExcluded or not iter.get_node_is_excluded_from_search()) {
                const bool nameMatch = "content" != scope and f_match(iter.get_node_name());
                json matches = json::array();
                size_t matchCount{0};
                if ("names" != scope) {
                    const std::string text = render_text(node_content(iter));
                    size_t lineNum{0};
                    size_t lineStart{0};
                    while (lineStart <= text.size()) {
                        size_t lineEnd = text.find('\n', lineStart);
                        if (std::string::npos == lineEnd) lineEnd = text.size();
                        const Glib::ustring line{text.substr(lineStart, lineEnd - lineStart)};
                        ++lineNum;
                        if (f_match(line)) {
                            ++matchCount;
                            if (matches.size() < 5u) {
                                matches.push_back(json{{"line", lineNum}, {"text", line.substr(0, 200).raw()}});
                            }
                        }
                        lineStart = lineEnd + 1;
                    }
                }
                if (nameMatch or matchCount > 0) {
                    if (results.size() >= maxResults) {
                        truncated = true;
                        break;
                    }
                    json result{{"id", iter.get_node_id()}, {"name", iter.get_node_name().raw()}, {"path", node_path(iter)}};
                    if (nameMatch) result["name_match"] = true;
                    if (matchCount > 0) {
                        result["match_count"] = matchCount;
                        result["matches"] = std::move(matches);
                    }
                    results.push_back(std::move(result));
                }
            }
            if (includeExcluded or not iter.get_node_children_are_excluded_from_search()) {
                f_walk(iter.first_child(), true);
            }
            if (not withSiblings) break;
        }
    };
    if (0 == rootId) f_walk(ctx.store.get_ct_iter_first(), true);
    else f_walk(node_iter(ctx, rootId), false);
    return json{{"results", std::move(results)}, {"truncated", truncated}};
}

json tool_select_node(CtAgentCtx& ctx, const json& args)
{
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    ctx.pWin->get_tree_view().set_cursor_safe(iter);
    return node_summary_with_path(ctx, iter);
}

json tool_add_node(CtAgentCtx& ctx, const json& args)
{
    check_idle(ctx);
    const std::string name = arg_req<std::string>(args, "name");
    const gint64 parentId = arg_opt<gint64>(args, "parent_id", 0);
    int position = arg_opt<int>(args, "position", -1);
    const std::string syntax = syntax_from_type(ctx.pWin, arg_opt<std::string>(args, "type", "rich_text"));
    const std::string tags = arg_opt<std::string>(args, "tags", "");
    const std::string text = arg_opt<std::string>(args, "content", "");
    const bool markdown = arg_opt<bool>(args, "markdown", true);
    if (name.empty()) throw CtAgentError{"The node name cannot be empty"};

    if (0 != parentId) {
        check_unlocked(ctx, node_iter(ctx, parentId));
    }
    auto pParentModel = 0 == parentId ? ctx.pModel->getRootNode() : ctx.pModel->getNodeById(parentId);
    if (not pParentModel) {
        throw CtAgentError{fmt::format("Node {} not found", parentId)};
    }
    const int numChildren = static_cast<int>(pParentModel->getChildren().size());
    if (position < 0 or position > numChildren) position = numChildren;

    CtNodeData nodeData{};
    nodeData.nodeId = ctx.store.node_id_get();
    nodeData.name = name;
    nodeData.syntax = syntax;
    nodeData.tags = tags;
    nodeData.tsCreation = std::time(nullptr);
    nodeData.tsLastSave = nodeData.tsCreation;
    const CtNodeContent content = content_from_text(ctx.pWin, CtConst::RICH_TEXT_ID == syntax, text, markdown);
    push_command(ctx,
                 std::make_unique<AddNodeCommand>(ctx.pModel, nodeData.nodeId, parentId, position,
                                                  nodePropsFromData(nodeData), content),
                 nodeData.nodeId, "add node");

    CtTreeIter newIter = ctx.store.get_node_from_node_id(nodeData.nodeId);
    if (not newIter) {
        throw CtAgentError{"The node could not be added"};
    }
    ctx.store.nodes_sequences_fix(static_cast<Gtk::TreeModel::iterator>(newIter)->parent(), false);
    ctx.pWin->update_window_save_needed(CtSaveNeededUpdType::None, false/*new_machine_state*/, &newIter);
    return node_summary_with_path(ctx, newIter);
}

json tool_move_node(CtAgentCtx& ctx, const json& args)
{
    check_idle(ctx);
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    const gint64 nodeId = iter.get_node_id();
    const gint64 newParentId = arg_req<gint64>(args, "new_parent_id");
    int position = arg_opt<int>(args, "position", -1);
    check_unlocked(ctx, iter);

    CtTreeIter newParentIter;
    if (0 != newParentId) {
        newParentIter = node_iter(ctx, newParentId);
        check_unlocked(ctx, newParentIter);
        for (CtTreeIter upIter = newParentIter; upIter; upIter = upIter.parent()) {
            if (upIter.get_node_id() == nodeId) {
                throw CtAgentError{"A node cannot be moved inside itself"};
            }
        }
    }
    CtProtectedAreas& areas = ctx.pWin->get_protected_areas();
    if (areas.has_any()) {
        const gint64 sourceAreaId = areas.enclosing_area_id(iter);
        const gint64 targetAreaId = newParentIter ? areas.enclosing_area_id(newParentIter) : 0;
        if (sourceAreaId != targetAreaId) {
            throw CtAgentError{"Moving a node into or out of a password protected area changes whether it is "
                               "encrypted: the user must do this in CherryTree"};
        }
    }

    const gint64 oldParentId = parent_id(iter);
    const int oldPos = sibling_position(iter);
    auto pNewParentModel = 0 == newParentId ? ctx.pModel->getRootNode() : ctx.pModel->getNodeById(newParentId);
    if (not pNewParentModel) {
        throw CtAgentError{fmt::format("Node {} not found", newParentId)};
    }
    // the position counts the siblings without the moved node
    int numChildren = static_cast<int>(pNewParentModel->getChildren().size());
    if (oldParentId == newParentId) --numChildren;
    if (position < 0 or position > numChildren) position = numChildren;
    if (oldParentId == newParentId and position == oldPos) {
        return node_summary_with_path(ctx, iter);
    }

    push_command(ctx,
                 std::make_unique<MoveNodeCommand>(ctx.pModel, nodeId, oldParentId, oldPos, newParentId, position),
                 nodeId, "move node");
    return node_summary_with_path(ctx, node_iter(ctx, nodeId));
}

json tool_delete_node(CtAgentCtx& ctx, const json& args)
{
    check_idle(ctx);
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    const gint64 nodeId = iter.get_node_id();
    if (iter.get_node_read_only()) {
        throw CtAgentError{fmt::format("Node {} is read-only", nodeId)};
    }
    // a locked area holds content nobody can see, so it is never thrown away from here
    int numNodes{0};
    std::function<void(CtTreeIter)> f_count;
    f_count = [&](CtTreeIter subIter) {
        check_unlocked(ctx, subIter);
        ++numNodes;
        for (CtTreeIter child = subIter.first_child(); child; ++child) f_count(child);
    };
    f_count(iter);

    const std::string name = iter.get_node_name().raw();
    push_command(ctx, ctx.pWin->get_ct_actions()->node_delete_build_command(iter), nodeId, "delete node");
    ctx.pWin->menu_set_bookmark_menu_items();
    ctx.pWin->get_timeline_panel()->refresh_names();
    return json{{"deleted_id", nodeId}, {"name", name}, {"nodes_deleted", numNodes}};
}

json tool_update_node(CtAgentCtx& ctx, const json& args)
{
    check_idle(ctx);
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    const gint64 nodeId = iter.get_node_id();
    check_unlocked(ctx, iter);
    if (is_shared(ctx, iter)) {
        throw CtAgentError{fmt::format("Node {} is a shared node: editing shared nodes is not supported", nodeId)};
    }
    const CtNodeProps oldProps = nodePropsFromIter(iter);
    CtNodeProps newProps = oldProps;
    if (args.contains("name")) {
        const std::string name = arg_req<std::string>(args, "name");
        if (name.empty()) throw CtAgentError{"The node name cannot be empty"};
        newProps.name = name;
    }
    if (args.contains("tags")) {
        newProps.tags = arg_req<std::string>(args, "tags");
    }
    if (newProps.name != oldProps.name or newProps.tags != oldProps.tags) {
        push_command(ctx, std::make_unique<EditNodePropertiesCommand>(ctx.pModel, nodeId, oldProps, newProps),
                     nodeId, "edit node properties");
    }
    return node_summary_with_path(ctx, node_iter(ctx, nodeId));
}

json tool_replace_text(CtAgentCtx& ctx, const json& args)
{
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    const Glib::ustring oldText = arg_req<std::string>(args, "old_text");
    const Glib::ustring newText = arg_opt<std::string>(args, "new_text", "");
    const bool replaceAll = arg_opt<bool>(args, "replace_all", false);
    if (oldText.empty()) throw CtAgentError{"old_text cannot be empty"};

    CtNodeContent oldContent = content_for_edit(ctx, iter);
    const Glib::ustring text = oldContent.getText();
    std::vector<int> offsets;
    if (replaceAll) {
        offsets = find_all(text, oldText);
        if (offsets.empty()) (void)find_unique(text, oldText, "old_text"); // throws the not found error
    }
    else {
        offsets.push_back(find_unique(text, oldText, "old_text"));
    }

    CtNodeContent newContent = oldContent;
    const int oldLength = static_cast<int>(oldText.size());
    for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
        // the new text takes the formatting of the first character it replaces
        std::map<std::string, std::string> attributes;
        const auto location = newContent.getElementAtOffset(*it);
        if (location.valid and location.elementIndex < newContent.getElements().size() and
            newContent.getElements()[location.elementIndex].isTextSpan())
        {
            attributes = newContent.getElements()[location.elementIndex].textSpan.attributes;
        }
        newContent.deleteRange(*it, oldLength);
        if (not newText.empty()) newContent.insertText(*it, newText, attributes);
    }
    const int cursorPos = offsets.front() + static_cast<int>(newText.size());
    commit_content(ctx, iter, std::move(oldContent), std::move(newContent), cursorPos, "replace text");
    return json{{"id", iter.get_node_id()}, {"replacements", offsets.size()}};
}

json tool_insert_text(CtAgentCtx& ctx, const json& args)
{
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    const std::string text = arg_req<std::string>(args, "text");
    const std::string where = arg_opt<std::string>(args, "position", "end");
    const bool markdown = arg_opt<bool>(args, "markdown", true);
    if (text.empty()) throw CtAgentError{"text cannot be empty"};

    CtNodeContent oldContent = content_for_edit(ctx, iter);
    int offset{0};
    if ("end" == where) {
        offset = static_cast<int>(oldContent.length());
    }
    else if ("after" == where or "before" == where) {
        const Glib::ustring anchorText = arg_req<std::string>(args, "anchor_text");
        offset = find_unique(oldContent.getText(), anchorText, "anchor_text");
        if ("after" == where) offset += static_cast<int>(anchorText.size());
    }
    else if ("start" != where) {
        throw CtAgentError{"position must be one of end, start, after, before"};
    }

    const CtNodeContent inserted = content_from_text(ctx.pWin, iter.get_node_is_rich_text(), text, markdown);
    CtNodeContent newContent = oldContent;
    splice_content(newContent, offset, inserted);
    const int length = static_cast<int>(inserted.length());
    commit_content(ctx, iter, std::move(oldContent), std::move(newContent), offset + length, "insert text");
    return json{{"id", iter.get_node_id()}, {"inserted_at", offset}, {"length", length}};
}

json tool_set_node_content(CtAgentCtx& ctx, const json& args)
{
    CtTreeIter iter = node_iter(ctx, arg_req<gint64>(args, "id"));
    const std::string text = arg_req<std::string>(args, "content");
    const bool markdown = arg_opt<bool>(args, "markdown", true);

    CtNodeContent oldContent = content_for_edit(ctx, iter);
    CtNodeContent newContent = content_from_text(ctx.pWin, iter.get_node_is_rich_text(), text, markdown);
    const int length = static_cast<int>(newContent.length());
    commit_content(ctx, iter, std::move(oldContent), std::move(newContent), 0, "set node content");
    return json{{"id", iter.get_node_id()}, {"length", length}};
}

struct CtAgentTool {
    const char* name;
    const char* description;
    const char* inputSchema;
    json (*run)(CtAgentCtx&, const json&);
};

const std::vector<CtAgentTool>& agent_tools()
{
    static const std::vector<CtAgentTool> tools{
        {"get_current_node",
         "Get the open document's file path and the node the user is looking at now.",
         R"({"type":"object","properties":{}})",
         &tool_get_current_node},
        {"list_tree",
         "List the nodes of the tree in order, depth first. Each node has id, name, type, depth, parent_id "
         "and, when present, tags, read_only, locked (a locked password protected area) and children (count).",
         R"({"type":"object","properties":{
            "root_id":{"type":"integer","description":"List only this node and its subtree. Default 0: the whole tree."},
            "max_depth":{"type":"integer","description":"Deepest level to list, 0 for the top level only. Default: all levels."}}})",
         &tool_list_tree},
        {"read_node",
         "Read a node: its properties, children and full text. Embedded objects show as ⟦...⟧ blocks.",
         R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})",
         &tool_read_node},
        {"search",
         "Search node names and text in the whole tree or a subtree. Returns the matching nodes with up to "
         "5 matching lines each. For a topic rather than an exact phrase, search several related keywords.",
         R"({"type":"object","properties":{
            "query":{"type":"string","description":"Text to find, or a regular expression when regex is true."},
            "regex":{"type":"boolean","description":"Default false."},
            "case_sensitive":{"type":"boolean","description":"Default false."},
            "scope":{"type":"string","enum":["all","names","content"],"description":"Default all."},
            "root_id":{"type":"integer","description":"Search only this node and its subtree. Default 0: the whole tree."},
            "max_results":{"type":"integer","description":"Default 50."},
            "include_excluded":{"type":"boolean","description":"Also search nodes the user excluded from search. Default false."}},
          "required":["query"]})",
         &tool_search},
        {"select_node",
         "Select a node in the CherryTree window so that the user sees it.",
         R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})",
         &tool_select_node},
        {"add_node",
         "Add a new node. Content is Markdown for a rich_text node (headings, bold, lists, code, tables) "
         "unless markdown is false; for other types it is the text as is.",
         R"({"type":"object","properties":{
            "name":{"type":"string"},
            "parent_id":{"type":"integer","description":"Default 0: top level."},
            "position":{"type":"integer","description":"Index among the parent's children. Default: last."},
            "type":{"type":"string","description":"rich_text (default), plain_text, or a code language id such as python."},
            "tags":{"type":"string","description":"Space separated tags."},
            "content":{"type":"string"},
            "markdown":{"type":"boolean","description":"Default true."}},
          "required":["name"]})",
         &tool_add_node},
        {"move_node",
         "Move a node, with its subtree, under a new parent.",
         R"({"type":"object","properties":{
            "id":{"type":"integer"},
            "new_parent_id":{"type":"integer","description":"0 for the top level."},
            "position":{"type":"integer","description":"Index among the new parent's other children. Default: last."}},
          "required":["id","new_parent_id"]})",
         &tool_move_node},
        {"delete_node",
         "Delete a node and its whole subtree.",
         R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})",
         &tool_delete_node},
        {"update_node",
         "Rename a node or change its tags.",
         R"({"type":"object","properties":{
            "id":{"type":"integer"},
            "name":{"type":"string"},
            "tags":{"type":"string","description":"Space separated tags; replaces the current ones."}},
          "required":["id"]})",
         &tool_update_node},
        {"replace_text",
         "Replace exact text in a node. old_text must occur once, unless replace_all is true. The new text "
         "keeps the formatting of the text it replaces. Use an empty new_text to delete text.",
         R"({"type":"object","properties":{
            "id":{"type":"integer"},
            "old_text":{"type":"string"},
            "new_text":{"type":"string"},
            "replace_all":{"type":"boolean","description":"Default false."}},
          "required":["id","old_text","new_text"]})",
         &tool_replace_text},
        {"insert_text",
         "Insert text into a node: at the end (default), at the start, or after or before anchor_text, "
         "which must occur once. In a rich_text node the text is Markdown unless markdown is false. "
         "Include the newlines you need around it.",
         R"({"type":"object","properties":{
            "id":{"type":"integer"},
            "text":{"type":"string"},
            "position":{"type":"string","enum":["end","start","after","before"]},
            "anchor_text":{"type":"string"},
            "markdown":{"type":"boolean","description":"Default true."}},
          "required":["id","text"]})",
         &tool_insert_text},
        {"set_node_content",
         "Replace the whole content of a node, embedded objects included. In a rich_text node the content "
         "is Markdown unless markdown is false.",
         R"({"type":"object","properties":{
            "id":{"type":"integer"},
            "content":{"type":"string"},
            "markdown":{"type":"boolean","description":"Default true."}},
          "required":["id","content"]})",
         &tool_set_node_content},
    };
    return tools;
}

// The window the agent works on: the most recently focused CherryTree window
CtMainWin* target_window()
{
    GApplication* pGApp = g_application_get_default();
    if (not pGApp or not GTK_IS_APPLICATION(pGApp)) return nullptr;
    for (GList* pList = gtk_application_get_windows(GTK_APPLICATION(pGApp)); pList; pList = pList->next) {
        if (auto pCtMainWin = dynamic_cast<CtMainWin*>(Glib::wrap(GTK_WINDOW(pList->data)))) {
            return pCtMainWin;
        }
    }
    return nullptr;
}

json rpc_result(const json& id, json result)
{
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

json rpc_error(const json& id, const int code, const std::string& message)
{
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

std::string dump(const json& j, const int indent = -1)
{
    return j.dump(indent, ' ', false/*ensure_ascii*/, json::error_handler_t::replace);
}

#ifndef _WIN32
bool write_all(const int fd, const char* pData, size_t size, const bool isSocket)
{
    while (size > 0) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = isSocket ? send(fd, pData, size, MSG_NOSIGNAL) : write(fd, pData, size);
#else
        (void)isSocket;
        const ssize_t n = write(fd, pData, size);
#endif
        if (n < 0) {
            if (EINTR == errno) continue;
            return false;
        }
        pData += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

// a socket that is not inherited by the processes CherryTree starts, such as the terminal shell
int new_socket()
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int connect_socket(const std::string& path)
{
    sockaddr_un addr{};
    if (path.size() >= sizeof(addr.sun_path)) return -1;
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    const int fd = new_socket();
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool peer_is_same_user(const int fd)
{
#if defined(SO_PEERCRED)
    struct ucred cred{};
    socklen_t len = sizeof(cred);
    return 0 == getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) and cred.uid == getuid();
#else
    uid_t uid;
    gid_t gid;
    return 0 == getpeereid(fd, &uid, &gid) and uid == getuid();
#endif
}
#endif // !_WIN32

} // namespace

CtAgentServer& CtAgentServer::instance()
{
    static CtAgentServer server;
    return server;
}

CtAgentServer::~CtAgentServer()
{
    stop();
}

/*static*/std::string CtAgentServer::socket_path(const std::string& appIdPostfix)
{
    return Glib::build_filename(g_get_user_runtime_dir(), "cherrytree_docmodel",
                                std::string{APP_ID_BASE} + appIdPostfix + ".sock");
}

/*static*/std::string CtAgentServer::relay_command()
{
    std::string exePath;
#ifdef __linux__
    if (gchar* pPath = g_file_read_link("/proc/self/exe", nullptr)) {
        exePath = pPath;
        g_free(pPath);
    }
#endif
    if (exePath.empty()) exePath = "cherrytree";
    return Glib::shell_quote(exePath) + " --mcp-stdio";
}

bool CtAgentServer::start(std::string& rError)
{
#ifdef _WIN32
    rError = "AI agent access is not supported on Windows yet";
    return false;
#else
    if (is_running()) return true;

    std::string appIdPostfix;
    if (GApplication* pGApp = g_application_get_default()) {
        const gchar* pAppId = g_application_get_application_id(pGApp);
        if (pAppId and g_str_has_prefix(pAppId, APP_ID_BASE)) {
            appIdPostfix = pAppId + strlen(APP_ID_BASE);
        }
    }
    const std::string path = socket_path(appIdPostfix);
    sockaddr_un addr{};
    if (path.size() >= sizeof(addr.sun_path)) {
        rError = "The socket path is too long: " + path;
        return false;
    }
    const std::string dirPath = Glib::path_get_dirname(path);
    if (g_mkdir_with_parents(dirPath.c_str(), 0700) != 0) {
        rError = "Cannot create the directory " + dirPath + ": " + std::strerror(errno);
        return false;
    }
    const int probeFd = connect_socket(path);
    if (probeFd >= 0) {
        close(probeFd);
        rError = "Another CherryTree instance already allows AI agent access";
        return false;
    }
    (void)g_unlink(path.c_str()); // left behind by a crash

    const int fd = new_socket();
    if (fd < 0) {
        rError = std::string{"Cannot create the socket: "} + std::strerror(errno);
        return false;
    }
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 or
        chmod(path.c_str(), 0600) != 0 or
        listen(fd, 4) != 0)
    {
        rError = "Cannot listen on " + path + ": " + std::strerror(errno);
        close(fd);
        (void)g_unlink(path.c_str());
        return false;
    }
    _listenFd = fd;
    _socketPath = path;
    _listenSourceId = g_unix_fd_add(fd, G_IO_IN, &CtAgentServer::_on_listen_ready, this);
    spdlog::info("CtAgentServer: listening on {}", path);
    return true;
#endif
}

void CtAgentServer::stop()
{
#ifndef _WIN32
    for (auto& fdClient : _clients) {
        if (fdClient.second.sourceId) g_source_remove(fdClient.second.sourceId);
        close(fdClient.first);
    }
    _clients.clear();
    if (_listenSourceId) {
        g_source_remove(_listenSourceId);
        _listenSourceId = 0;
    }
    if (_listenFd >= 0) {
        close(_listenFd);
        _listenFd = -1;
        (void)g_unlink(_socketPath.c_str());
        spdlog::info("CtAgentServer: stopped");
    }
#endif
}

#ifndef _WIN32
/*static*/gboolean CtAgentServer::_on_listen_ready(gint fd, GIOCondition /*condition*/, gpointer user_data)
{
    auto pSelf = static_cast<CtAgentServer*>(user_data);
    const int clientFd = accept(fd, nullptr, nullptr);
    if (clientFd < 0) return G_SOURCE_CONTINUE;
    (void)fcntl(clientFd, F_SETFD, FD_CLOEXEC);
    // the socket file is private to the user already; this also covers a changed file mode
    if (not peer_is_same_user(clientFd)) {
        spdlog::warn("CtAgentServer: refused a connection from another user");
        close(clientFd);
        return G_SOURCE_CONTINUE;
    }
    Client& client = pSelf->_clients[clientFd];
    client.fd = clientFd;
    client.sourceId = g_unix_fd_add(clientFd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                    &CtAgentServer::_on_client_ready, pSelf);
    spdlog::debug("CtAgentServer: client connected");
    return G_SOURCE_CONTINUE;
}

/*static*/gboolean CtAgentServer::_on_client_ready(gint fd, GIOCondition /*condition*/, gpointer user_data)
{
    auto pSelf = static_cast<CtAgentServer*>(user_data);
    auto it = pSelf->_clients.find(fd);
    if (it == pSelf->_clients.end()) return G_SOURCE_REMOVE;
    if (pSelf->_read_client(it->second)) return G_SOURCE_CONTINUE;
    close(fd);
    pSelf->_clients.erase(fd);
    spdlog::debug("CtAgentServer: client disconnected");
    return G_SOURCE_REMOVE;
}

bool CtAgentServer::_read_client(Client& client)
{
    char buf[65536];
    const ssize_t n = read(client.fd, buf, sizeof(buf));
    if (n < 0 and (EINTR == errno or EAGAIN == errno)) return true;
    if (n <= 0) return false;
    client.inBuf.append(buf, static_cast<size_t>(n));
    if (client.inBuf.size() > 64u*1024u*1024u) return false; // no MCP message is that big
    for (size_t newlinePos = client.inBuf.find('\n'); std::string::npos != newlinePos; newlinePos = client.inBuf.find('\n')) {
        std::string message = client.inBuf.substr(0, newlinePos);
        client.inBuf.erase(0, newlinePos + 1);
        if (not message.empty() and '\r' == message.back()) message.pop_back();
        if (message.empty()) continue;
        std::string reply = handle_message(message);
        if (reply.empty()) continue;
        reply += '\n';
        if (not write_all(client.fd, reply.data(), reply.size(), true/*isSocket*/)) return false;
    }
    return true;
}
#else
/*static*/gboolean CtAgentServer::_on_listen_ready(gint, GIOCondition, gpointer) { return G_SOURCE_REMOVE; }
/*static*/gboolean CtAgentServer::_on_client_ready(gint, GIOCondition, gpointer) { return G_SOURCE_REMOVE; }
bool CtAgentServer::_read_client(Client&) { return false; }
#endif // _WIN32

std::string CtAgentServer::handle_message(const std::string& message)
{
    const json request = json::parse(message, nullptr, false/*allow_exceptions*/);
    if (request.is_discarded() or not request.is_object()) {
        return dump(rpc_error(nullptr, -32700, "Parse error"));
    }
    if (not request.contains("id")) {
        return ""; // a notification, e.g. notifications/initialized
    }
    const json& id = request["id"];
    const std::string method = request.value("method", "");
    const json params = request.value("params", json::object());

    if ("initialize" == method) {
        std::string protocolVersion = params.value("protocolVersion", MCP_PROTOCOL_LATEST);
        if ("2025-06-18" != protocolVersion and "2025-03-26" != protocolVersion and "2024-11-05" != protocolVersion) {
            protocolVersion = MCP_PROTOCOL_LATEST;
        }
        return dump(rpc_result(id, json{
            {"protocolVersion", protocolVersion},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "cherrytree"}, {"version", CtConst::CT_VERSION}}},
            {"instructions", AGENT_INSTRUCTIONS}}));
    }
    if ("ping" == method) {
        return dump(rpc_result(id, json::object()));
    }
    if ("tools/list" == method) {
        json tools = json::array();
        for (const CtAgentTool& tool : agent_tools()) {
            tools.push_back(json{{"name", tool.name},
                                 {"description", tool.description},
                                 {"inputSchema", json::parse(tool.inputSchema)}});
        }
        return dump(rpc_result(id, json{{"tools", std::move(tools)}}));
    }
    if ("tools/call" == method) {
        const std::string toolName = params.value("name", "");
        const json arguments = params.value("arguments", json::object());
        bool isError{false};
        std::string text;
        if (CtMainWin* pCtMainWin = target_window()) {
            text = call_tool(pCtMainWin, toolName, dump(arguments), isError);
        }
        else {
            isError = true;
            text = dump(json{{"error", "No CherryTree window is open"}});
        }
        return dump(rpc_result(id, json{{"content", json::array({json{{"type", "text"}, {"text", text}}})},
                                        {"isError", isError}}));
    }
    return dump(rpc_error(id, -32601, "Method not found: " + method));
}

/*static*/std::string CtAgentServer::call_tool(CtMainWin* pCtMainWin,
                                               const std::string& toolName,
                                               const std::string& argsJson,
                                               bool& rIsError)
{
    rIsError = false;
    try {
        const json args = argsJson.empty() ? json::object() : json::parse(argsJson);
        for (const CtAgentTool& tool : agent_tools()) {
            if (toolName == tool.name) {
                CtAgentCtx ctx = make_ctx(pCtMainWin);
                return dump(tool.run(ctx, args), 1);
            }
        }
        throw CtAgentError{"Unknown tool: " + toolName};
    }
    catch (const CtAgentError& e) {
        rIsError = true;
        return dump(json{{"error", e.what()}});
    }
    catch (const json::exception& e) {
        rIsError = true;
        return dump(json{{"error", std::string{"Invalid arguments: "} + e.what()}});
    }
    catch (const std::exception& e) {
        rIsError = true;
        spdlog::error("CtAgentServer: tool {} failed: {}", toolName, e.what());
        return dump(json{{"error", std::string{"The tool failed: "} + e.what()}});
    }
}

/*static*/int CtAgentServer::run_stdio_relay(int argc, char* argv[])
{
#ifdef _WIN32
    (void)argc;
    (void)argv;
    fputs("cherrytree --mcp-stdio is not supported on Windows yet\n", stderr);
    return 1;
#else
    std::string appIdPostfix;
    for (int i = 2; i < argc; ++i) {
        if (0 == strcmp("-S", argv[i]) or 0 == strcmp("--secondary_session", argv[i])) {
            appIdPostfix = "_2";
        }
    }
    const std::string path = socket_path(appIdPostfix);
    const int sockFd = connect_socket(path);
    if (sockFd < 0) {
        fprintf(stderr, "Cannot reach CherryTree at %s. Start CherryTree, open a document and choose "
                        "Tools > Allow/Deny AI Agent Access.\n", path.c_str());
        return 1;
    }
    (void)signal(SIGPIPE, SIG_IGN);

    pollfd fds[2]{{STDIN_FILENO, POLLIN, 0}, {sockFd, POLLIN, 0}};
    char buf[65536];
    while (true) {
        if (poll(fds, 2, -1) < 0) {
            if (EINTR == errno) continue;
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n < 0 and EINTR == errno) continue;
            if (n <= 0) {
                // the client is done: let the app finish replying, then it closes the socket
                fds[0].fd = -1;
                shutdown(sockFd, SHUT_WR);
            }
            else if (not write_all(sockFd, buf, static_cast<size_t>(n), true/*isSocket*/)) {
                break;
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            const ssize_t n = read(sockFd, buf, sizeof(buf));
            if (n < 0 and EINTR == errno) continue;
            if (n <= 0) break;
            if (not write_all(STDOUT_FILENO, buf, static_cast<size_t>(n), false/*isSocket*/)) break;
        }
    }
    close(sockFd);
    return 0;
#endif
}
