/*
 * ct_pref_dlg_menubar.cc
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

#include "ct_pref_dlg.h"
#include "ct_main_win.h"

Gtk::Widget* CtPrefDlg::build_tab_menubar()
{
    struct MenuIdLabel { std::string id; Glib::ustring label; };
    static const std::vector<MenuIdLabel> topLevelMenus = {
        {"FileMenu",   _("File")},
        {"EditMenu",   _("Edit")},
        {"InsertMenu", _("Insert")},
        {"FormatMenu", _("Format")},
        {"ToolsMenu",  _("Tools")},
        {"TreeMenu",   _("Tree")},
        {"SearchMenu", _("Search")},
        {"ViewMenu",   _("View")},
        {"HelpMenu",   _("Help")},
    };

    Glib::RefPtr<Gtk::ListStore> liststore = Gtk::ListStore::create(_menuEditorModelColumns);
    Gtk::TreeView* treeview = Gtk::manage(new Gtk::TreeView(liststore));
    treeview->set_headers_visible(false);
    treeview->set_reorderable(true);
    treeview->set_size_request(300, 300);

    Gtk::CellRendererPixbuf pixbuf_renderer;
    pixbuf_renderer.property_stock_size() = Gtk::BuiltinIconSize::ICON_SIZE_LARGE_TOOLBAR;
    const int col_num_pixbuf = treeview->append_column("", pixbuf_renderer) - 1;
    treeview->get_column(col_num_pixbuf)->add_attribute(pixbuf_renderer, "icon-name", _menuEditorModelColumns.icon);
    treeview->append_column("", _menuEditorModelColumns.desc);

    Gtk::ScrolledWindow* scrolledwindow = Gtk::manage(new Gtk::ScrolledWindow());
    scrolledwindow->add(*treeview);

    auto pCombo = Gtk::manage(new Gtk::ComboBoxText());
    for (const auto& m : topLevelMenus) {
        pCombo->append(m.id, m.label);
    }
    pCombo->set_active(0);

    auto pBtnUp = Gtk::manage(new Gtk::Button());
    pBtnUp->set_image(*_pCtMainWin->new_managed_image_from_stock("ct_go-up", Gtk::ICON_SIZE_BUTTON));
    pBtnUp->set_tooltip_text(_("Move Menu Up"));
    auto pBtnDown = Gtk::manage(new Gtk::Button());
    pBtnDown->set_image(*_pCtMainWin->new_managed_image_from_stock("ct_go-down", Gtk::ICON_SIZE_BUTTON));
    pBtnDown->set_tooltip_text(_("Move Menu Down"));

    auto hboxTopLevel = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4});
    hboxTopLevel->pack_start(*Gtk::manage(new Gtk::Label{_("Menu:")}), false, false);
    hboxTopLevel->pack_start(*pCombo, true, true);
    hboxTopLevel->pack_start(*pBtnUp, false, false);
    hboxTopLevel->pack_start(*pBtnDown, false, false);

    Gtk::Button* button_add = Gtk::manage(new Gtk::Button());
    button_add->set_image(*_pCtMainWin->new_managed_image_from_stock("ct_add", Gtk::ICON_SIZE_BUTTON));
    button_add->set_tooltip_text(_("Add"));
    Gtk::Button* button_remove = Gtk::manage(new Gtk::Button());
    button_remove->set_image(*_pCtMainWin->new_managed_image_from_stock("ct_remove", Gtk::ICON_SIZE_BUTTON));
    button_remove->set_tooltip_text(_("Remove Selected"));
    Gtk::Button* button_reset = Gtk::manage(new Gtk::Button());
    button_reset->set_image(*_pCtMainWin->new_managed_image_from_stock("ct_undo", Gtk::ICON_SIZE_BUTTON));
    button_reset->set_tooltip_text(_("Reset to Default"));

    auto vboxButtons = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL});
    vboxButtons->pack_start(*button_add, false, false);
    vboxButtons->pack_start(*button_remove, false, false);
    vboxButtons->pack_start(*Gtk::manage(new Gtk::Label()), true, true);
    vboxButtons->pack_start(*button_reset, false, false);

    auto hboxEditor = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL});
    hboxEditor->pack_start(*scrolledwindow, true, true);
    hboxEditor->pack_start(*vboxButtons, false, false);

    auto pMainBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 3});
    pMainBox->pack_start(*hboxTopLevel, false, false);
    pMainBox->pack_start(*hboxEditor, true, true);

    auto currentMenuId = std::make_shared<std::string>(topLevelMenus[0].id);

    auto loadMenu = [this, liststore, treeview, currentMenuId]() {
        std::string& configStr = _pCtMenu->getMenubarSubmenuConfig(*currentMenuId);
        fill_menu_editor_model(liststore, configStr);
    #if GTKMM_MAJOR_VERSION >= 4
        {
            auto const_iter = liststore->children().begin();
            if (const_iter)
                treeview->get_selection()->select(liststore->get_iter(liststore->get_path(const_iter)));
        }
    #else
        if (not liststore->children().empty())
            treeview->get_selection()->select(Gtk::TreePath("0"));
    #endif
    };

    auto saveAndRebuild = [this, liststore, currentMenuId]() {
        std::string& configStr = _pCtMenu->getMenubarSubmenuConfig(*currentMenuId);
        configStr = update_config_from_menu_editor(liststore);
        apply_for_each_window([](CtMainWin* win) { win->menu_rebuild_menubar(); });
    };

    loadMenu();

    pCombo->signal_changed().connect([pCombo, currentMenuId, loadMenu]() {
        *currentMenuId = pCombo->get_active_id();
        loadMenu();
    });

    auto reorderTopLevel = [this, pCombo](int direction) {
        std::vector<std::string> order = str::split(_pConfig->menubarTopLevelOrder, ",");
        std::string activeId = pCombo->get_active_id();
        auto it = std::find(order.begin(), order.end(), activeId);
        if (it == order.end()) return;
        int idx = it - order.begin();
        int newIdx = idx + direction;
        if (newIdx < 0 || newIdx >= (int)order.size()) return;
        std::swap(order[idx], order[newIdx]);
        _pConfig->menubarTopLevelOrder = str::join(order, ",");
        apply_for_each_window([](CtMainWin* win) { win->menu_rebuild_menubar(); });
    };
    pBtnUp->signal_clicked().connect([reorderTopLevel]() { reorderTopLevel(-1); });
    pBtnDown->signal_clicked().connect([reorderTopLevel]() { reorderTopLevel(1); });

    button_add->signal_clicked().connect([this, treeview, liststore, saveAndRebuild](){
        if (add_new_item_in_menu_editor(treeview, liststore)) {
            saveAndRebuild();
        }
    });
    button_remove->signal_clicked().connect([this, treeview, liststore, saveAndRebuild](){
        if (remove_selected_from_menu_editor(treeview, liststore)) {
            saveAndRebuild();
        }
    });
    button_reset->signal_clicked().connect([this, liststore, currentMenuId, loadMenu](){
        if (CtDialogs::question_dialog(reset_warning, *this)) {
            static const std::map<std::string, const gchar*> defaultMap = {
                {"FileMenu",   CtConst::MENUBAR_FILE_DEFAULT},
                {"EditMenu",   CtConst::MENUBAR_EDIT_DEFAULT},
                {"InsertMenu", CtConst::MENUBAR_INSERT_DEFAULT},
                {"FormatMenu", CtConst::MENUBAR_FORMAT_DEFAULT},
                {"ToolsMenu",  CtConst::MENUBAR_TOOLS_DEFAULT},
                {"TreeMenu",   CtConst::MENUBAR_TREE_DEFAULT},
                {"SearchMenu", CtConst::MENUBAR_SEARCH_DEFAULT},
                {"ViewMenu",   CtConst::MENUBAR_VIEW_DEFAULT},
                {"HelpMenu",   CtConst::MENUBAR_HELP_DEFAULT},
            };
            auto it = defaultMap.find(*currentMenuId);
            if (it != defaultMap.end()) {
                std::string& configStr = _pCtMenu->getMenubarSubmenuConfig(*currentMenuId);
                configStr = it->second;
                loadMenu();
                apply_for_each_window([](CtMainWin* win) { win->menu_rebuild_menubar(); });
            }
        }
    });
    treeview->signal_key_press_event().connect([button_remove](GdkEventKey* key) -> bool {
        if (key->keyval == GDK_KEY_Delete) {
            button_remove->clicked();
            return true;
        }
        return false;
    });
    treeview->signal_drag_end().connect([saveAndRebuild](const Glib::RefPtr<Gdk::DragContext>&){
        saveAndRebuild();
    });
    auto button_remove_test_sensitive = [button_remove, treeview](){
        button_remove->set_sensitive(bool(treeview->get_selection()->get_selected()));
    };
    treeview->signal_cursor_changed().connect(button_remove_test_sensitive);
    button_remove_test_sensitive();

    return pMainBox;
}

void CtPrefDlg::fill_menu_editor_model(Glib::RefPtr<Gtk::ListStore> model, const std::string& configStr)
{
    model->clear();
    int depth = 0;
    for (const std::string& token : str::split(configStr, ",")) {
        if (token.empty()) continue;
        if (token.front() == '{') {
            populate_row_in_menu_editor(model->append(), token, depth);
            depth++;
        }
        else if (token == "}") {
            depth--;
            if (depth < 0) depth = 0;
            populate_row_in_menu_editor(model->append(), token, depth);
        }
        else {
            populate_row_in_menu_editor(model->append(), token, depth);
        }
    }
}

void CtPrefDlg::populate_row_in_menu_editor(Gtk::TreeModel::iterator row, const Glib::ustring& key, int depth)
{
    Glib::ustring icon, desc;
    Glib::ustring storeKey = key;

    if (key == CtConst::TAG_SEPARATOR) {
        desc = CtConst::TAG_SEPARATOR_ANSI_REPR;
    }
    else if (not key.empty() and key[0] == '{') {
        std::string submenuId = key.substr(1);
        desc = submenuId + " >";
    }
    else if (key == "}") {
        desc = "< " + Glib::ustring{_("end submenu")};
    }
    else if (CtMenuAction const* action = _pCtMenu->find_action(key)) {
        icon = action->image;
        desc = action->desc;
    }
    else {
        desc = key;
    }

    if (depth > 0) {
        Glib::ustring indent;
        for (int i = 0; i < depth; i++) indent += "    ";
        desc = indent + desc;
    }

    row->set_value(_menuEditorModelColumns.icon, icon);
    row->set_value(_menuEditorModelColumns.key, storeKey);
    row->set_value(_menuEditorModelColumns.desc, desc);
    row->set_value(_menuEditorModelColumns.colWeight, depth);
}

bool CtPrefDlg::add_new_item_in_menu_editor(Gtk::TreeView* treeview, Glib::RefPtr<Gtk::ListStore> model)
{
    const Glib::ustring newSubmenuKey = "__new_submenu__";
    auto itemStore = CtChooseDialogListStore::create();
    itemStore->add_row("ct_add", newSubmenuKey, _("New Submenu..."));
    itemStore->add_row("", CtConst::TAG_SEPARATOR, CtConst::TAG_SEPARATOR_ANSI_REPR);
    for (const CtMenuAction& action : _pCtMenu->get_actions()) {
        if (action.desc.empty()) continue;
        itemStore->add_row(action.image, action.id, action.desc);
    }

    auto chosen_row = CtDialogs::choose_item_dialog(*this,
                                                    _("Select Element to Add"),
                                                    itemStore,
                                                    nullptr,
                                                    "0",
                                                    std::make_pair(500, _pConfig->winRect[3]));
    if (chosen_row) {
        Glib::ustring chosenKey = chosen_row->get_value(itemStore->columns.key);
        auto selected_row = treeview->get_selection()->get_selected();
        int depth = 0;
        if (selected_row) {
            depth = selected_row->get_value(_menuEditorModelColumns.colWeight);
        }

        if (chosenKey == newSubmenuKey) {
            Glib::ustring submenuName = CtDialogs::img_n_entry_dialog(
                *this, _("Submenu Name"), "", "ct_add");
            if (submenuName.empty()) return false;
            auto open_row = selected_row ? model->insert_after(*selected_row) : model->append();
            populate_row_in_menu_editor(open_row, "{" + submenuName, depth);
            auto close_row = model->insert_after(open_row);
            populate_row_in_menu_editor(close_row, "}", depth);
            return true;
        }

        auto new_row = selected_row ? model->insert_after(*selected_row) : model->append();
        populate_row_in_menu_editor(new_row, chosenKey, depth);
        return true;
    }
    return false;
}

bool CtPrefDlg::remove_selected_from_menu_editor(Gtk::TreeView* treeview, Glib::RefPtr<Gtk::ListStore> model)
{
    auto sel = treeview->get_selection()->get_selected();
    if (!sel) return false;

    Glib::ustring key = sel->get_value(_menuEditorModelColumns.key);

    if (!key.empty() && key[0] == '{') {
        int depth = 1;
        int count = 1;
        auto it = sel;
        it++;
        while (it) {
            count++;
            Glib::ustring k = it->get_value(_menuEditorModelColumns.key);
            if (!k.empty() && k[0] == '{') depth++;
            else if (k == "}") { if (--depth == 0) break; }
            it++;
        }
        auto cur = sel;
        for (int i = 0; i < count && cur; i++) cur = model->erase(cur);
        return true;
    }

    if (key == "}") {
        int selIdx = model->get_path(sel)[0];
        int depth = 1;
        int openIdx = -1;
        for (int i = selIdx - 1; i >= 0; i--) {
            auto it = model->get_iter(Gtk::TreeModel::Path{std::to_string(i)});
            Glib::ustring k = it->get_value(_menuEditorModelColumns.key);
            if (k == "}") depth++;
            else if (!k.empty() && k[0] == '{') { if (--depth == 0) { openIdx = i; break; } }
        }
        if (openIdx >= 0) {
            int count = selIdx - openIdx + 1;
            auto cur = model->get_iter(Gtk::TreeModel::Path{std::to_string(openIdx)});
            for (int i = 0; i < count; i++) cur = model->erase(cur);
        }
        else {
            model->erase(sel);
        }
        return true;
    }

    model->erase(sel);
    return true;
}

std::string CtPrefDlg::update_config_from_menu_editor(Glib::RefPtr<Gtk::ListStore> model)
{
    std::vector<std::string> items;
    for (auto it : model->children()) {
        items.push_back(it.get_value(_menuEditorModelColumns.key));
    }
    return str::join(items, ",");
}
