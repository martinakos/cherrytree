/*
 * ct_protected_area.cc
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

#include "ct_protected_area.h"
#include "ct_main_win.h"
#include "ct_storage_xml.h"
#include "ct_command_bridge.h"
#include "ct_logging.h"
#include "ct_command.h"

#include <functional>
#include <ctime>
#include <glibmm/main.h>

namespace CtProtectedAreaSerial {

namespace {

const char PAYLOAD_ROOT_ELEMENT[]{"cherrytree_protected_area"};
const char PAYLOAD_VERSION[]{"1"};

} // anonymous namespace

std::string subtree_to_xml(CtMainWin* pCtMainWin, CtTreeIter& rootIter)
{
    if (not rootIter) {
        spdlog::error("!! {} invalid root iter", __FUNCTION__);
        return std::string{};
    }
    try {
        xmlpp::Document xmlDoc;
        xmlpp::Element* pRootElement = xmlDoc.create_root_node(PAYLOAD_ROOT_ELEMENT);
        pRootElement->set_attribute("version", PAYLOAD_VERSION);

        CtStorageXmlHelper xmlHelper{pCtMainWin};
        std::function<void(CtTreeIter&, xmlpp::Element*)> f_nodeToXmlRecursive;
        f_nodeToXmlRecursive = [&](CtTreeIter& ctTreeIter, xmlpp::Element* pParentElement) {
            // node_to_xml reads the buffer, which forces the lazy load; an area is
            // only ever serialised while unlocked, so it must be there
            if (not ctTreeIter.get_node_text_buffer()) {
                throw std::runtime_error(str::format(_("Failed to retrieve the content of the node '%s'"),
                                                     ctTreeIter.get_node_name().raw()));
            }
            xmlpp::Element* pNodeElement = xmlHelper.node_to_xml(&ctTreeIter,
                                                                 pParentElement,
                                                                 std::string{}/*multifile_dir*/,
                                                                 nullptr/*storage_cache*/,
                                                                 CtExporting::NONESAVE,
                                                                 nullptr/*pExpoMasterReassign*/,
                                                                 0/*start_offset*/,
                                                                 -1/*end_offset*/);
            CtTreeIter ctTreeIterChild = ctTreeIter.first_child();
            while (ctTreeIterChild) {
                f_nodeToXmlRecursive(ctTreeIterChild, pNodeElement);
                ++ctTreeIterChild;
            }
        };
        f_nodeToXmlRecursive(rootIter, pRootElement);

        // Bookmarks pointing into the area travel with it: locking drops the
        // nodes from the tree, and BridgeObserver::onNodeDeleted removes their
        // bookmarks, so without this they would be lost on the first lock.
        {
            CtTreeStore& ctTreeStore = pCtMainWin->get_tree_store();
            std::vector<std::string> bookmarkedIds;
            std::function<void(CtTreeIter)> f_collectBookmarks;
            f_collectBookmarks = [&](CtTreeIter iter) {
                while (iter) {
                    if (ctTreeStore.is_node_bookmarked(iter.get_node_id())) {
                        bookmarkedIds.push_back(std::to_string(iter.get_node_id()));
                    }
                    f_collectBookmarks(iter.first_child());
                    ++iter;
                }
            };
            f_collectBookmarks(rootIter);
            if (not bookmarkedIds.empty()) {
                pRootElement->set_attribute("bookmarks", str::join(bookmarkedIds, ","));
            }
        }
        return xmlDoc.write_to_string();
    }
    catch (std::exception& e) {
        spdlog::error("!! {} {}", __FUNCTION__, e.what());
        return std::string{};
    }
}

