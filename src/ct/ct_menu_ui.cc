/*
 * ct_menu_ui.cc
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

#include "ct_menu.h"
#include "ct_const.h"
#include "ct_config.h"
#include "ct_misc_utils.h"

std::vector<std::string> CtMenu::_get_ui_str_toolbars()
{
    auto f_generate_ui = [&](const size_t id, const std::vector<std::unordered_set<std::string>::const_iterator>& items){
        std::string str_buff;
        for (std::unordered_set<std::string>::const_iterator element : items) {
            if (*element == CtConst::TAG_SEPARATOR) {
                str_buff += "<child><object class='GtkSeparatorToolItem'/></child>";
            }
            else {
                const bool isOpenRecent{*element == CtConst::CHAR_STAR};
                const bool isUndoRedo{*element == "act_undo" or *element == "act_redo"};
                CtMenuAction const* pAction = isOpenRecent ? find_action("ct_open_file") : find_action(*element);
                if (pAction) {
                    if (isOpenRecent) str_buff += "<child><object class='GtkMenuToolButton' id='RecentDocs'>";
                    else if (isUndoRedo) str_buff += "<child><object class='GtkMenuToolButton' id='" + *element + "'>";
                    else str_buff += "<child><object class='GtkToolButton' id='" + *element + "'>";
                    str_buff += "<property name='action-name'>win." + pAction->id + "</property>"; // 'win.' is a default action group in Window
                    str_buff += "<property name='icon-name'>" + pAction->image + "</property>";
                    str_buff += "<property name='label'>" + pAction->name + "</property>";
                    std::string kb_shortcut = pAction->get_shortcut(_pCtConfig);
                    std::string tooltip;
                    if (not _pCtConfig->toolbarTooltips) {
                        // keep empty
                    }
                    else if (kb_shortcut.empty()) {
                        tooltip = pAction->desc;
                    }
                    else {
                        if (kb_shortcut.find(CtMenu::KB_CONTROL) != std::string::npos) {
                            kb_shortcut = str::replace(kb_shortcut, CtMenu::KB_CONTROL, "Ctrl+");
                        }
                        if (kb_shortcut.find(CtMenu::KB_SHIFT) != std::string::npos) {
                            kb_shortcut = str::replace(kb_shortcut, CtMenu::KB_SHIFT, "Shift+");
                        }
                        if (kb_shortcut.find(CtMenu::KB_ALT) != std::string::npos) {
                            kb_shortcut = str::replace(kb_shortcut, CtMenu::KB_ALT, "Alt+");
                        }
                        if (kb_shortcut.find(CtMenu::KB_META) != std::string::npos) {
                            kb_shortcut = str::replace(kb_shortcut, CtMenu::KB_META, "Meta+");
                        }
                        tooltip = pAction->desc + " (" + str::xml_escape(kb_shortcut).c_str() + ")";
                    }
                    if (not tooltip.empty()) {
                        str_buff += "<property name='tooltip-text'>" + tooltip + "</property>";
                    }
                    str_buff += "<property name='visible'>True</property>";
                    str_buff += "<property name='use_underline'>True</property>";
                    str_buff += "</object></child>";
                }
            }
        }
        str_buff = "<interface><object class='GtkToolbar' id='ToolBar" + std::to_string(id) + "'>"
                   "<property name='visible'>True</property>"
                   "<property name='can_focus'>False</property>"
                   + str_buff +
                   "</object></interface>";
        return str_buff;
    };

    std::vector<std::string> toolbarUIstr;
    std::vector<std::string> vecToolbarElements = str::split(_pCtConfig->toolbarUiList, ",");
    std::unordered_set<std::string> toolbarSet;
    std::vector<std::unordered_set<std::string>::const_iterator> toolbar_accumulator;
    for (const std::string& element : vecToolbarElements) {
        if (element != CtConst::TOOLBAR_SPLIT) {
            const auto retPair = toolbarSet.insert(element);
            // only the separator can be inserted multiple times
            if (retPair.second or CtConst::TAG_SEPARATOR == element) toolbar_accumulator.push_back(retPair.first);
            else spdlog::debug("?? {} skipped dupl {}", __FUNCTION__, element);
        }
        else if (not toolbar_accumulator.empty()) {
            toolbarUIstr.push_back(f_generate_ui(toolbarUIstr.size(), toolbar_accumulator));
            toolbar_accumulator.clear();
        }
    }

    if (not toolbar_accumulator.empty()) {
        toolbarUIstr.push_back(f_generate_ui(toolbarUIstr.size(), toolbar_accumulator));
        toolbar_accumulator.clear();
    }

    return toolbarUIstr;
}

std::string CtMenu::generate_menu_xml(const std::string& configStr)
{
    std::string xml;
    for (const std::string& token : str::split(configStr, ",")) {
        if (token.empty()) continue;
        if (token == CtConst::TAG_SEPARATOR) {
            xml += "<separator/>";
        }
        else if (token.front() == '{') {
            xml += "<menu action='" + token.substr(1) + "'>";
        }
        else if (token == "}") {
            xml += "</menu>";
        }
        else {
            xml += "<menuitem action='" + token + "'/>";
        }
    }
    return xml;
}

std::string CtMenu::_get_ui_str_menu()
{
    std::string xml = "<menubar name='MenuBar'>";
    static const std::map<std::string, std::string*> submenuConfigMap = {
        {"FileMenu",   &_pCtConfig->menubarFileUiList},
        {"EditMenu",   &_pCtConfig->menubarEditUiList},
        {"InsertMenu", &_pCtConfig->menubarInsertUiList},
        {"FormatMenu", &_pCtConfig->menubarFormatUiList},
        {"ToolsMenu",  &_pCtConfig->menubarToolsUiList},
        {"TreeMenu",   &_pCtConfig->menubarTreeUiList},
        {"SearchMenu", &_pCtConfig->menubarSearchUiList},
        {"ViewMenu",   &_pCtConfig->menubarViewUiList},
        {"HelpMenu",   &_pCtConfig->menubarHelpUiList},
    };
    for (const std::string& menuId : str::split(_pCtConfig->menubarTopLevelOrder, ",")) {
        if (menuId.empty()) continue;
        if (menuId == "BookmarksMenu") {
            xml += "<menu action='BookmarksMenu'></menu>";
            continue;
        }
        auto it = submenuConfigMap.find(menuId);
        if (it != submenuConfigMap.end()) {
            xml += "<menu action='" + menuId + "'>";
            xml += generate_menu_xml(*it->second);
            xml += "</menu>";
        }
    }
    xml += "</menubar>";
    return xml;
}

std::string& CtMenu::getMenubarSubmenuConfig(const std::string& menuId)
{
    static const std::map<std::string, std::string CtConfig::*> configMap = {
        {"FileMenu",   &CtConfig::menubarFileUiList},
        {"EditMenu",   &CtConfig::menubarEditUiList},
        {"InsertMenu", &CtConfig::menubarInsertUiList},
        {"FormatMenu", &CtConfig::menubarFormatUiList},
        {"ToolsMenu",  &CtConfig::menubarToolsUiList},
        {"TreeMenu",   &CtConfig::menubarTreeUiList},
        {"SearchMenu", &CtConfig::menubarSearchUiList},
        {"ViewMenu",   &CtConfig::menubarViewUiList},
        {"HelpMenu",   &CtConfig::menubarHelpUiList},
    };
    auto it = configMap.find(menuId);
    if (it != configMap.end()) {
        return _pCtConfig->*(it->second);
    }
    static std::string empty;
    empty.clear();
    return empty;
}

std::string CtMenu::_get_popup_menu_ui_str_text()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupTextUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_code()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupCodeUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_image()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupImageUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_latex()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupLatexUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_anchor()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupAnchorUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_embfile()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupEmbfileUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_terminal()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupTerminalUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_link()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupLinkUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_codebox()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupCodeboxUiList) + "</popup>";
}

std::string CtMenu::_get_popup_menu_ui_str_table_cell()
{
    return "<popup>" + generate_menu_xml(_pCtConfig->popupTableCellUiList) + "</popup>";
}
