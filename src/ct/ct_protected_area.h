/*
 * ct_protected_area.h
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

#pragma once

#include "ct_crypto.h"
#include <glib.h>
#include <glibmm/ustring.h>
#include <sigc++/connection.h>
#include <map>
#include <set>
#include <string>
#include <vector>

class CtMainWin;
class CtTreeIter;

// One password protected area: the subtree rooted at nodeId, serialised to XML
// and encrypted. While the area is locked these bytes are all that exists of the
// descendants; they have no rows of their own in any table.
struct CtProtectedAreaRecord
{
    gint64                        nodeId{0};
    CtCrypto::CtEncryptedEnvelope envelope;
    gint64                        tsLastSave{0};
};

// Serialisation of a protected subtree to and from the XML that goes inside the
// encrypted payload. The schema is the one the .ctd backend already uses, so the
// existing serialisers cover rich text, images, codeboxes, tables, anchors and
// drawing canvases; only the subtree walk lives here, because
// CtStorageXmlHelper::node_to_xml handles a single node and the recursion in
// CtStorageXml::_nodes_to_xml is private to that backend.
namespace CtProtectedAreaSerial {

// The subtree rooted at rootIter, the root node itself included, as a standalone
// XML document. Empty string on failure.
std::string subtree_to_xml(CtMainWin* pCtMainWin, CtTreeIter& rootIter);

// Rebuild from such a document: the root's own content is applied to rootIter and
// the descendants are appended beneath it. rootIter must have no children.
bool subtree_from_xml(CtMainWin* pCtMainWin, const std::string& xmlText, CtTreeIter& rootIter);

} // namespace CtProtectedAreaSerial

// Runtime owner of the password protected areas of the open document.
//
// Holds only CtMainWin*: CtMainWin::_reset_CtTreestore_CtTreeview() destroys and
// rebuilds the tree store on every document open or save as, so a cached
// CtTreeStore& or CtTreeView* would dangle.
//
// The password is a parameter rather than something this class prompts for, so
// that it stays testable without a display. The dialogs live in the UI layer.
class CtProtectedAreas
{
public:
    explicit CtProtectedAreas(CtMainWin* pCtMainWin) : _pCtMainWin{pCtMainWin} {}
    ~CtProtectedAreas() { clear(); }

    CtProtectedAreas(const CtProtectedAreas&) = delete;
    CtProtectedAreas& operator=(const CtProtectedAreas&) = delete;

    // document lifecycle
    // every area starts locked; staleAreaIds are blobs the storage found orphaned
    void load_records(std::vector<CtProtectedAreaRecord> records,
                      const std::vector<gint64>& staleAreaIds = {});
    void clear();                                                  // wipes the derived keys

    // queries
    bool   has_any() const { return not _records.empty(); }
    bool   is_protected_root(const gint64 nodeId) const { return 0u != _records.count(nodeId); }
    bool   is_unlocked(const gint64 nodeId) const { return 0u != _derivedKeys.count(nodeId); }
    bool   is_locked(const gint64 nodeId) const { return is_protected_root(nodeId) and not is_unlocked(nodeId); }
    // the protected root at or above this node, 0 when the node is not in an area
    gint64 enclosing_area_id(CtTreeIter iter) const;
    std::vector<gint64> area_ids() const;

    // Node ids that must not get rows of their own: every descendant of every
    // protected root currently present in the tree. The roots themselves keep
    // their rows, so they stay visible while locked.
    std::set<gint64> node_ids_owned_by_areas() const;

    // lifecycle of one area
    bool protect(CtTreeIter rootIter, const std::string& password, Glib::ustring& rError);
    bool unprotect(const gint64 nodeId, const std::string& password, Glib::ustring& rError);
    bool unlock(const gint64 nodeId, const std::string& password, Glib::ustring& rError);
    // re-seals an already unlocked area under a new password, with a fresh salt
    bool change_password(const gint64 nodeId, const std::string& newPassword, Glib::ustring& rError);
    bool lock(const gint64 nodeId, Glib::ustring& rError);
    void lock_all();

    // storage integration: re-seals the areas that are unlocked
    std::vector<CtProtectedAreaRecord> records_for_save();
    // Areas whose blob must be deleted from the document, because the protection
    // was removed or the record turned out to be stale. Cleared once consumed.
    std::vector<gint64> take_area_ids_to_remove();

    // Set when a search or an export walks past a locked area, so the user can
    // be told that something was skipped. Mirrors CtTreeIter's exclusion flag.
    static bool get_hit_locked_area() { return _hitLockedArea; }
    static void clear_hit_locked_area() { _hitLockedArea = false; }
    static void note_hit_locked_area() { _hitLockedArea = true; }

    // Inactivity relock. The timer ticks once a minute and counts idle minutes;
    // note_activity() resets the count and is called from the key press, text
    // view and tree selection handlers.
    void timer_restart();
    void timer_stop();
    void note_activity() { _idleMinutes = 0; }

private:
    // collects the ids of every node below rootIter, deepest last
    std::vector<gint64> _descendant_ids(CtTreeIter rootIter) const;
    // drops the descendants from the model, which makes the observer tear down
    // the GTK rows, the nav history, the bookmarks and the pending db state
    void _drop_descendants(CtTreeIter rootIter);
    bool _reseal_from_tree(const gint64 nodeId, Glib::ustring& rError);

    inline static bool _hitLockedArea{false};

    std::set<gint64> _areaIdsToRemove;

    CtMainWin* const _pCtMainWin;
    sigc::connection _lockTimerConnection;
    int _idleMinutes{0};
    std::map<gint64, CtProtectedAreaRecord> _records;
    std::map<gint64, std::string>           _derivedKeys; // unlocked areas only
};