bool subtree_from_xml(CtMainWin* pCtMainWin, const std::string& xmlText, CtTreeIter& rootIter)
{
    if (not rootIter) {
        spdlog::error("!! {} invalid root iter", __FUNCTION__);
        return false;
    }
    try {
        xmlpp::DomParser xmlParser;
        xmlParser.parse_memory(xmlText);
        if (not xmlParser) return false;
        xmlpp::Element* pRootElement = xmlParser.get_document()->get_root_node();
        if (not pRootElement or pRootElement->get_name() != PAYLOAD_ROOT_ELEMENT) {
            spdlog::error("!! {} unexpected payload root element", __FUNCTION__);
            return false;
        }
        // exactly one <node>, the protected root itself
        xmlpp::Element* pProtectedRootNodeElement{nullptr};
        for (xmlpp::Node* pNode : pRootElement->get_children()) {
            if (pNode->get_name() == "node") {
                if (pProtectedRootNodeElement) {
                    spdlog::error("!! {} more than one root node in the payload", __FUNCTION__);
                    return false;
                }
                pProtectedRootNodeElement = static_cast<xmlpp::Element*>(pNode);
            }
        }
        if (not pProtectedRootNodeElement) {
            spdlog::error("!! {} no node element in the payload", __FUNCTION__);
            return false;
        }

        CtStorageXmlHelper xmlHelper{pCtMainWin};
        CtTreeStore& ctTreeStore = pCtMainWin->get_tree_store();

        // the root node already exists in the tree, only its content comes back
        const std::string rootSyntax = pProtectedRootNodeElement->get_attribute_value("prog_lang");
        std::list<CtAnchoredWidget*> rootWidgets;
        Glib::RefPtr<Gtk::TextBuffer> pRootBuffer = xmlHelper.create_buffer_and_widgets_from_xml(
            pProtectedRootNodeElement, rootSyntax, rootWidgets, nullptr/*text_insert_pos*/,
            -1/*force_offset*/, std::string{}/*multifile_dir*/);
        if (not pRootBuffer) {
            spdlog::error("!! {} could not rebuild the root buffer", __FUNCTION__);
            return false;
        }
        rootIter.set_node_text_buffer(pRootBuffer, rootSyntax);
        if (not rootWidgets.empty()) {
            ctTreeStore.addAnchoredWidgets(rootIter, rootWidgets, &pCtMainWin->get_text_view().mm());
        }
        auto* pBridge = pCtMainWin->get_command_bridge();
        if (pBridge and pBridge->isActive()) {
            auto pNodeModel = pBridge->getDocumentModel()->getNodeById(rootIter.get_node_id());
            if (pNodeModel) {
                pNodeModel->getDrawingCanvasesMut() = CtXmlHelper::drawing_canvases_from_xml(pProtectedRootNodeElement);
            }
        }

        // and the descendants are appended back under it, keeping their ids;
        // a new_id other than -1 also makes the buffers build eagerly, which is
        // what we want because the decrypted payload is thrown away right after
        CtDelayedTextBufferMap unusedDelayedBuffers;
        std::function<void(xmlpp::Element*, Gtk::TreeModel::iterator)> f_childrenFromXmlRecursive;
        f_childrenFromXmlRecursive = [&](xmlpp::Element* pParentElement, Gtk::TreeModel::iterator parentIter) {
            gint64 sequence{0};
            for (xmlpp::Node* pNode : pParentElement->get_children()) {
                if (pNode->get_name() != "node") continue;
                xmlpp::Element* pNodeElement = static_cast<xmlpp::Element*>(pNode);
                ++sequence;
                const gint64 nodeId = CtStrUtil::gint64_from_gstring(
                    pNodeElement->get_attribute_value("unique_id").c_str());
                Gtk::TreeModel::iterator newIter = xmlHelper.node_from_xml(pNodeElement,
                                                                          sequence,
                                                                          parentIter,
                                                                          nodeId/*new_id, keeps the id*/,
                                                                          nullptr/*pHasDuplicatedId*/,
                                                                          nullptr/*pIsSharedNonMaster*/,
                                                                          nullptr/*pImportedIdsRemap*/,
                                                                          unusedDelayedBuffers,
                                                                          false/*isDryRun*/,
                                                                          std::string{}/*multifile_dir*/);
                if (not newIter) {
                    spdlog::error("!! {} could not restore node {}", __FUNCTION__, nodeId);
                    continue;
                }
                f_childrenFromXmlRecursive(pNodeElement, newIter);
            }
        };
        f_childrenFromXmlRecursive(pProtectedRootNodeElement, rootIter);

        // restore the bookmarks that were saved with the area
        const Glib::ustring bookmarksAttr = pRootElement->get_attribute_value("bookmarks");
        if (not bookmarksAttr.empty()) {
            bool anyAdded{false};
            for (const std::string& idStr : str::split(bookmarksAttr.raw(), ",")) {
                if (idStr.empty()) continue;
                const gint64 bookmarkNodeId = CtStrUtil::gint64_from_gstring(idStr.c_str());
                if (ctTreeStore.bookmarks_add(bookmarkNodeId)) anyAdded = true;
            }
            if (anyAdded) {
                ctTreeStore.pending_edit_db_bookmarks();
                pCtMainWin->menu_set_bookmark_menu_items();
            }
        }
        return true;
    }
    catch (std::exception& e) {
        spdlog::error("!! {} {}", __FUNCTION__, e.what());
        return false;
    }
}

} // namespace CtProtectedAreaSerial

