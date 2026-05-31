/*
 * ct_actions_tree.cc
 *
 * Copyright 2009-2026
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

#include <sigc++/sigc++.h>
#include "ct_actions.h"
#include "ct_image.h"
#include "ct_dialogs.h"
#include "ct_clipboard.h"
#include "ct_treestore.h"
#include "ct_logging.h"
#include "ct_command_bridge.h"
#include "ct_node_commands.h"
#include "ct_table.h"
#include <ctime>
#include <gtkmm/dialog.h>

// ─── Helpers used by node-operation command construction ─────────────────────

// Capture all metadata from a GTK tree iterator into CtNodeProps.
static CtNodeProps nodePropsFromIter(const CtTreeIter& iter)
{
    CtNodeProps p;
    p.name                    = iter.get_node_name();
    p.syntax                  = iter.get_node_syntax_highlighting();
    p.tags                    = iter.get_node_tags();
    p.isReadOnly               = iter.get_node_read_only();
    p.isBold                  = iter.get_node_is_bold();
    p.customIconId             = iter.get_node_custom_icon_id();
    p.foregroundRgb24          = iter.get_node_foreground();
    p.excludeMeFromSearch      = iter.get_node_is_excluded_from_search();
    p.excludeChildrenFromSearch = iter.get_node_children_are_excluded_from_search();
    p.tsCreation               = iter.get_node_creating_time();
    p.tsLastSave               = iter.get_node_modification_time();
    return p;
}

// Fill CtNodeProps from a CtNodeData struct (dialog output).
static CtNodeProps nodePropsFromData(const CtNodeData& d)
{
    CtNodeProps p;
    p.name                    = d.name;
    p.syntax                  = d.syntax;
    p.tags                    = d.tags;
    p.isReadOnly               = d.isReadOnly;
    p.isBold                  = d.isBold;
    p.customIconId             = d.customIconId;
    p.foregroundRgb24          = d.foregroundRgb24;
    p.excludeMeFromSearch      = d.excludeMeFromSearch;
    p.excludeChildrenFromSearch = d.excludeChildrenFromSearch;
    p.tsCreation               = d.tsCreation;
    p.tsLastSave               = d.tsLastSave;
    return p;
}

// Return the 0-based position of iter among its siblings.
static int gtkIterPos(const Gtk::TreeModel::iterator& iter)
{
    int pos = 0;
    Gtk::TreeModel::iterator tmp = iter;
    while (--tmp) ++pos;
    return pos;
}

bool CtActions::_is_there_selected_node_or_error()
{
    if (_pCtMainWin->curr_tree_iter()) return true;
    CtDialogs::warning_dialog(_("No Node is Selected"), *_pCtMainWin);
    return false;
}

bool CtActions::_is_tree_not_empty_or_error()
{
    if (not _pCtMainWin->get_tree_store().get_iter_first()) {
        CtDialogs::error_dialog(_("The Tree is Empty!"), *_pCtMainWin);
        return false;
    }
    return true;
}

bool CtActions::_is_curr_node_not_read_only_or_error()
{
    if (_pCtMainWin->curr_tree_iter().get_node_read_only()) {
        CtDialogs::error_dialog(_("The Selected Node is Read Only."), *_pCtMainWin);
        return false;
    }
    return true;
}

// Returns True if ok (no syntax highlighting) or False and prompts error dialog
bool CtActions::_is_curr_node_not_syntax_highlighting_or_error(bool plain_text_ok /*=false*/)
{
    if (_pCtMainWin->curr_tree_iter().get_node_syntax_highlighting() == CtConst::RICH_TEXT_ID
        or (plain_text_ok and _pCtMainWin->curr_tree_iter().get_node_syntax_highlighting() == CtConst::PLAIN_TEXT_ID))
        return true;
    if (not plain_text_ok)
        CtDialogs::warning_dialog(_("This Feature is Available Only in Rich Text Nodes."), *_pCtMainWin);
    else
        CtDialogs::warning_dialog(_("This Feature is Not Available in Automatic Syntax Highlighting Nodes."), *_pCtMainWin);
    return false;
}

// Returns True if ok (there's a selection) or False and prompts error dialog
bool CtActions::_is_there_text_selection_or_error()
{
    if (not _is_there_selected_node_or_error()) return false;
    if (not _curr_buffer()->get_has_selection()) {
        CtDialogs::error_dialog(_("No Text is Selected."), *_pCtMainWin);
        return false;
    }
    return true;
}

bool CtActions::_is_there_anch_widg_selection_or_error(const char anch_widg_id)
{
    if (not _is_there_selected_node_or_error()) return false;
    if (not _is_curr_node_not_syntax_highlighting_or_error()) return false;

    // When focus is inside a rich table cell, the main text view cursor is not
    // at the table anchor.  The cell context menu already set curr_table_anchor
    // via on_cell_populate_popup, so trust it.
    if ('t' == anch_widg_id) {
        auto* pBridge = _pCtMainWin->get_command_bridge();
        if (pBridge && pBridge->isActive() && pBridge->isTrackingRichCell() && curr_table_anchor) {
            return true;
        }
    }

    bool already_failed{false};
    Gtk::TextIter iter_insert;
    if (_curr_buffer()->get_has_selection()) {
        Gtk::TextIter iter_sel_end;
        _pCtMainWin->get_text_view().get_buffer()->get_selection_bounds(iter_insert, iter_sel_end);
        const int num_chars = iter_sel_end.get_offset() - iter_insert.get_offset();
        if (num_chars != 1) {
            already_failed = true;
        }
    }
    else {
        iter_insert = _pCtMainWin->get_text_view().get_buffer()->get_insert()->get_iter();
    }
    if (not already_failed) {
        auto widgets = _pCtMainWin->curr_tree_iter().get_anchored_widgets(iter_insert.get_offset(), iter_insert.get_offset());
        if (not widgets.empty()) {
            if ('t' == anch_widg_id) {
                auto pTableCommon = dynamic_cast<CtTableCommon*>(widgets.front());
                if (pTableCommon) {
                    curr_table_anchor = pTableCommon;
                    return true;
                }
            }
            else if ('c' == anch_widg_id) {
                auto pCodeBox = dynamic_cast<CtCodebox*>(widgets.front());
                if (pCodeBox) {
                    curr_codebox_anchor = pCodeBox;
                    return true;
                }
            }
        }
    }
    if ('t' == anch_widg_id) CtDialogs::error_dialog(_("No Table is Selected."), *_pCtMainWin);
    else if ('c' == anch_widg_id) CtDialogs::error_dialog(_("No CodeBox is Selected."), *_pCtMainWin);
    return false;
}

