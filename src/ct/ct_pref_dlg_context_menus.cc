/*
 * ct_pref_dlg_context_menus.cc
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

Gtk::Widget* CtPrefDlg::build_tab_context_menus()
{
    struct PopupIdLabel {
        std::string id;
        Glib::ustring label;
        std::string CtConfig::* configMember;
        const gchar* defaultValue;
    };
    static const std::vector<PopupIdLabel> popupMenus = {
        {"Node",     _("Node"),          &CtConfig::popupNodeUiList,     CtConst::POPUP_NODE_DEFAULT},
        {"Text",     _("Text"),          &CtConfig::popupTextUiList,     CtConst::POPUP_TEXT_DEFAULT},
        {"Code",     _("Code"),          &CtConfig::popupCodeUiList,     CtConst::POPUP_CODE_DEFAULT},
        {"Image",    _("Image"),         &CtConfig::popupImageUiList,    CtConst::POPUP_IMAGE_DEFAULT},
        {"LaTeX",    _("LaTeX"),         &CtConfig::popupLatexUiList,    CtConst::POPUP_LATEX_DEFAULT},
        {"Anchor",   _("Anchor"),        &CtConfig::popupAnchorUiList,   CtConst::POPUP_ANCHOR_DEFAULT},
        {"EmbFile",  _("Embedded File"), &CtConfig::popupEmbfileUiList,  CtConst::POPUP_EMBFILE_DEFAULT},
        {"Terminal", _("Terminal"),      &CtConfig::popupTerminalUiList, CtConst::POPUP_TERMINAL_DEFAULT},
        {"Link",     _("Link"),          &CtConfig::popupLinkUiList,     CtConst::POPUP_LINK_DEFAULT},
        {"Codebox",  _("Codebox"),       &CtConfig::popupCodeboxUiList,  CtConst::POPUP_CODEBOX_DEFAULT},
        {"TableCell", _("Table Cell"),   &CtConfig::popupTableCellUiList, CtConst::POPUP_TABLE_CELL_DEFAULT},
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
    for (size_t i = 0; i < popupMenus.size(); i++) {
        pCombo->append(std::to_string(i), popupMenus[i].label);
    }
    pCombo->set_active(0);

    auto hboxSelector = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 4});
    hboxSelector->pack_start(*Gtk::manage(new Gtk::Label{_("Context Menu:")}), false, false);
    hboxSelector->pack_start(*pCombo, true, true);

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
    pMainBox->pack_start(*hboxSelector, false, false);
    pMainBox->pack_start(*hboxEditor, true, true);

    auto currentIdx = std::make_shared<int>(0);

    auto loadMenu = [this, liststore, treeview, currentIdx]() {
        const auto& pm = popupMenus[*currentIdx];
        fill_menu_editor_model(liststore, _pConfig->*(pm.configMember));
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

    auto saveAndRebuild = [this, liststore, currentIdx]() {
        const auto& pm = popupMenus[*currentIdx];
        _pConfig->*(pm.configMember) = update_config_from_menu_editor(liststore);
        apply_for_each_window([](CtMainWin* win) { win->get_ct_menu().invalidate_popup_menus(); });
    };

    loadMenu();

    pCombo->signal_changed().connect([pCombo, currentIdx, loadMenu]() {
        std::string idStr = pCombo->get_active_id();
        if (!idStr.empty()) {
            *currentIdx = std::stoi(idStr);
            loadMenu();
        }
    });

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
    button_reset->signal_clicked().connect([this, liststore, currentIdx, loadMenu](){
        if (CtDialogs::question_dialog(reset_warning, *this)) {
            const auto& pm = popupMenus[*currentIdx];
            _pConfig->*(pm.configMember) = pm.defaultValue;
            loadMenu();
            apply_for_each_window([](CtMainWin* win) { win->get_ct_menu().invalidate_popup_menus(); });
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