// ── CtProtectedAreas ───────────────────────────────────────────────────────

void CtProtectedAreas::load_records(std::vector<CtProtectedAreaRecord> records,
                                    const std::vector<gint64>& staleAreaIds)
{
    clear();
    for (CtProtectedAreaRecord& record : records) {
        _records[record.nodeId] = std::move(record);
    }
    // orphaned blobs the storage spotted: clear them out on the next save
    for (const gint64 staleId : staleAreaIds) {
        _areaIdsToRemove.insert(staleId);
    }
    if (not staleAreaIds.empty()) {
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, false/*new_machine_state*/);
    }
    // the tree was built before the areas were known, so the roots still show
    // whatever aux icon they had; refresh them now
    CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();
    for (const auto& recordPair : _records) {
        CtTreeIter rootIter = ctTreeStore.get_node_from_node_id(recordPair.first);
        if (rootIter) ctTreeStore.update_node_aux_icon(rootIter);
    }
}

std::vector<gint64> CtProtectedAreas::take_area_ids_to_remove()
{
    std::vector<gint64> ids{_areaIdsToRemove.begin(), _areaIdsToRemove.end()};
    _areaIdsToRemove.clear();
    return ids;
}

void CtProtectedAreas::clear()
{
    timer_stop();
    for (auto& keyPair : _derivedKeys) {
        CtCrypto::wipe(keyPair.second);
    }
    _derivedKeys.clear();
    _records.clear();
}

std::vector<gint64> CtProtectedAreas::area_ids() const
{
    std::vector<gint64> ids;
    ids.reserve(_records.size());
    for (const auto& recordPair : _records) ids.push_back(recordPair.first);
    return ids;
}

gint64 CtProtectedAreas::enclosing_area_id(CtTreeIter iter) const
{
    while (iter) {
        const gint64 nodeId = iter.get_node_id();
        if (is_protected_root(nodeId)) return nodeId;
        iter = iter.parent();
    }
    return 0;
}

std::vector<gint64> CtProtectedAreas::_descendant_ids(CtTreeIter rootIter) const
{
    std::vector<gint64> ids;
    if (not rootIter) return ids;
    std::function<void(CtTreeIter)> f_collect;
    f_collect = [&](CtTreeIter iter) {
        while (iter) {
            ids.push_back(iter.get_node_id());
            f_collect(iter.first_child());
            ++iter;
        }
    };
    f_collect(rootIter.first_child());
    return ids;
}

