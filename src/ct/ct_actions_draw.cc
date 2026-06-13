/*
 * ct_actions_draw.cc
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

#include "ct_actions.h"
#include "ct_drawing.h"
#include "ct_command_bridge.h"

void CtActions::new_drawing_canvas()
{
    if (!_is_there_selected_node_or_error()) return;

    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;

    auto* bridge = _pCtMainWin->get_command_bridge();
    if (!bridge || !bridge->isActive()) return;

    overlay->beginCreateCanvas();
}

void CtActions::toggle_drawing_mode()
{
    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;
    overlay->setDrawingMode(!overlay->isDrawingMode());
}