// Put Selection Upon the anchored widget
void CtActions::object_set_selection(CtAnchoredWidget* widget)
{
    // When the widget lives in a rich table cell its anchor is in the cell buffer,
    // not the main node buffer.  Operating on _curr_buffer() with that anchor is UB
    // and corrupts the main buffer's modified flag, causing a stale re-sync later.
    auto pBridge = _pCtMainWin->get_command_bridge();
    if (pBridge && pBridge->isActive() && pBridge->isTrackingRichCell()) {
        return;
    }

    // Detect widgets embedded in a rich cell (same walk as image_copy): their
    // anchor lives in the cell buffer, so select there and track the cell —
    // otherwise Ctrl+C from a fresh click copies a bogus 1-char slice of the
    // main buffer and pastes whatever widget happens to sit at that offset.
    for (auto* w = widget->get_parent(); w; w = w->get_parent()) {
        auto* pTable = dynamic_cast<CtTableRich*>(w);
        if (not pTable) continue;
        for (size_t r = 0; r < pTable->get_num_rows(); ++r) {
            for (size_t c = 0; c < pTable->get_num_columns(); ++c) {
                CtRichCell* cell = pTable->getRichCell(r, c);
                for (auto* emb : cell->getEmbeddedWidgets()) {
                    if (emb != widget) continue;
                    auto cellBuffer = cell->get_buffer();
                    Gtk::TextIter iter_cell_obj = cellBuffer->get_iter_at_child_anchor(widget->getTextChildAnchor());
                    Gtk::TextIter iter_cell_bound = iter_cell_obj;
                    iter_cell_bound.forward_char();
                    cellBuffer->select_range(iter_cell_obj, iter_cell_bound);
                    cell->get_text_view().mm().grab_focus();
                    if (pBridge && pBridge->isActive()) {
                        pBridge->beginWidgetEdit(_pCtMainWin->curr_tree_iter().get_node_id(),
                                                 pTable, (int)r, (int)c);
                    }
                    return;
                }
            }
        }
    }

    const bool isImage = dynamic_cast<CtImage*>(widget) != nullptr;
    Glib::RefPtr<Gtk::TextChildAnchor> anchor = widget->getTextChildAnchor();
    if (_pCtConfig->objectNoSelOnClick) {
        Glib::signal_idle().connect_once([this, anchor, isImage](){
            Gtk::TextIter iter_object = _curr_buffer()->get_iter_at_child_anchor(anchor);
            _curr_buffer()->place_cursor(iter_object);
            if (isImage) {
                auto& textView = _pCtMainWin->get_text_view().mm();
                if (not textView.has_focus()) {
                    textView.grab_focus();
                }
            }
        });
    }
    else {
        Glib::signal_idle().connect_once([this, anchor, isImage](){
            Gtk::TextIter iter_object = _curr_buffer()->get_iter_at_child_anchor(anchor);
            Gtk::TextIter iter_bound = iter_object;
            iter_bound.forward_char();
            _curr_buffer()->select_range(iter_object, iter_bound);
            if (isImage) {
                auto& textView = _pCtMainWin->get_text_view().mm();
                if (not textView.has_focus()) {
                    textView.grab_focus();
                }
            }
        });
    }
}

// Returns True if there's not a node selected or is not rich text
bool CtActions::_node_sel_and_rich_text()
{
    if (not _is_there_selected_node_or_error()) return false;
    if (not _is_curr_node_not_syntax_highlighting_or_error()) return false;
    return true;
}

void CtActions::node_subnodes_copy()
{
    if (not _is_there_selected_node_or_error()) return;
    _pCtMainWin->emit_app_tree_node_copy();
}

void CtActions::node_subnodes_paste()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    _pCtMainWin->emit_app_tree_node_paste();
}