std::set<gint64> CtProtectedAreas::node_ids_owned_by_areas() const
{
    std::set<gint64> ids;
    if (_records.empty()) return ids;
    CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();
    for (const auto& recordPair : _records) {
        CtTreeIter rootIter = ctTreeStore.get_node_from_node_id(recordPair.first);
        if (not rootIter) continue; // locked: the descendants are not in the tree
        for (const gint64 nodeId : _descendant_ids(rootIter)) ids.insert(nodeId);
    }
    return ids;
}

void CtProtectedAreas::_drop_descendants(CtTreeIter rootIter)
{
    auto* pBridge = _pCtMainWin->get_command_bridge();
    if (not pBridge or not pBridge->isActive()) {
        spdlog::error("!! {} the command bridge is required", __FUNCTION__);
        return;
    }
    // Remove through the model and let BridgeObserver::onNodeDeleted do the rest:
    // it erases the GTK rows, cleans the navigation history and the bookmarks,
    // moves the cursor off a node about to die, and marks the rows for removal
    // from the database. Reimplementing that here would miss the stale iterator
    // guard it carries.
    std::vector<gint64> childIds;
    for (CtTreeIter child = rootIter.first_child(); child; ++child) {
        childIds.push_back(child.get_node_id());
    }
    auto pDocModel = pBridge->getDocumentModel();
    for (const gint64 childId : childIds) {
        if (pDocModel->getNodeById(childId)) {
            pDocModel->removeNodeWithChildren(childId);
        }
    }
}

bool CtProtectedAreas::protect(CtTreeIter rootIter, const std::string& password, Glib::ustring& rError)
{
    if (not rootIter) { rError = _("Invalid node."); return false; }
    if (password.empty()) { rError = _("The Password Fields Must be Filled."); return false; }

    const gint64 rootNodeId = rootIter.get_node_id();
    if (is_protected_root(rootNodeId)) {
        rError = _("This node is already password protected.");
        return false;
    }
    if (0 != enclosing_area_id(rootIter)) {
        rError = _("This node is already inside a password protected area.");
        return false;
    }
    for (const gint64 descendantId : _descendant_ids(rootIter)) {
        if (is_protected_root(descendantId)) {
            rError = _("A password protected area cannot contain another one.");
            return false;
        }
    }
    // A shared node group must sit wholly inside the area or wholly outside it.
    // Locking removes the nodes inside from the tree, and an instance left
    // outside would then point at a master that is no longer there.
    {
        std::set<gint64> insideIds{rootNodeId};
        for (const gint64 descendantId : _descendant_ids(rootIter)) insideIds.insert(descendantId);
        CtSharedNodesMap sharedNodesMap;
        _pCtMainWin->get_tree_store().populate_shared_nodes_map(sharedNodesMap);
        for (const auto& groupPair : sharedNodesMap) {
            std::set<gint64> wholeGroup = groupPair.second;
            wholeGroup.insert(groupPair.first); // the master itself
            unsigned numInside{0u};
            for (const gint64 groupNodeId : wholeGroup) {
                if (0u != insideIds.count(groupNodeId)) ++numInside;
            }
            if (0u != numInside and wholeGroup.size() != numInside) {
                rError = _("This area contains a shared node whose other instances are outside it.\n"
                           "Move them in or out before protecting the area.");
                return false;
            }
        }
    }

    const std::string payloadXml = CtProtectedAreaSerial::subtree_to_xml(_pCtMainWin, rootIter);
    if (payloadXml.empty()) { rError = _("Failed to read the content of the area."); return false; }

    CtProtectedAreaRecord record;
    record.nodeId = rootNodeId;
    record.tsLastSave = std::time(nullptr);
    if (not CtCrypto::seal(payloadXml, password, record.envelope)) {
        rError = _("Failed to encrypt the content of the area.");
        return false;
    }
    std::string derivedKey = CtCrypto::derive_key(password, record.envelope.kdfSalt, record.envelope.kdfIterations);
    if (derivedKey.empty()) { rError = _("Failed to derive the encryption key."); return false; }

    _records[rootNodeId] = std::move(record);
    _derivedKeys[rootNodeId] = std::move(derivedKey);
    _pCtMainWin->get_tree_store().update_node_aux_icon(rootIter);

    // the descendants keep living in the tree while the area is unlocked, but
    // their rows must go: from now on they exist only inside the blob
    std::vector<gint64> descendantIds = _descendant_ids(rootIter);
    if (not descendantIds.empty()) {
        _pCtMainWin->get_tree_store().pending_rm_db_nodes(descendantIds);
    }
    // and the root's own row must be rewritten, or the plaintext that was
    // already saved in its txt column would simply stay there
    rootIter.pending_edit_db_node_prop();
    rootIter.pending_edit_db_node_buff();
    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::npro, false/*new_machine_state*/, &rootIter);
    timer_restart();
    return true;
}

