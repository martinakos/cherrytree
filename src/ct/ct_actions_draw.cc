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
#include "ct_drawing_commands.h"
#include "ct_command_bridge.h"

void CtActions::new_drawing_canvas()
{
    if (!_is_there_selected_node_or_error()) return;

    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;

    auto* bridge = _pCtMainWin->get_command_bridge();
    if (!bridge || !bridge->isActive()) return;

    auto treeIter = _pCtMainWin->curr_tree_iter();
    if (!treeIter) return;

    auto hAdj = _pCtMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pCtMainWin->getScrolledwindowText().get_vadjustment();
    double zoom = _pCtMainWin->get_rt_zoom_scale_factor();

    CtDrawingCanvas canvas;
    canvas.width = 300.0;
    canvas.height = 250.0;
    // center in current viewport
    double viewW = hAdj ? hAdj->get_page_size() : 600.0;
    double viewH = vAdj ? vAdj->get_page_size() : 400.0;
    double scrollX = hAdj ? hAdj->get_value() : 0.0;
    double scrollY = vAdj ? vAdj->get_value() : 0.0;
    canvas.x = (scrollX + (viewW - canvas.width * zoom) / 2.0) / zoom;
    canvas.y = (scrollY + (viewH - canvas.height * zoom) / 2.0) / zoom;
    if (canvas.x < 10.0) canvas.x = 10.0;
    if (canvas.y < 10.0) canvas.y = 10.0;

    auto cmd = std::make_unique<AddCanvasCommand>(
        bridge->getDocumentModel(), treeIter.get_node_id(), canvas);
    bridge->executeCommand(std::move(cmd));

    // activate drawing mode
    if (!overlay->isDrawingMode()) {
        overlay->setDrawingMode(true);
    }

    // select the new canvas
    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (nodeModel) {
        overlay->resetSelection();
    }

    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
}

void CtActions::toggle_drawing_mode()
{
    auto* overlay = _pCtMainWin->get_drawing_overlay();
    if (!overlay) return;
    overlay->setDrawingMode(!overlay->isDrawingMode());
}