void CtActions::node_subnodes_paste2(CtTreeIter& other_ct_tree_iter,
                                     CtMainWin* pWinToCopyFrom)
{
    CtTreeStore& ct_treestore = _pCtMainWin->get_tree_store();
    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (!pModel) {
        // Bridge inactive — use the old direct-append path
        _node_add(CtDuplicateShared::Duplicate, false/*add_as_child*/, &other_ct_tree_iter, pWinToCopyFrom);
        Gtk::TreeModel::iterator new_top_iter = _pCtMainWin->curr_tree_iter();
        std::function<void(Gtk::TreeModel::iterator, Gtk::TreeModel::iterator)> dup_subnodes;
        dup_subnodes = [&](Gtk::TreeModel::iterator old_parent, Gtk::TreeModel::iterator new_parent) {
            #if GTKMM_MAJOR_VERSION >= 4
            for (Gtk::TreeModel::iterator child = old_parent->children().begin(); child; ++child) {
            #else
            for (Gtk::TreeModel::iterator child : old_parent->children()) {
            #endif
                CtNodeData nd{};
                pWinToCopyFrom->get_tree_store().get_node_data(
                    pWinToCopyFrom->get_tree_store().to_ct_tree_iter(child), nd, true);
                nd.tsCreation = std::time(nullptr);
                nd.tsLastSave = nd.tsCreation;
                nd.nodeId = ct_treestore.node_id_get();
                auto new_child = ct_treestore.append_node(&nd, &new_parent);
                ct_treestore.to_ct_tree_iter(new_child).pending_new_db_node();
                dup_subnodes(child, new_child);
            }
        };
        dup_subnodes(other_ct_tree_iter, new_top_iter);
        ct_treestore.nodes_sequences_fix(new_top_iter->parent(), true);
        pWinToCopyFrom->get_tree_view().set_cursor_safe(other_ct_tree_iter);
        _pCtMainWin->get_tree_view().set_cursor_safe(new_top_iter);
        _pCtMainWin->get_text_view().mm().grab_focus();
        return;
    }

    // Bridge active — build one CompoundCommand for the entire subtree so the
    // whole duplicate is a single undo step.
    Gtk::TreeModel::iterator curr_iter = _pCtMainWin->curr_tree_iter();
    gint64 topParentId = 0;
    int topPosition = 0;
    if (curr_iter) {
        Gtk::TreeModel::iterator parentGtk = curr_iter->parent();
        if (parentGtk) {
            CtTreeIter parentCtIter = ct_treestore.to_ct_tree_iter(parentGtk);
            if (parentCtIter) topParentId = parentCtIter.get_node_id();
        }
        topPosition = gtkIterPos(curr_iter) + 1;
    } else {
        auto rootNode = pModel->getRootNode();
        topPosition = rootNode ? static_cast<int>(rootNode->getChildren().size()) : 0;
    }

    auto compound = std::make_unique<CompoundCommand>("[" + std::to_string(other_ct_tree_iter.get_node_id()) + "] Duplicate node");
    gint64 topNodeId = 0;

    // node_id_get() scans the GTK tree for the current max ID each call, so
    // calling it N times before the compound executes returns the same ID every
    // time (the newly allocated IDs aren't in the GTK tree yet).  Allocate the
    // first ID once and increment manually for the rest.
    gint64 nextNodeId = ct_treestore.node_id_get();

    // Build AddNodeCommand for one source node and add it to the compound.
    // Returns the new nodeId so callers can use it as parentId for children.
    auto buildAddCmd = [&](CtTreeIter srcIter, gint64 parentId, int position) -> gint64 {
        CtNodeData nd{};
        pWinToCopyFrom->get_tree_store().get_node_data(srcIter, nd, true/*loadTextBuffer*/);
        nd.sharedNodesMasterId = 0;
        nd.tsCreation = std::time(nullptr);
        nd.tsLastSave = nd.tsCreation;
        nd.nodeId = nextNodeId++;
        if (!nd.pTextBuffer)
            nd.pTextBuffer = _pCtMainWin->get_new_text_buffer();
        CtNodeContent content = buildContentFromBuffer(nd.pTextBuffer, nd.anchoredWidgets);
        compound->addCommand(std::make_unique<AddNodeCommand>(
            pModel, nd.nodeId, parentId, position, nodePropsFromData(nd), std::move(content), 0));
        return nd.nodeId;
    };

    std::function<void(CtTreeIter, gint64, int)> buildSubtree;
    buildSubtree = [&](CtTreeIter srcIter, gint64 parentId, int position) {
        gint64 newId = buildAddCmd(srcIter, parentId, position);
        if (!topNodeId) topNodeId = newId;
        int childPos = 0;
        #if GTKMM_MAJOR_VERSION >= 4
        for (Gtk::TreeModel::iterator child = static_cast<Gtk::TreeModel::iterator>(srcIter)->children().begin(); child; ++child) {
        #else
        for (Gtk::TreeModel::iterator child : static_cast<Gtk::TreeModel::iterator>(srcIter)->children()) {
        #endif
            buildSubtree(pWinToCopyFrom->get_tree_store().to_ct_tree_iter(child), newId, childPos++);
        }
    };
    buildSubtree(other_ct_tree_iter, topParentId, topPosition);

    pBridge->pushNodeCommand(std::move(compound));

    CtTreeIter topCtIter = ct_treestore.get_node_from_node_id(topNodeId);
    if (topCtIter) {
        ct_treestore.nodes_sequences_fix(static_cast<Gtk::TreeModel::iterator>(topCtIter)->parent(), true);
        pWinToCopyFrom->get_tree_view().set_cursor_safe(other_ct_tree_iter);
        _pCtMainWin->get_tree_view().set_cursor_safe(static_cast<Gtk::TreeModel::iterator>(topCtIter));
    }
    _pCtMainWin->get_text_view().mm().grab_focus();
}

void CtActions::node_subnodes_duplicate()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    CtTreeIter top_iter = _pCtMainWin->curr_tree_iter();
    node_subnodes_paste2(top_iter, _pCtMainWin);
}

void CtActions::_node_add(const CtDuplicateShared duplicate_shared,
                          const bool add_as_child,
                          const CtTreeIter* pCtTreeIterFrom/*=nullptr*/,
                          CtMainWin* pWinToCopyFrom/*=nullptr*/)
{
    CtNodeData nodeData{};
    if (CtDuplicateShared::None == duplicate_shared) {
        std::string title = add_as_child ? _("New Child Node Properties") : _("New Node Properties");
        CtTreeIter currTreeIter = _pCtMainWin->curr_tree_iter();
        nodeData.syntax = currTreeIter ? currTreeIter.get_node_syntax_highlighting() : CtConst::RICH_TEXT_ID;
        if (not CtDialogs::node_prop_dialog(title, _pCtMainWin, nodeData, _pCtMainWin->get_tree_store().get_used_tags())) {
            return;
        }
    }
    else {
        pWinToCopyFrom->get_tree_store().get_node_data(*pCtTreeIterFrom, nodeData, true/*loadTextBuffer*/);
        if (CtDuplicateShared::Duplicate == duplicate_shared) {
            // Text buffer is already loaded with all content (command pattern handles undo/redo now)
            nodeData.sharedNodesMasterId = 0;
        }
        else {
            // CtDuplicateShared::Shared
            if (nodeData.sharedNodesMasterId > 0) {
                // node from is also a shared node, we just point to the same master / leave it as is
            }
            else {
                // node from is not a shared node, let's set the shared node id to its node id (new node id will be assigned)
                nodeData.sharedNodesMasterId = nodeData.nodeId;
            }
        }
    }
    (void)_node_add_with_data(_pCtMainWin->curr_tree_iter(), nodeData, add_as_child);
}

Gtk::TreeModel::iterator CtActions::_node_add_with_data(Gtk::TreeModel::iterator curr_iter,
                                             CtNodeData& nodeData,
                                             const bool add_as_child)
{
    if (nodeData.sharedNodesMasterId <= 0) {
        if (not nodeData.pTextBuffer) {
            nodeData.pTextBuffer = _pCtMainWin->get_new_text_buffer();
        }
        nodeData.tsCreation = std::time(nullptr);
        nodeData.tsLastSave = nodeData.tsCreation;
    }
    CtTreeStore& ct_treestore = _pCtMainWin->get_tree_store();
    nodeData.nodeId = ct_treestore.node_id_get();

    _pCtMainWin->update_window_save_needed();
    _pCtConfig->syntaxHighlighting = nodeData.syntax;

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    Gtk::TreeModel::iterator nodeIter;

    if (pModel) {
        // Determine parent ID and insertion position
        gint64 parentId = 0;
        int position = -1; // -1 = append

        if (add_as_child && curr_iter) {
            CtTreeIter parentCtIter = ct_treestore.to_ct_tree_iter(curr_iter);
            if (parentCtIter) {
                parentId = parentCtIter.get_node_id();
                // Append after all existing model children
                auto parentNode = pModel->getNodeById(parentId);
                position = parentNode ? static_cast<int>(parentNode->getChildren().size()) : 0;
            }
        } else if (curr_iter) {
            // Insert after curr_iter among its siblings
            Gtk::TreeModel::iterator parentGtk = curr_iter->parent();
            if (parentGtk) {
                CtTreeIter parentCtIter = ct_treestore.to_ct_tree_iter(parentGtk);
                if (parentCtIter) parentId = parentCtIter.get_node_id();
            }
            position = gtkIterPos(curr_iter) + 1;
        } else {
            // Root-level append
            parentId = 0;
            auto rootNode = pModel->getRootNode();
            position = rootNode ? static_cast<int>(rootNode->getChildren().size()) : 0;
        }

        // Capture initial content from buffer + already-loaded widgets
        CtNodeContent initialContent;
        if (nodeData.sharedNodesMasterId <= 0 && nodeData.pTextBuffer) {
            initialContent = buildContentFromBuffer(nodeData.pTextBuffer, nodeData.anchoredWidgets);
        }

        CtNodeProps props = nodePropsFromData(nodeData);

        pBridge->pushNodeCommand(std::make_unique<AddNodeCommand>(
            pModel, nodeData.nodeId, parentId, position, props, initialContent,
            nodeData.sharedNodesMasterId));

        // Look up the GTK iter that onNodeAdded created
        CtTreeIter nodeCtIter = ct_treestore.get_node_from_node_id(nodeData.nodeId);
        if (nodeCtIter) {
            ct_treestore.nodes_sequences_fix(static_cast<Gtk::TreeModel::iterator>(nodeCtIter)->parent(), false);
            nodeIter = static_cast<Gtk::TreeModel::iterator>(nodeCtIter);
        }
    } else {
        // Bridge not active — add directly to GTK tree
        if (add_as_child) {
            nodeIter = ct_treestore.append_node(&nodeData, &curr_iter/*as parent*/);
        } else if (curr_iter) {
            nodeIter = ct_treestore.insert_node(&nodeData, curr_iter/*after*/);
        } else {
            nodeIter = ct_treestore.append_node(&nodeData);
        }
        CtTreeIter nodeCtIter = ct_treestore.to_ct_tree_iter(nodeIter);
        nodeCtIter.pending_new_db_node();
        ct_treestore.nodes_sequences_fix(nodeIter->parent(), false);
        ct_treestore.update_node_aux_icon(nodeCtIter);
    }

    if (nodeIter) {
        _pCtMainWin->get_tree_view().set_cursor_safe(nodeIter);
        _pCtMainWin->get_text_view().mm().grab_focus();
    }
    return nodeIter;
}

Gtk::TreeModel::iterator CtActions::node_child_exist_or_create(Gtk::TreeModel::iterator parentIter, const std::string& nodeName, const bool focusIfExisting)
{
    #if GTKMM_MAJOR_VERSION >= 4
    auto children = parentIter ? parentIter->children() : _pCtMainWin->get_tree_store().get_store()->children();
    Gtk::TreeModel::iterator childIter = children.begin();
    #else
    Gtk::TreeModel::iterator childIter = parentIter ? parentIter->children().begin() : _pCtMainWin->get_tree_store().get_iter_first();
    #endif
    for (; childIter; ++childIter) {
        if (_pCtMainWin->get_tree_store().to_ct_tree_iter(childIter).get_node_name() == Glib::ustring(nodeName)) {
            if (focusIfExisting) {
                _pCtMainWin->get_tree_view().set_cursor_safe(childIter);
            }
            return childIter;
        }
    }
    CtNodeData nodeData{};
    nodeData.name = nodeName;
    nodeData.syntax = CtConst::RICH_TEXT_ID;
    return _node_add_with_data(parentIter, nodeData, true/*add_as_child*/);
}

// Move a node to a parent and after a sibling
void CtActions::node_move_after(Gtk::TreeModel::iterator iter_to_move,
                                Gtk::TreeModel::iterator father_iter,
                                Gtk::TreeModel::iterator brother_iter/*= Gtk::TreeModel::iterator{}*/,
                                bool set_first/*= false*/)
{
    CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (pModel) {
        // Compute old parent ID and position
        CtTreeIter moveCtIter = ctTreeStore.to_ct_tree_iter(iter_to_move);
        const gint64 nodeId = moveCtIter.get_node_id();
        const int oldPos = gtkIterPos(iter_to_move);
        gint64 oldParentId = 0;
        {
            auto gtkOldParent = iter_to_move->parent();
            if (gtkOldParent) {
                CtTreeIter pt = ctTreeStore.to_ct_tree_iter(gtkOldParent);
                if (pt) oldParentId = pt.get_node_id();
            }
        }

        // Compute new parent ID
        gint64 newParentId = 0;
        if (father_iter) {
            CtTreeIter ft = ctTreeStore.to_ct_tree_iter(father_iter);
            if (ft) newParentId = ft.get_node_id();
        }

        // Compute new position (post-removal position in new parent)
        int newPos = -1; // -1 = append
        if (brother_iter) {
            int brotherPos = gtkIterPos(brother_iter);
            // If same parent and node is before brother, removal shifts brother up by 1
            if (oldParentId == newParentId && oldPos < brotherPos) {
                newPos = brotherPos; // compensated
            } else {
                newPos = brotherPos + 1;
            }
        } else if (set_first) {
            newPos = 0;
        } else {
            // Append: count existing children of new parent, subtract 1 if same parent
            auto newParentNode = pModel->getNodeById(newParentId);
            int cnt = newParentNode ? static_cast<int>(newParentNode->getChildren().size()) : 0;
            if (oldParentId == newParentId) cnt--; // one child removed
            newPos = std::max(0, cnt);
        }

        pBridge->pushNodeCommand(std::make_unique<MoveNodeCommand>(
            pModel, nodeId, oldParentId, oldPos, newParentId, newPos));
        return;
    }

    // Bridge not active — move directly in GTK tree
    Glib::RefPtr<Gtk::TreeStore> pTreeStore = ctTreeStore.get_store();
    Gtk::TreeModel::iterator new_node_iter;
    if (brother_iter)   new_node_iter = pTreeStore->insert_after(brother_iter);
    else if (set_first) new_node_iter = pTreeStore->prepend(father_iter->children());
    else                new_node_iter = pTreeStore->append(father_iter->children());

    std::function<void(Gtk::TreeModel::iterator&,Gtk::TreeModel::iterator&)> node_move_data_and_children;
    node_move_data_and_children = [&](Gtk::TreeModel::iterator& old_iter,Gtk::TreeModel::iterator& new_iter) {
        CtNodeData node_data{};
        ctTreeStore.get_node_data(old_iter, node_data, true/*loadTextBuffer*/);
        ctTreeStore.update_node_data(new_iter, node_data);
        #if GTKMM_MAJOR_VERSION >= 4
        for (Gtk::TreeModel::iterator child = old_iter->children().begin(); child; ++child) {
            Gtk::TreeModel::iterator new_child = pTreeStore->append(new_iter->children());
            node_move_data_and_children(child, new_child);
        }
        #else
        for (Gtk::TreeModel::iterator child : old_iter->children()) {
            Gtk::TreeModel::iterator new_child = pTreeStore->append(new_iter->children());
            node_move_data_and_children(child, new_child);
        }
        #endif
    };
    node_move_data_and_children(iter_to_move, new_node_iter);

    _pCtMainWin->resetPrevTreeIter();
    pTreeStore->erase(iter_to_move);
    ctTreeStore.to_ct_tree_iter(new_node_iter).pending_edit_db_node_hier();

    ctTreeStore.nodes_sequences_fix(Gtk::TreeModel::iterator(), true);
    CtTreeView& ctTreeView = _pCtMainWin->get_tree_view();
    if (father_iter) {
        ctTreeView.expand_row(ctTreeStore.get_path(father_iter), false);
    }
    else {
        ctTreeView.expand_row(ctTreeStore.get_path(new_node_iter), false);
    }
    Gtk::TreePath new_node_path = _pCtMainWin->get_tree_store().get_path(new_node_iter);
    ctTreeView.collapse_row(new_node_path);
    ctTreeView.set_cursor(new_node_path);
    _pCtMainWin->update_window_save_needed();
}

bool CtActions::_need_node_swap(Gtk::TreeModel::iterator& leftIter, Gtk::TreeModel::iterator& rightIter, bool ascending)
{
    Glib::ustring left_node_name = _pCtMainWin->get_tree_store().to_ct_tree_iter(leftIter).get_node_name().lowercase();
    Glib::ustring right_node_name = _pCtMainWin->get_tree_store().to_ct_tree_iter(rightIter).get_node_name().lowercase();
    //int cmp = left_node_name.compare(right_node_name);
    int cmp = CtStrUtil::natural_compare(left_node_name, right_node_name);

    return ascending ? cmp > 0 : cmp < 0;
}

bool CtActions::_tree_sort_level_and_sublevels(const Gtk::TreeNodeChildren& children, bool ascending)
{
    #if GTKMM_MAJOR_VERSION >= 4
    // TODO: implement sibling sorting for GTK4; for now, recurse only.
    bool swap_excecuted = false;
    for (auto it = children.begin(); it != children.end(); ++it) {
        // gtkmm4: child->children() returns const-children; recurse by casting to match signature
        if (_tree_sort_level_and_sublevels(static_cast<const Gtk::TreeNodeChildren&>(it->children()), ascending))
            swap_excecuted = true;
    }
    return swap_excecuted;
    #else
    auto need_swap = [this,&ascending](Gtk::TreeModel::iterator& l, Gtk::TreeModel::iterator& r) { return _need_node_swap(l, r, ascending); };
    bool swap_excecuted = CtMiscUtil::node_siblings_sort(_pCtMainWin->get_tree_store().get_store(), children, need_swap);
    for (auto& child: children)
        if (_tree_sort_level_and_sublevels(child.children(), ascending))
            swap_excecuted = true;
    return swap_excecuted;
    #endif
}

void CtActions::node_edit()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    CtNodeData nodeData{};
    CtTreeIter ct_tree_iter = _pCtMainWin->curr_tree_iter();
    CtTreeStore& ct_treestore = _pCtMainWin->get_tree_store();
    ct_treestore.get_node_data(ct_tree_iter, nodeData, true/*loadTextBuffer*/);
    CtNodeData newData = nodeData;
    if (not CtDialogs::node_prop_dialog(_("Node Properties"), _pCtMainWin, newData, ct_treestore.get_used_tags())) {
        return;
    }

    // leaving rich text ?
    if (nodeData.syntax != newData.syntax and
        CtConst::RICH_TEXT_ID == nodeData.syntax and
        not CtDialogs::question_dialog(_("Changing the node type to Automatic Syntax Highlighting removes all rich text formatting in this node. Do you want to Continue?"), *_pCtMainWin))
    {
        return;
    }

    _pCtConfig->syntaxHighlighting = newData.syntax;

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    CtNodeProps oldProps = nodePropsFromIter(ct_tree_iter);
    CtNodeProps newProps = nodePropsFromData(newData);

    if (pModel) {
        // Collect all group members (master + non-masters share the same editable props)
        const gint64 thisNodeId = ct_tree_iter.get_node_id();
        CtSharedNodesMap shared_nodes_map;
        bool isInSharedGroup = false;
        if (ct_treestore.populate_shared_nodes_map(shared_nodes_map) > 0u) {
            for (auto& currPair : shared_nodes_map) {
                if (thisNodeId == currPair.first or newData.sharedNodesMasterId == currPair.first) {
                    isInSharedGroup = true;
                    // Build compound command covering all group members
                    auto compound = std::make_unique<CompoundCommand>("[" + std::to_string(thisNodeId) + "] Edit node properties");
                    compound->addCommand(std::make_unique<EditNodePropertiesCommand>(pModel, thisNodeId, oldProps, newProps));
                    currPair.second.insert(currPair.first); // include master
                    for (const gint64 gid : currPair.second) {
                        if (gid != thisNodeId) {
                            CtTreeIter other = ct_treestore.get_node_from_node_id(gid);
                            if (other) {
                                compound->addCommand(std::make_unique<EditNodePropertiesCommand>(
                                    pModel, gid, nodePropsFromIter(other), newProps));
                            }
                        }
                    }
                    pBridge->pushNodeCommand(std::move(compound));
                    break;
                }
            }
        }
        if (not isInSharedGroup) {
            pBridge->pushNodeCommand(std::make_unique<EditNodePropertiesCommand>(pModel, thisNodeId, oldProps, newProps));
        }
    } else {
        // Bridge not active — update GTK tree directly (fallback)
        ct_treestore.update_node_data(ct_tree_iter, newData);
        if (nodeData.syntax != newData.syntax) {
            _pCtMainWin->switch_buffer_text_source(ct_tree_iter.get_node_text_buffer(), ct_tree_iter, newData.syntax, nodeData.syntax);
        }
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::npro);
    }

    _pCtMainWin->get_text_view().mm().grab_focus();
}