bool CtProtectedAreas::_reseal_from_tree(const gint64 nodeId, Glib::ustring& rError)
{
    auto itRecord = _records.find(nodeId);
    auto itKey = _derivedKeys.find(nodeId);
    if (_records.end() == itRecord or _derivedKeys.end() == itKey) {
        rError = _("The area is not unlocked.");
        return false;
    }
    CtTreeIter rootIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (not rootIter) { rError = _("Invalid node."); return false; }

    const std::string payloadXml = CtProtectedAreaSerial::subtree_to_xml(_pCtMainWin, rootIter);
    if (payloadXml.empty()) { rError = _("Failed to read the content of the area."); return false; }
    // seal_with_key keeps the salt, so the derived key stays valid and the
    // expensive derivation is not repeated; only the iv is fresh
    if (not CtCrypto::seal_with_key(payloadXml, itKey->second, itRecord->second.envelope)) {
        rError = _("Failed to encrypt the content of the area.");
        return false;
    }
    itRecord->second.tsLastSave = std::time(nullptr);
    return true;
}

bool CtProtectedAreas::lock(const gint64 nodeId, Glib::ustring& rError)
{
    if (not is_protected_root(nodeId)) { rError = _("This node is not password protected."); return false; }
    if (not is_unlocked(nodeId)) return true; // already locked, nothing to do

    CtTreeIter rootIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (not rootIter) { rError = _("Invalid node."); return false; }

    if (not _reseal_from_tree(nodeId, rError)) return false;

    // Undo must not be able to resurrect the plaintext of nodes that are about
    // to leave the tree, so every entry touching them goes off both stacks.
    {
        std::set<gint64> purgeIds;
        purgeIds.insert(nodeId); // the root's own content is cleared too
        for (const gint64 descendantId : _descendant_ids(rootIter)) purgeIds.insert(descendantId);
        auto* pBridge = _pCtMainWin->get_command_bridge();
        if (pBridge and pBridge->isActive()) {
            pBridge->getCommandManager().purgeCommandsForNodes(purgeIds);
        }
    }

    // Was the cursor inside the area, the root included? If so it has to leave
    // before the nodes are torn down: BridgeObserver::onNodeDeleted re-selects a
    // neighbour when the selected node is deleted, and doing that in the middle
    // of dropping a whole subtree re-enters and hangs. The target is picked now,
    // while the tree is whole, and it is outside the area so it cannot be one of
    // the nodes about to go.
    _drop_descendants(rootIter);

    // and the root keeps its row but not its content
    rootIter.remove_all_embedded_widgets();
    rootIter.set_node_text_buffer(_pCtMainWin->get_new_text_buffer(), rootIter.get_node_syntax_highlighting());

    auto itKey = _derivedKeys.find(nodeId);
    if (_derivedKeys.end() != itKey) {
        CtCrypto::wipe(itKey->second);
        _derivedKeys.erase(itKey);
    }

    // The text view keeps rendering the buffer object it was handed, so
    // replacing a node's buffer does not by itself change what is on screen:
    // without this the content stayed visible after the area had closed.
    // Re-applying whatever ends up selected is enough; moving the cursor is left
    // to BridgeObserver::onNodeDeleted, which already does it when the selected
    // node is one of the ones being dropped.
    CtTreeIter currIterAfter = _pCtMainWin->curr_tree_iter();
    if (currIterAfter) {
        _pCtMainWin->get_tree_store().text_view_apply_textbuffer(currIterAfter, &_pCtMainWin->get_text_view());
    }

    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::npro, false/*new_machine_state*/, &rootIter);
    return true;
}

