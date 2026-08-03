/*
 * ct_delta_engine.h
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

#pragma once

#include "ct_node_content.h"
#include "ct_drawing.h"
#include <string>

class CtDeltaEngine {
public:
    static bool applyReverse(CtNodeContent& content, const std::string& deltaData);
    static bool applyForward(CtNodeContent& content, const std::string& deltaData);
    static bool isReplayable(const std::string& deltaData);

    static bool applyReverseDrawing(std::vector<CtDrawingCanvas>& canvases, const std::string& deltaData);
    static bool applyForwardDrawing(std::vector<CtDrawingCanvas>& canvases, const std::string& deltaData);
    static bool isDrawingDelta(const std::string& deltaData);

    static std::string serializeStroke(const CtDrawingStroke& s);
    static CtDrawingStroke deserializeStroke(const std::string& data);
    static std::string serializeCanvas(const CtDrawingCanvas& c);
    static CtDrawingCanvas deserializeCanvas(const std::string& data);
    static std::string serializePoints(const std::vector<CtDrawingPoint>& pts);
};