// Change the Selected Node's Children Syntax Highlighting to the Parent's Syntax Highlighting
void CtActions::node_inherit_syntax()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;

    const std::string new_syntax = _pCtMainWin->curr_tree_iter().get_node_syntax_highlighting();

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (pModel) {
        auto compound = std::make_unique<CompoundCommand>("[" + std::to_string(_pCtMainWin->curr_tree_iter().get_node_id()) + "] Inherit syntax");
        std::function<void(Gtk::TreeModel::iterator)> collect;
        collect = [&](Gtk::TreeModel::iterator parent) {
            #if GTKMM_MAJOR_VERSION >= 4
            for (Gtk::TreeModel::iterator child = parent->children().begin(); child; ++child) {
            #else
            for (Gtk::TreeModel::iterator child : parent->children()) {
            #endif
                CtTreeIter iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(child);
                std::string node_syntax = iter.get_node_syntax_highlighting();
                if (not iter.get_node_read_only() and node_syntax != new_syntax) {
                    CtNodeProps oldProps = nodePropsFromIter(iter);
                    CtNodeProps newProps = oldProps;
                    newProps.syntax = new_syntax;
                    compound->addCommand(std::make_unique<EditNodePropertiesCommand>(
                        pModel, iter.get_node_id(), oldProps, newProps));
                }
                collect(child);
            }
        };
        collect(_pCtMainWin->curr_tree_iter());

        if (not compound->isEmpty()) {
            pBridge->pushNodeCommand(std::move(compound));
        }
    } else {
        // Bridge not active — update GTK tree directly (fallback)
        std::function<void(Gtk::TreeModel::iterator)> f_iterate_childs;
        f_iterate_childs = [&](Gtk::TreeModel::iterator parent) {
            #if GTKMM_MAJOR_VERSION >= 4
            for (Gtk::TreeModel::iterator child = parent->children().begin(); child; ++child) {
            #else
            for (Gtk::TreeModel::iterator child : parent->children()) {
            #endif
                CtTreeIter iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(child);
                std::string node_syntax = iter.get_node_syntax_highlighting();
                if (not iter.get_node_read_only() and node_syntax != new_syntax) {
                    _pCtMainWin->switch_buffer_text_source(iter.get_node_text_buffer(), iter, new_syntax, node_syntax);
                    _pCtMainWin->get_tree_store().update_node_icon(iter);
                    iter.pending_edit_db_node_prop();
                }
                f_iterate_childs(child);
            }
        };
        f_iterate_childs(_pCtMainWin->curr_tree_iter());
        _pCtMainWin->update_window_save_needed();
    }

    // Re-focus the current node so the text view reflects any syntax change on it
    _pCtMainWin->resetPrevTreeIter();
    _pCtMainWin->get_tree_view().set_cursor(_pCtMainWin->get_tree_store().get_path(_pCtMainWin->curr_tree_iter()));
}

void CtActions::node_delete()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    if (not _is_curr_node_not_read_only_or_error()) return;

    CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();

    // Collect IDs and build warning label
    std::list<gint64> nodeIdsToRemove;
    std::list<std::string> lstNodesWarn;
    std::function<void(Gtk::TreeModel::iterator, int)> f_collect;
    f_collect = [&](Gtk::TreeModel::iterator iter, const int level) {
        CtTreeIter ctIter = ctTreeStore.to_ct_tree_iter(iter);
        nodeIdsToRemove.push_back(ctIter.get_node_id());
        if (lstNodesWarn.size() <= 15) {
            lstNodesWarn.push_back(CtConst::CHAR_NEWLINE + str::repeat(CtConst::CHAR_SPACE, level*3) +
                                   _pCtConfig->charsListbul[0] + CtConst::CHAR_SPACE + ctIter.get_node_name());
        } else if (lstNodesWarn.size() == 16) {
            lstNodesWarn.push_back(CtConst::CHAR_NEWLINE + "...");
        }
        #if GTKMM_MAJOR_VERSION >= 4
        for (auto ci = iter->children().begin(); ci; ++ci) f_collect(ci, level + 1);
        #else
        for (Gtk::TreeModel::iterator child : iter->children()) f_collect(child, level + 1);
        #endif
    };
    f_collect(_pCtMainWin->curr_tree_iter(), 0);

    Glib::ustring warning_label = str::format(_("Are you sure to <b>Delete the node '%s'?</b>"),
                                               str::xml_escape(_pCtMainWin->curr_tree_iter().get_node_name()));
    if (nodeIdsToRemove.size() > 1u) {
        warning_label += str::repeat(CtConst::CHAR_NEWLINE, 2) + _("The node <b>has Children, they will be Deleted too!</b>");
        warning_label += str::xml_escape(str::join(lstNodesWarn, ""));
    }
    if (not CtDialogs::question_dialog(warning_label, *_pCtMainWin)) return;

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (pModel) {
        CtTreeIter topCtIter = _pCtMainWin->curr_tree_iter();

        // Build snapshot directly from GTK tree so we capture any unsaved buffer edits
        SubtreeSnapshot snap;
        gint64 snapParentId = 0;
        {
            auto gtkParent = static_cast<Gtk::TreeModel::iterator>(topCtIter)->parent();
            if (gtkParent) {
                CtTreeIter pt = ctTreeStore.to_ct_tree_iter(gtkParent);
                if (pt) snapParentId = pt.get_node_id();
            }
        }
        std::function<void(CtTreeIter, gint64)> walkGtk;
        walkGtk = [&](CtTreeIter iter, gint64 parentId) {
            SubtreeSnapshot::Entry e;
            e.nodeId         = iter.get_node_id();
            e.parentId       = parentId;
            e.sharedMasterId = iter.get_node_shared_master_id();
            e.sequence       = iter.get_node_sequence();
            e.position       = gtkIterPos(static_cast<Gtk::TreeModel::iterator>(iter));
            e.props          = nodePropsFromIter(iter);
            if (e.sharedMasterId == 0) {
                auto buf = iter.get_node_text_buffer();
                if (buf) e.content = buildContentFromBuffer(buf, iter.get_anchored_widgets());
            }
            gint64 thisId = e.nodeId; // capture before move
            snap.entries.push_back(std::move(e));
            #if GTKMM_MAJOR_VERSION >= 4
            for (auto ci = static_cast<Gtk::TreeModel::iterator>(iter)->children().begin(); ci; ++ci)
                walkGtk(ctTreeStore.to_ct_tree_iter(ci), thisId);
            #else
            for (Gtk::TreeModel::iterator child : static_cast<Gtk::TreeModel::iterator>(iter)->children())
                walkGtk(ctTreeStore.to_ct_tree_iter(child), thisId);
            #endif
        };
        walkGtk(topCtIter, snapParentId);

        // Sync snapshot content into model so execute() works correctly
        for (const auto& e : snap.entries) {
            auto node = pModel->getNodeById(e.nodeId);
            if (node && e.sharedMasterId == 0) {
                node->setContent(e.content);
            }
        }

        // Build shared-master promotions
        std::vector<SharedGroupPromotion> promotions;
        CtSharedNodesMap sharedMap;
        if (ctTreeStore.populate_shared_nodes_map(sharedMap) > 0u) {
            for (const auto& currPair : sharedMap) {
                if (vec::exists(nodeIdsToRemove, currPair.first)) {
                    // Old master is being deleted — find a surviving non-master to promote
                    CtTreeIter oldMasterIter = ctTreeStore.get_node_from_node_id(currPair.first);
                    if (!oldMasterIter) continue;
                    for (const gint64 newMasterId : currPair.second) {
                        if (vec::exists(nodeIdsToRemove, newMasterId)) continue;
                        CtTreeIter newMasterIter = ctTreeStore.get_node_from_node_id(newMasterId);
                        if (!newMasterIter) continue;
                        SharedGroupPromotion promo;
                        promo.oldMasterId        = currPair.first;
                        promo.newMasterId        = newMasterId;
                        promo.oldMasterProps     = nodePropsFromIter(oldMasterIter);
                        // Capture old master content from snapshot
                        for (const auto& se : snap.entries) {
                            if (se.nodeId == currPair.first) { promo.oldMasterContent = se.content; break; }
                        }
                        promo.newMasterPriorProps   = nodePropsFromIter(newMasterIter);
                        {
                            auto buf = newMasterIter.get_node_text_buffer();
                            if (buf) promo.newMasterPriorContent = buildContentFromBuffer(buf, newMasterIter.get_anchored_widgets());
                        }
                        // Record members that will be re-pointed to new master
                        for (const gint64 memberId : currPair.second) {
                            if (memberId != newMasterId && !vec::exists(nodeIdsToRemove, memberId)) {
                                promo.rePointed.push_back({memberId, currPair.first});
                            }
                        }
                        promotions.push_back(std::move(promo));
                        break;
                    }
                }
            }
        }

        // Bookmarks snapshot (for undo)
        std::vector<gint64> bookmarkedIds;
        for (gint64 nid : nodeIdsToRemove) {
            if (ctTreeStore.is_node_bookmarked(nid)) bookmarkedIds.push_back(nid);
        }

        // Nav history snapshot (for undo)
        std::vector<gint64> visitedSnapshot(_pCtMainWin->_visitedNodes.begin(),
                                             _pCtMainWin->_visitedNodes.end());
        size_t visitedIdx = _pCtMainWin->_visitedNodesIdx;

        // Determine next-selected node (for re-selection after deletion)
        gint64 nextSelectedId = -1;
        {
            Gtk::TreeModel::iterator ni = --_pCtMainWin->curr_tree_iter();
            if (not ni) ni = ++_pCtMainWin->curr_tree_iter();
            if (not ni) ni = _pCtMainWin->curr_tree_iter().parent();
            if (ni) {
                CtTreeIter nit = ctTreeStore.to_ct_tree_iter(ni);
                if (nit) nextSelectedId = nit.get_node_id();
            }
        }

        pBridge->pushNodeCommand(std::make_unique<DeleteNodeCommand>(
            pModel, _pCtMainWin, snap, std::move(promotions),
            std::move(bookmarkedIds), std::move(visitedSnapshot), visitedIdx,
            nextSelectedId));

        // Update bookmark menu if needed (execute already removed bookmarks via observer)
        _pCtMainWin->menu_set_bookmark_menu_items();
    } else {
        // Bridge not active — delete directly from GTK tree
        CtSharedNodesMap shared_nodes_map;
        if (ctTreeStore.populate_shared_nodes_map(shared_nodes_map) > 0u) {
            for (const auto& currPair : shared_nodes_map) {
                if (vec::exists(nodeIdsToRemove, currPair.first)) {
                    CtTreeIter oldMasterIter = ctTreeStore.get_node_from_node_id(currPair.first);
                    if (oldMasterIter) {
                        for (const gint64 newMasterId : currPair.second) {
                            if (not vec::exists(nodeIdsToRemove, newMasterId)) {
                                CtTreeIter newMasterIter = ctTreeStore.get_node_from_node_id(newMasterId);
                                if (newMasterIter) {
                                    CtNodeData nodeData{};
                                    ctTreeStore.get_node_data(oldMasterIter, nodeData, true/*loadTextBuffer*/);
                                    nodeData.nodeId = newMasterId;
                                    nodeData.sequence = newMasterIter.get_node_sequence();
                                    ctTreeStore.update_node_data(newMasterIter, nodeData);
                                    newMasterIter.pending_edit_db_node_prop();
                                    newMasterIter.pending_edit_db_node_buff();
                                    newMasterIter.pending_edit_db_node_hier();
                                    for (const gint64 nonMasterId : currPair.second) {
                                        if (nonMasterId != newMasterId and not vec::exists(nodeIdsToRemove, nonMasterId)) {
                                            CtTreeIter nonMasterIter = ctTreeStore.get_node_from_node_id(nonMasterId);
                                            if (nonMasterIter) {
                                                nonMasterIter.set_node_shared_master_id(newMasterId);
                                                nonMasterIter.pending_edit_db_node_hier();
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        Gtk::TreeModel::iterator new_iter = --_pCtMainWin->curr_tree_iter();
        if (not new_iter) new_iter = ++_pCtMainWin->curr_tree_iter();
        if (not new_iter) new_iter = _pCtMainWin->curr_tree_iter().parent();
        _pCtMainWin->resetPrevTreeIter();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::ndel);
        Gtk::TreeModel::iterator erase_iter = _pCtMainWin->curr_tree_iter();
        if (new_iter) {
            _pCtMainWin->get_tree_view().set_cursor_safe(new_iter);
            _pCtMainWin->get_text_view().mm().grab_focus();
        } else {
            _curr_buffer()->set_text("");
            _pCtMainWin->window_header_update();
            _pCtMainWin->update_selected_node_statusbar_info();
            _pCtMainWin->get_text_view().mm().set_sensitive(false);
        }
        ctTreeStore.get_store()->erase(erase_iter);
        bool anyBookmark{false};
        for (gint64 nid : nodeIdsToRemove) {
            if (ctTreeStore.bookmarks_remove(nid)) anyBookmark = true;
        }
        if (anyBookmark) {
            _pCtMainWin->menu_set_bookmark_menu_items();
            _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::book);
        }
    }
}

void CtActions::node_toggle_read_only()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    CtTreeIter currTreeIter = _pCtMainWin->curr_tree_iter();
    CtTreeStore& ct_treestore = _pCtMainWin->get_tree_store();

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (pModel) {
        const gint64 thisNodeId = currTreeIter.get_node_id();
        CtNodeProps oldProps = nodePropsFromIter(currTreeIter);
        CtNodeProps newProps = oldProps;
        newProps.isReadOnly = not oldProps.isReadOnly;

        CtSharedNodesMap shared_nodes_map;
        bool isInSharedGroup = false;
        if (ct_treestore.populate_shared_nodes_map(shared_nodes_map) > 0u) {
            const gint64 masterId = currTreeIter.get_node_shared_master_id();
            for (auto& currPair : shared_nodes_map) {
                if (thisNodeId == currPair.first or masterId == currPair.first) {
                    isInSharedGroup = true;
                    auto compound = std::make_unique<CompoundCommand>("[" + std::to_string(thisNodeId) + "] Toggle read-only");
                    currPair.second.insert(currPair.first);
                    for (const gint64 gid : currPair.second) {
                        CtTreeIter other = ct_treestore.get_node_from_node_id(gid);
                        if (other) {
                            CtNodeProps otherOld = nodePropsFromIter(other);
                            CtNodeProps otherNew = otherOld;
                            otherNew.isReadOnly = newProps.isReadOnly;
                            compound->addCommand(std::make_unique<EditNodePropertiesCommand>(pModel, gid, otherOld, otherNew));
                        }
                    }
                    pBridge->pushNodeCommand(std::move(compound));
                    break;
                }
            }
        }
        if (not isInSharedGroup) {
            pBridge->pushNodeCommand(std::make_unique<EditNodePropertiesCommand>(pModel, thisNodeId, oldProps, newProps));
        }
    } else {
        // Bridge not active — update GTK tree directly (fallback)
        const bool node_is_ro = not currTreeIter.get_node_read_only();
        currTreeIter.set_node_read_only(node_is_ro);
        _pCtMainWin->get_text_view().mm().set_editable(not node_is_ro);
        _pCtMainWin->window_header_update_lock_icon(node_is_ro);
        _pCtMainWin->update_selected_node_statusbar_info();
        ct_treestore.update_node_aux_icon(currTreeIter);
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::npro);
    }

    _pCtMainWin->get_text_view().mm().grab_focus();
}

void CtActions::_node_date(const bool from_sel_not_root)
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    const time_t time = std::time(nullptr);
    const Glib::ustring year = str::time_format("%Y", time);
    const Glib::ustring month = str::time_format("%B", time);
    const Glib::ustring day = str::time_format("%d %a", time);

    Gtk::TreeModel::iterator nodeParent;
    if (from_sel_not_root) {
        if (not _is_there_selected_node_or_error()) return;
        nodeParent = _pCtMainWin->curr_tree_iter();
    }
    Gtk::TreeModel::iterator treeIterYear = node_child_exist_or_create(nodeParent, year, false/*focusIfExisting*/);
    Gtk::TreeModel::iterator treeIterMonth = node_child_exist_or_create(treeIterYear, month, false/*focusIfExisting*/);
    (void)node_child_exist_or_create(treeIterMonth, day, true/*focusIfExisting*/);
}

void CtActions::node_up()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
    auto prev_iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(--currIter);
    if (not prev_iter) return;

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (pModel) {
        CtTreeIter ci = _pCtMainWin->curr_tree_iter();
        const gint64 nodeId = ci.get_node_id();
        const int oldPos = gtkIterPos(ci);
        gint64 parentId = 0;
        auto gtkParent = static_cast<Gtk::TreeModel::iterator>(ci)->parent();
        if (gtkParent) {
            CtTreeIter pt = _pCtMainWin->get_tree_store().to_ct_tree_iter(gtkParent);
            if (pt) parentId = pt.get_node_id();
        }
        pBridge->pushNodeCommand(std::make_unique<MoveNodeCommand>(
            pModel, nodeId, parentId, oldPos, parentId, oldPos - 1));
    } else {
        // Bridge not active — swap directly
        _pCtMainWin->get_tree_store().get_store()->iter_swap(_pCtMainWin->curr_tree_iter(), prev_iter);
        auto cur_seq = _pCtMainWin->curr_tree_iter().get_node_sequence();
        auto prev_seq = prev_iter.get_node_sequence();
        _pCtMainWin->curr_tree_iter().set_node_sequence(prev_seq);
        prev_iter.set_node_sequence(cur_seq);
        _pCtMainWin->curr_tree_iter().pending_edit_db_node_hier();
        prev_iter.pending_edit_db_node_hier();
        _pCtMainWin->get_tree_view().set_cursor(_pCtMainWin->get_tree_store().get_path(_pCtMainWin->curr_tree_iter()));
        _pCtMainWin->update_window_save_needed();
    }
}

void CtActions::node_down()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    CtTreeIter currIter = _pCtMainWin->curr_tree_iter();
    auto next_iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(++currIter);
    if (not next_iter) return;

    auto pBridge = _pCtMainWin->get_command_bridge();
    auto* pModel = (pBridge && pBridge->isActive()) ? pBridge->getDocumentModel().get() : nullptr;

    if (pModel) {
        CtTreeIter ci = _pCtMainWin->curr_tree_iter();
        const gint64 nodeId = ci.get_node_id();
        const int oldPos = gtkIterPos(ci);
        gint64 parentId = 0;
        auto gtkParent = static_cast<Gtk::TreeModel::iterator>(ci)->parent();
        if (gtkParent) {
            CtTreeIter pt = _pCtMainWin->get_tree_store().to_ct_tree_iter(gtkParent);
            if (pt) parentId = pt.get_node_id();
        }
        pBridge->pushNodeCommand(std::make_unique<MoveNodeCommand>(
            pModel, nodeId, parentId, oldPos, parentId, oldPos + 1));
    } else {
        // Bridge not active — swap directly
        _pCtMainWin->get_tree_store().get_store()->iter_swap(_pCtMainWin->curr_tree_iter(), next_iter);
        auto cur_seq = _pCtMainWin->curr_tree_iter().get_node_sequence();
        auto next_seq = next_iter.get_node_sequence();
        _pCtMainWin->curr_tree_iter().set_node_sequence(next_seq);
        next_iter.set_node_sequence(cur_seq);
        _pCtMainWin->curr_tree_iter().pending_edit_db_node_hier();
        next_iter.pending_edit_db_node_hier();
        _pCtMainWin->get_tree_view().set_cursor(_pCtMainWin->get_tree_store().get_path(_pCtMainWin->curr_tree_iter()));
        _pCtMainWin->update_window_save_needed();
    }
}

void CtActions::node_right()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    auto prev_iter = --_pCtMainWin->curr_tree_iter();
    if (not prev_iter) return;
    node_move_after(_pCtMainWin->curr_tree_iter(), prev_iter);
    _pCtMainWin->get_tree_store().update_nodes_icon(_pCtMainWin->curr_tree_iter(), true);
}

void CtActions::node_left()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    Gtk::TreeModel::iterator father_iter = _pCtMainWin->curr_tree_iter()->parent();
    if (not father_iter) return;
    node_move_after(_pCtMainWin->curr_tree_iter(), father_iter->parent(), father_iter);
    _pCtMainWin->get_tree_store().update_nodes_icon(_pCtMainWin->curr_tree_iter(), true);
}

void CtActions::node_change_father()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    CtTreeIter old_father_iter = _pCtMainWin->curr_tree_iter().parent();
    CtTreeIter father_iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(CtDialogs::choose_node_dialog(_pCtMainWin,
                                   _pCtMainWin->get_tree_view(), _("Select the New Parent"), &_pCtMainWin->get_tree_store(), _pCtMainWin->curr_tree_iter()));
    if (not father_iter) return;
    gint64 curr_node_id = _pCtMainWin->curr_tree_iter().get_node_id();
    gint64 old_father_node_id = old_father_iter.get_node_id();
    gint64 new_father_node_id = father_iter.get_node_id();
    if (curr_node_id == new_father_node_id) {
        CtDialogs::error_dialog(_("The new parent can't be the very node to move!"), *_pCtMainWin);
        return;
    }
    if (old_father_node_id != -1 && new_father_node_id == old_father_node_id) {
        CtDialogs::info_dialog(_("The new chosen parent is still the old parent!"), *_pCtMainWin);
        return;
    }
    for (CtTreeIter move_towards_top_iter = father_iter.parent(); move_towards_top_iter; move_towards_top_iter = move_towards_top_iter.parent())
        if (move_towards_top_iter.get_node_id() == curr_node_id) {
            CtDialogs::error_dialog(_("The new parent can't be one of his children!"), *_pCtMainWin);
            return;
        }

    node_move_after(_pCtMainWin->curr_tree_iter(), father_iter);
    _pCtMainWin->get_tree_store().update_nodes_icon(_pCtMainWin->curr_tree_iter(), true);
}