void CtProtectedAreas::lock_all()
{
    Glib::ustring error;
    for (const gint64 nodeId : area_ids()) {
        if (is_unlocked(nodeId) and not lock(nodeId, error)) {
            spdlog::error("!! {} node {}: {}", __FUNCTION__, nodeId, error.raw());
        }
    }
}

bool CtProtectedAreas::unlock(const gint64 nodeId, const std::string& password, Glib::ustring& rError)
{
    auto itRecord = _records.find(nodeId);
    if (_records.end() == itRecord) { rError = _("This node is not password protected."); return false; }
    if (is_unlocked(nodeId)) return true;

    CtTreeIter rootIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (not rootIter) { rError = _("Invalid node."); return false; }

    std::string derivedKey = CtCrypto::derive_key(password,
                                                  itRecord->second.envelope.kdfSalt,
                                                  itRecord->second.envelope.kdfIterations);
    std::string payloadXml;
    if (not CtCrypto::unseal_with_key(itRecord->second.envelope, derivedKey, payloadXml)) {
        CtCrypto::wipe(derivedKey);
        rError = _("Wrong Password");
        return false;
    }
    const bool restored = CtProtectedAreaSerial::subtree_from_xml(_pCtMainWin, payloadXml, rootIter);
    CtCrypto::wipe(payloadXml);
    if (not restored) {
        CtCrypto::wipe(derivedKey);
        rError = _("Failed to restore the content of the area.");
        return false;
    }

    // the restored nodes exist in the GTK tree only; register them in the model.
    // CtDocumentModel::addNode does not notify, so this cannot create duplicate
    // GTK rows (unlike removeNodeWithChildren, which does notify)
    auto* pBridge = _pCtMainWin->get_command_bridge();
    if (pBridge and pBridge->isActive()) {
        for (CtTreeIter child = rootIter.first_child(); child; ++child) {
            pBridge->registerSubtreeInModel(child, nodeId);
        }
    }
    _derivedKeys[nodeId] = std::move(derivedKey);
    _pCtMainWin->get_tree_store().update_node_aux_icon(rootIter);
    timer_restart();
    return true;
}

bool CtProtectedAreas::unprotect(const gint64 nodeId, const std::string& password, Glib::ustring& rError)
{
    if (not is_protected_root(nodeId)) { rError = _("This node is not password protected."); return false; }
    // the password is required even when already unlocked, so that walking away
    // from an unlocked screen is not enough to strip the protection
    if (not unlock(nodeId, password, rError)) return false;

    CtTreeIter rootIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (not rootIter) { rError = _("Invalid node."); return false; }

    auto itKey = _derivedKeys.find(nodeId);
    if (_derivedKeys.end() != itKey) {
        CtCrypto::wipe(itKey->second);
        _derivedKeys.erase(itKey);
    }
    _records.erase(nodeId);
    // the blob must be deleted from the document as well, otherwise reopening
    // reads it back and the area returns from the dead
    _areaIdsToRemove.insert(nodeId);
    _pCtMainWin->get_tree_store().update_node_aux_icon(rootIter);

    // the root's own content moves back out of the blob and into its row
    rootIter.pending_edit_db_node_prop();
    rootIter.pending_edit_db_node_buff();

    // the descendants get rows of their own again
    CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();
    for (const gint64 descendantId : _descendant_ids(rootIter)) {
        CtTreeIter descendantIter = ctTreeStore.get_node_from_node_id(descendantId);
        if (descendantIter) descendantIter.pending_new_db_node();
    }
    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::npro, false/*new_machine_state*/, &rootIter);
    return true;
}