bool CtActions::node_move(Gtk::TreeModel::Path src_path, Gtk::TreeModel::Path dest_path, bool only_test_dest)
{
    if (src_path == dest_path) {
        if (not only_test_dest)
            CtDialogs::error_dialog(_("The new parent can't be the very node to move!"), *_pCtMainWin);
        return false;
    }
    if (dest_path.is_descendant(src_path)) {
        if (not only_test_dest)
            CtDialogs::error_dialog(_("The new parent can't be one of his children!"), *_pCtMainWin);
        return false;
    }
    if (only_test_dest)
        return true;

    Gtk::TreeModel::Path father_path{dest_path};
    father_path.up();
    CtTreeIter father_dest_iter = _pCtMainWin->get_tree_store().get_iter(father_path);
    CtTreeIter src_iter = _pCtMainWin->get_tree_store().get_iter(src_path);

    // 3 cases:
    // 1 - dest iter exists - insert before it, or at very first position
    // 2 - dest iter doesn't exist and there're siblings - insert after siblings
    // 3 - dest iter doesn't exist and no siblings - insert as a first child of father

    // case 1
    if (_pCtMainWin->get_tree_store().get_iter(dest_path)) {
        if (dest_path.prev()) {
            CtTreeIter dest_iter = _pCtMainWin->get_tree_store().get_iter(dest_path); // move iter to insert `before` it
            node_move_after(src_iter, father_dest_iter, dest_iter, false);
        } else {
            node_move_after(src_iter, father_dest_iter, CtTreeIter(), true); // put it as first
        }
    } else { // case 2, 3
        if (dest_path.prev()) {
            CtTreeIter dest_iter = _pCtMainWin->get_tree_store().get_iter(dest_path); // put after siblings
            node_move_after(src_iter, father_dest_iter, dest_iter, false);
        } else {
            node_move_after(src_iter, father_dest_iter, CtTreeIter(), true); // put it as first child
        }
    }
    return true;
}

void CtActions::tree_sort_ascending()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (_tree_sort_level_and_sublevels(_pCtMainWin->get_tree_store().get_store()->children(), true)) {
        _pCtMainWin->get_tree_store().nodes_sequences_fix(Gtk::TreeModel::iterator(), true);
        _pCtMainWin->update_window_save_needed();
    }
}

void CtActions::tree_sort_descending()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (_tree_sort_level_and_sublevels(_pCtMainWin->get_tree_store().get_store()->children(), false)) {
        _pCtMainWin->get_tree_store().nodes_sequences_fix(Gtk::TreeModel::iterator(), true);
        _pCtMainWin->update_window_save_needed();
    }
}

void CtActions::node_siblings_sort_ascending()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    Gtk::TreeModel::iterator father_iter = _pCtMainWin->curr_tree_iter()->parent();
    const Gtk::TreeNodeChildren& children = father_iter ? father_iter->children() : _pCtMainWin->get_tree_store().get_store()->children();
    auto need_swap = [this](Gtk::TreeModel::iterator& l, Gtk::TreeModel::iterator& r) { return _need_node_swap(l, r, true); };
    if (CtMiscUtil::node_siblings_sort(_pCtMainWin->get_tree_store().get_store(), children, need_swap)) {
        _pCtMainWin->get_tree_store().nodes_sequences_fix(father_iter, true);
        _pCtMainWin->update_window_save_needed();
    }
}