std::vector<CtProtectedAreaRecord> CtProtectedAreas::records_for_save()
{
    std::vector<CtProtectedAreaRecord> records;
    records.reserve(_records.size());
    Glib::ustring error;
    for (auto& recordPair : _records) {
        // an unlocked area is re-sealed from what is currently in the tree; a
        // locked one already holds the bytes that must be written
        if (is_unlocked(recordPair.first) and not _reseal_from_tree(recordPair.first, error)) {
            spdlog::error("!! {} node {}: {}", __FUNCTION__, recordPair.first, error.raw());
        }
        records.push_back(recordPair.second);
    }
    return records;
}

void CtProtectedAreas::timer_stop()
{
    _lockTimerConnection.disconnect();
    _idleMinutes = 0;
}

void CtProtectedAreas::timer_restart()
{
    timer_stop();
    if (_records.empty()) return; // nothing to relock in this document
    const int lockMinutes = _pCtMainWin->get_ct_config()->protectedAreaLockMinutes;
    if (lockMinutes < 1) {
        spdlog::debug("protected areas: inactivity relock disabled");
        return;
    }
    spdlog::debug("protected areas: relock after {} idle min", lockMinutes);
    // same shape as CtMainWin::file_autosave_restart: tick a minute at a time
    _lockTimerConnection = Glib::signal_timeout().connect_seconds([this, lockMinutes]() {
        if (++_idleMinutes < lockMinutes) return true;
        _idleMinutes = 0;
        bool anyUnlocked{false};
        for (const gint64 nodeId : area_ids()) {
            if (is_unlocked(nodeId)) { anyUnlocked = true; break; }
        }
        if (anyUnlocked) {
            spdlog::debug("protected areas: relocking after {} idle min", lockMinutes);
            lock_all();
        }
        return true;
    }, 60/*1 min iter*/);
}

bool CtProtectedAreas::change_password(const gint64 nodeId, const std::string& newPassword, Glib::ustring& rError)
{
    if (not is_protected_root(nodeId)) { rError = _("This node is not password protected."); return false; }
    if (not is_unlocked(nodeId)) { rError = _("The area is not unlocked."); return false; }
    if (newPassword.empty()) { rError = _("The Password Fields Must be Filled."); return false; }

    CtTreeIter rootIter = _pCtMainWin->get_tree_store().get_node_from_node_id(nodeId);
    if (not rootIter) { rError = _("Invalid node."); return false; }

    const std::string payloadXml = CtProtectedAreaSerial::subtree_to_xml(_pCtMainWin, rootIter);
    if (payloadXml.empty()) { rError = _("Failed to read the content of the area."); return false; }

    // a brand new salt, so the old derived key cannot open the new blob
    CtCrypto::CtEncryptedEnvelope envelope;
    if (not CtCrypto::seal(payloadXml, newPassword, envelope)) {
        rError = _("Failed to encrypt the content of the area.");
        return false;
    }
    std::string derivedKey = CtCrypto::derive_key(newPassword, envelope.kdfSalt, envelope.kdfIterations);
    if (derivedKey.empty()) { rError = _("Failed to derive the encryption key."); return false; }

    auto itKey = _derivedKeys.find(nodeId);
    if (_derivedKeys.end() != itKey) CtCrypto::wipe(itKey->second);
    _derivedKeys[nodeId] = std::move(derivedKey);
    _records[nodeId].envelope = std::move(envelope);
    _records[nodeId].tsLastSave = std::time(nullptr);
    return true;
}