void CtActions::node_siblings_sort_descending()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    Gtk::TreeModel::iterator father_iter = _pCtMainWin->curr_tree_iter()->parent();
    const Gtk::TreeNodeChildren& children = father_iter ? father_iter->children() : _pCtMainWin->get_tree_store().get_store()->children();
    auto need_swap = [this](Gtk::TreeModel::iterator& l, Gtk::TreeModel::iterator& r) { return _need_node_swap(l, r, false); };
    if (CtMiscUtil::node_siblings_sort(_pCtMainWin->get_tree_store().get_store(), children, need_swap)) {
        _pCtMainWin->get_tree_store().nodes_sequences_fix(father_iter, true);
        _pCtMainWin->update_window_save_needed();
    }
}

// Go to the Previous Visited Node
void CtActions::node_go_back()
{
    if (_pCtMainWin->_visitedNodes.empty() || _pCtMainWin->_visitedNodesIdx == 0) {
        spdlog::debug("node_go_back: no history to go back to");
        return;
    }

    // Move back in history
    _pCtMainWin->_visitedNodesIdx--;
    gint64 target_node_id = _pCtMainWin->_visitedNodes[_pCtMainWin->_visitedNodesIdx];

    spdlog::debug("node_go_back: navigating to node {}", target_node_id);

    // Suppress history tracking while navigating
    _pCtMainWin->_navigatingHistory = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; });

    // Find and select the node
    auto tree_store = _pCtMainWin->get_tree_store().get_store();
    _pCtMainWin->get_tree_store().get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& iter) {
        CtTreeIter ct_iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(iter);
        if (ct_iter && ct_iter.get_node_id() == target_node_id) {
            _pCtMainWin->get_tree_view().set_cursor_safe(iter);
            _pCtMainWin->get_tree_view().scroll_to_row(tree_store->get_path(iter), 0.5);
            return true; // stop iteration
        }
        return false; // continue iteration
    });

    _pCtMainWin->window_header_update();
}

// Go to the Next Visited Node
void CtActions::node_go_forward()
{
    if (_pCtMainWin->_visitedNodes.empty() ||
        _pCtMainWin->_visitedNodesIdx >= _pCtMainWin->_visitedNodes.size() - 1) {
        spdlog::debug("node_go_forward: no history to go forward to");
        return;
    }

    // Move forward in history
    _pCtMainWin->_visitedNodesIdx++;
    gint64 target_node_id = _pCtMainWin->_visitedNodes[_pCtMainWin->_visitedNodesIdx];

    spdlog::debug("node_go_forward: navigating to node {}", target_node_id);

    // Suppress history tracking while navigating
    _pCtMainWin->_navigatingHistory = true;
    auto on_scope_exit = scope_guard([this](void*) { _pCtMainWin->_navigatingHistory = false; });

    // Find and select the node
    auto tree_store = _pCtMainWin->get_tree_store().get_store();
    _pCtMainWin->get_tree_store().get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& iter) {
        CtTreeIter ct_iter = _pCtMainWin->get_tree_store().to_ct_tree_iter(iter);
        if (ct_iter && ct_iter.get_node_id() == target_node_id) {
            _pCtMainWin->get_tree_view().set_cursor_safe(iter);
            _pCtMainWin->get_tree_view().scroll_to_row(tree_store->get_path(iter), 0.5);
            return true; // stop iteration
        }
        return false; // continue iteration
    });

    _pCtMainWin->window_header_update();
}

void CtActions::bookmark_curr_node()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    gint64 node_id = _pCtMainWin->curr_tree_iter().get_node_id();

    if (_pCtMainWin->get_tree_store().bookmarks_add(node_id)) {
        _pCtMainWin->menu_set_bookmark_menu_items();
        _pCtMainWin->get_tree_store().update_node_aux_icon(_pCtMainWin->curr_tree_iter());
        _pCtMainWin->window_header_update_bookmark_icon(true);
        _pCtMainWin->menu_update_bookmark_menu_item(true);
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::book);
    }
}

void CtActions::bookmark_curr_node_remove()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_there_selected_node_or_error()) return;
    gint64 node_id = _pCtMainWin->curr_tree_iter().get_node_id();

    if (_pCtMainWin->get_tree_store().bookmarks_remove(node_id)) {
        _pCtMainWin->menu_set_bookmark_menu_items();
        _pCtMainWin->get_tree_store().update_node_aux_icon(_pCtMainWin->curr_tree_iter());
        _pCtMainWin->window_header_update_bookmark_icon(false);
        _pCtMainWin->menu_update_bookmark_menu_item(false);
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::book);
    }
}

void CtActions::bookmarks_handle()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    CtDialogs::bookmarks_handle_dialog(_pCtMainWin);
}

void CtActions::tree_info()
{
    if (not _is_tree_not_empty_or_error()) return;
    CtSummaryInfo summaryInfo{};
    if (_pCtMainWin->get_tree_store().populate_summary_info(summaryInfo)) {
        CtDialogs::summary_info_dialog(_pCtMainWin, summaryInfo);
    }
}

void CtActions::tree_clear_property_exclude_from_search()
{
    if (_in_action) { spdlog::debug("?? 2*{}", __FUNCTION__); return; }
    _in_action = true;
    auto on_scope_exit = scope_guard([this](void*) { _in_action = false; });

    if (not _is_tree_not_empty_or_error()) return;
    const unsigned nodes_properties_changed = _pCtMainWin->get_tree_store().tree_clear_property_exclude_from_search();
    if (nodes_properties_changed > 0u) {
        _pCtMainWin->window_header_update_ghost_icon(false);
    }
    CtDialogs::info_dialog(str::format(_("%s Nodes Properties Changed"), std::to_string(nodes_properties_changed)), *_pCtMainWin);
}

void CtActions::node_link_to_clipboard()
{
    if (not _is_there_selected_node_or_error()) return;
    CtClipboard(_pCtMainWin).node_link_to_clipboard(_pCtMainWin->curr_tree_iter());
}
