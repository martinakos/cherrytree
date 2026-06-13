/*
 * ct_drawing.cc
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

#include "ct_drawing.h"
#include "ct_drawing_commands.h"
#include "ct_main_win.h"
#include "ct_command_bridge.h"
#include "ct_dialogs.h"
#include <glibmm/i18n.h>
#include <cmath>
#include <algorithm>

static void parseColor(const std::string& hex, double& r, double& g, double& b)
{
    if (hex.size() == 7 && hex[0] == '#') {
        r = std::stoi(hex.substr(1, 2), nullptr, 16) / 255.0;
        g = std::stoi(hex.substr(3, 2), nullptr, 16) / 255.0;
        b = std::stoi(hex.substr(5, 2), nullptr, 16) / 255.0;
    } else {
        r = g = b = 0.0;
    }
}

std::optional<CtDrawingCanvas> CtDrawingOverlay::_clipboard;

CtDrawingOverlay::CtDrawingOverlay(CtMainWin* pMainWin)
    : _pMainWin(pMainWin)
{
    _drawingArea.set_can_focus(false);
    _drawingArea.set_hexpand(true);
    _drawingArea.set_vexpand(true);

#if GTKMM_MAJOR_VERSION < 4
    _drawingArea.signal_draw().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onDraw));
    _drawingArea.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
                            Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_MOTION_MASK |
                            Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK);
    _drawingArea.signal_button_press_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onButtonPress), false);
    _drawingArea.signal_button_release_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onButtonRelease), false);
    _drawingArea.signal_motion_notify_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onMotionNotify), false);
    _drawingArea.signal_scroll_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onScroll), false);
    _drawingArea.signal_realize().connect([this]() {
        if (auto gdkWin = _drawingArea.get_window()) {
            gdk_window_set_pass_through(gdkWin->gobj(), !_drawingMode);
        }
    });
#endif
}

void CtDrawingOverlay::beginCreateCanvas()
{
    _createCanvasMode = true;
    if (!_drawingMode) {
        setDrawingMode(true);
    }
    auto gdkWin = _drawingArea.get_window();
    if (gdkWin) {
        gdkWin->set_cursor(Gdk::Cursor::create(gdkWin->get_display(), Gdk::CROSSHAIR));
    }
}

void CtDrawingOverlay::setDrawingMode(bool on)
{
    _drawingMode = on;
    if (!on) {
        _deleteStrokeMode = false;
        _createCanvasMode = false;
        _dragType = CtDrawingDragType::None;
    }
#if GTKMM_MAJOR_VERSION < 4
    _overlay.set_overlay_pass_through(_drawingArea, !_drawingMode);
    if (_drawingArea.get_realized()) {
        if (auto gdkWin = _drawingArea.get_window()) {
            gdk_window_set_pass_through(gdkWin->gobj(), !_drawingMode);
        }
    }
#endif
    _drawingArea.queue_draw();
    _pMainWin->get_status_bar().canvasEditLabel.set_visible(on);
    _pMainWin->update_drawing_mode_toggle(on);
}

void CtDrawingOverlay::resetSelection()
{
    _selectedCanvasIdx = -1;
    _deleteStrokeMode = false;
    _createCanvasMode = false;
    _dragType = CtDrawingDragType::None;
    _drawingArea.queue_draw();
}

void CtDrawingOverlay::refresh()
{
    _drawingArea.queue_draw();
}

bool CtDrawingOverlay::_onDraw(const Cairo::RefPtr<Cairo::Context>& cr)
{
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!treeIter) return false;

    auto* bridge = _pMainWin->get_command_bridge();
    if (!bridge || !bridge->isActive()) return false;

    auto docModel = bridge->getDocumentModel();
    auto nodeModel = docModel->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return false;

    const auto& canvases = nodeModel->getDrawingCanvases();
    if (canvases.empty() && !_drawingMode) return false;

    auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
    double hScroll = hAdj ? hAdj->get_value() : 0.0;
    double vScroll = vAdj ? vAdj->get_value() : 0.0;
    double zoom = _pMainWin->get_rt_zoom_scale_factor();

    for (size_t i = 0; i < canvases.size(); ++i) {
        _drawCanvas(cr, canvases[i], static_cast<int>(i), hScroll, vScroll, zoom);
    }

    if (_dragType == CtDrawingDragType::CreateCanvas) {
        double sx = std::min(_dragStartX, _dragStartX + _dragCanvasOrigW);
        double sy = std::min(_dragStartY, _dragStartY + _dragCanvasOrigH);
        double pw = std::abs(_dragCanvasOrigW);
        double ph = std::abs(_dragCanvasOrigH);
        cr->rectangle(sx, sy, pw, ph);
        cr->set_source_rgba(0.2, 0.5, 1.0, 0.3);
        cr->fill_preserve();
        cr->set_source_rgba(0.2, 0.5, 1.0, 0.8);
        cr->set_line_width(1.0);
        std::vector<double> dashes{6.0, 3.0};
        cr->set_dash(dashes, 0.0);
        cr->stroke();
        cr->unset_dash();
    }

    return false;
}

void CtDrawingOverlay::_drawCanvas(const Cairo::RefPtr<Cairo::Context>& cr,
                                    const CtDrawingCanvas& canvas,
                                    int idx, double hScroll, double vScroll, double zoom)
{
    double cx = canvas.x * zoom - hScroll;
    double cy = canvas.y * zoom - vScroll;
    double cw = canvas.width * zoom;
    double ch = canvas.height * zoom;
    double cr_radius = canvas.cornerRadius * zoom;

    bool showChrome = _drawingMode || canvas.showBorderWhenInactive;

    // background fill — skip header area so header opacity stays independent
    {
        double bgR, bgG, bgB;
        parseColor(canvas.bgColor, bgR, bgG, bgB);
        cr->save();
        _drawRoundedRect(cr, cx, cy, cw, ch, cr_radius);
        cr->clip();
        double headerH = showChrome ? std::min(HEADER_HEIGHT * zoom, ch) : 0.0;
        cr->rectangle(cx, cy + headerH, cw, ch - headerH);
        cr->set_source_rgba(bgR, bgG, bgB, canvas.bgOpacity);
        cr->fill();
        cr->restore();
    }

    if (showChrome) {
        // border
        _drawRoundedRect(cr, cx, cy, cw, ch, cr_radius);
        if (_drawingMode && idx == _selectedCanvasIdx) {
            cr->set_source_rgba(0.2, 0.5, 1.0, 0.8);
            cr->set_line_width(2.0);
        } else {
            cr->set_source_rgba(0.5, 0.5, 0.5, 0.6);
            cr->set_line_width(1.0);
            std::vector<double> dashes{4.0, 2.0};
            cr->set_dash(dashes, 0.0);
        }
        cr->stroke();
        cr->unset_dash();

        // header bar — clip to canvas shape so large corner radii don't overflow
        double headerH = std::min(HEADER_HEIGHT * zoom, ch);
        cr->save();
        _drawRoundedRect(cr, cx, cy, cw, ch, cr_radius);
        cr->clip();
        cr->rectangle(cx, cy, cw, headerH);
        cr->set_source_rgba(0.6, 0.6, 0.6, 1.0);
        cr->fill();
        cr->restore();

        if (!canvas.name.empty()) {
            auto layout = Pango::Layout::create(cr);
            layout->set_text(canvas.name);
            Pango::FontDescription fontDesc;
            fontDesc.set_family("Sans");
            fontDesc.set_size(static_cast<int>(10 * zoom * Pango::SCALE));
            layout->set_font_description(fontDesc);
            layout->set_ellipsize(Pango::ELLIPSIZE_END);
            layout->set_width(static_cast<int>((cw - 12 * zoom) * Pango::SCALE));
            layout->set_alignment(Pango::ALIGN_CENTER);
            int textW, textH;
            layout->get_pixel_size(textW, textH);
            cr->set_source_rgba(0.1, 0.1, 0.1, 0.9);
            cr->move_to(cx + 6 * zoom, cy + (headerH - textH) / 2.0);
            layout->show_in_cairo_context(cr);
        }
    }

    // clip strokes to canvas bounds
    cr->save();
    _drawRoundedRect(cr, cx, cy, cw, ch, cr_radius);
    cr->clip();

    // render strokes
    for (const auto& stroke : canvas.strokes) {
        if (stroke.points.size() < 2) continue;
        double r, g, b;
        parseColor(stroke.color, r, g, b);
        cr->set_source_rgba(r, g, b, stroke.opacity);
        cr->set_line_width(stroke.lineWidth * zoom);
        cr->set_line_cap(Cairo::LINE_CAP_ROUND);
        cr->set_line_join(Cairo::LINE_JOIN_ROUND);

        cr->move_to(cx + stroke.points[0].x * zoom,
                    cy + stroke.points[0].y * zoom);
        for (size_t j = 1; j < stroke.points.size(); ++j) {
            cr->line_to(cx + stroke.points[j].x * zoom,
                        cy + stroke.points[j].y * zoom);
        }
        cr->stroke();
    }

    cr->restore();
}

void CtDrawingOverlay::_drawRoundedRect(const Cairo::RefPtr<Cairo::Context>& cr,
                                         double x, double y, double w, double h, double r)
{
    r = std::min(r, std::min(w / 2.0, h / 2.0));
    cr->begin_new_sub_path();
    cr->arc(x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cr->arc(x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cr->arc(x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cr->arc(x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cr->close_path();
}

CtDrawingHitZone CtDrawingOverlay::_hitTest(double mx, double my,
                                             const CtDrawingCanvas& canvas,
                                             double hScroll, double vScroll, double zoom)
{
    double cx = canvas.x * zoom - hScroll;
    double cy = canvas.y * zoom - vScroll;
    double cw = canvas.width * zoom;
    double ch = canvas.height * zoom;
    double e = EDGE_HIT_THRESHOLD;

    if (mx < cx - e || mx > cx + cw + e || my < cy - e || my > cy + ch + e) {
        return CtDrawingHitZone::None;
    }

    bool onLeft = mx >= cx - e && mx <= cx + e;
    bool onRight = mx >= cx + cw - e && mx <= cx + cw + e;
    bool onTop = my >= cy - e && my <= cy + e;
    bool onBottom = my >= cy + ch - e && my <= cy + ch + e;

    if (onTop && onLeft) return CtDrawingHitZone::TopLeft;
    if (onTop && onRight) return CtDrawingHitZone::TopRight;
    if (onBottom && onLeft) return CtDrawingHitZone::BottomLeft;
    if (onBottom && onRight) return CtDrawingHitZone::BottomRight;
    if (onLeft) return CtDrawingHitZone::Left;
    if (onRight) return CtDrawingHitZone::Right;
    if (onTop) return CtDrawingHitZone::Top;
    if (onBottom) return CtDrawingHitZone::Bottom;

    double headerH = HEADER_HEIGHT * zoom;
    if (my >= cy && my <= cy + headerH) {
        return CtDrawingHitZone::Header;
    }

    if (mx >= cx && mx <= cx + cw && my >= cy && my <= cy + ch) {
        return CtDrawingHitZone::Interior;
    }

    return CtDrawingHitZone::None;
}

int CtDrawingOverlay::_hitTestCanvas(double mx, double my,
                                      double hScroll, double vScroll, double zoom)
{
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!treeIter) return -1;

    auto* bridge = _pMainWin->get_command_bridge();
    if (!bridge || !bridge->isActive()) return -1;

    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return -1;

    const auto& canvases = nodeModel->getDrawingCanvases();
    // iterate back-to-front so topmost canvas wins
    for (int i = static_cast<int>(canvases.size()) - 1; i >= 0; --i) {
        if (_hitTest(mx, my, canvases[i], hScroll, vScroll, zoom) != CtDrawingHitZone::None) {
            return i;
        }
    }
    return -1;
}

double CtDrawingOverlay::_distPointToSegment(double px, double py,
                                              double ax, double ay,
                                              double bx, double by)
{
    double dx = bx - ax, dy = by - ay;
    double lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-12) return std::hypot(px - ax, py - ay);
    double t = std::clamp(((px - ax) * dx + (py - ay) * dy) / lenSq, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

int CtDrawingOverlay::_hitTestStroke(double mx, double my,
                                      const CtDrawingCanvas& canvas,
                                      double hScroll, double vScroll, double zoom)
{
    double cx = canvas.x * zoom - hScroll;
    double cy = canvas.y * zoom - vScroll;
    double threshold = STROKE_HIT_THRESHOLD;

    double bestDist = threshold + 1.0;
    int bestIdx = -1;

    for (size_t si = 0; si < canvas.strokes.size(); ++si) {
        const auto& pts = canvas.strokes[si].points;
        for (size_t j = 1; j < pts.size(); ++j) {
            double dist = _distPointToSegment(mx, my,
                cx + pts[j-1].x * zoom, cy + pts[j-1].y * zoom,
                cx + pts[j].x * zoom, cy + pts[j].y * zoom);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = static_cast<int>(si);
            }
        }
    }

    return bestDist <= threshold ? bestIdx : -1;
}

bool CtDrawingOverlay::_onButtonPress(GdkEventButton* event)
{
    if (!_drawingMode) return false;

    auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
    double hScroll = hAdj ? hAdj->get_value() : 0.0;
    double vScroll = vAdj ? vAdj->get_value() : 0.0;
    double zoom = _pMainWin->get_rt_zoom_scale_factor();

    if (event->button == 3) {
        int ci = _hitTestCanvas(event->x, event->y, hScroll, vScroll, zoom);
        if (ci >= 0) {
            _selectedCanvasIdx = ci;
            _drawingArea.queue_draw();
        } else {
            _selectedCanvasIdx = -1;
        }
        _showContextMenu(event);
        return true;
    }

    if (event->button != 1) return false;

    if (_createCanvasMode) {
        _dragStartX = event->x;
        _dragStartY = event->y;
        _dragType = CtDrawingDragType::CreateCanvas;
        _dragCanvasOrigW = 0.0;
        _dragCanvasOrigH = 0.0;
        return true;
    }

    int ci = _hitTestCanvas(event->x, event->y, hScroll, vScroll, zoom);
    if (ci < 0) {
        setDrawingMode(false);
        _selectedCanvasIdx = -1;
        return true;
    }

    _selectedCanvasIdx = ci;

    auto* bridge = _pMainWin->get_command_bridge();
    auto nodeModel = bridge->getDocumentModel()->getNodeById(_pMainWin->curr_tree_iter().get_node_id());
    if (!nodeModel) return false;

    const auto& canvas = nodeModel->getDrawingCanvases()[ci];
    auto zone = _hitTest(event->x, event->y, canvas, hScroll, vScroll, zoom);

    _dragStartX = event->x;
    _dragStartY = event->y;
    _dragCanvasOrigX = canvas.x;
    _dragCanvasOrigY = canvas.y;
    _dragCanvasOrigW = canvas.width;
    _dragCanvasOrigH = canvas.height;

    if (zone == CtDrawingHitZone::Header) {
        _dragType = CtDrawingDragType::Move;
    }
    else if (zone >= CtDrawingHitZone::TopLeft && zone <= CtDrawingHitZone::Bottom) {
        _dragType = CtDrawingDragType::Resize;
        _resizeZone = zone;
    }
    else if (zone == CtDrawingHitZone::Interior) {
        if (_deleteStrokeMode) {
            int si = _hitTestStroke(event->x, event->y, canvas, hScroll, vScroll, zoom);
            if (si >= 0) {
                auto treeIter = _pMainWin->curr_tree_iter();
                auto cmd = std::make_unique<EraseStrokeCommand>(
                    bridge->getDocumentModel(), treeIter.get_node_id(),
                    static_cast<size_t>(ci), canvas.strokes[si], static_cast<size_t>(si));
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
            }
            return true;
        }
        // start drawing a stroke
        _dragType = CtDrawingDragType::Draw;
        auto& canvasMut = nodeModel->getDrawingCanvasesMut()[ci];
        CtDrawingStroke newStroke;
        newStroke.color = _currentColor;
        newStroke.lineWidth = _currentLineWidth;
        newStroke.opacity = _currentOpacity;

        double cx = canvas.x * zoom - hScroll;
        double cy = canvas.y * zoom - vScroll;
        double px = (event->x - cx) / zoom;
        double py = (event->y - cy) / zoom;
        newStroke.points.push_back({px, py});
        canvasMut.strokes.push_back(std::move(newStroke));
        _drawingArea.queue_draw();
    }

    return true;
}

bool CtDrawingOverlay::_onMotionNotify(GdkEventMotion* event)
{
    if (!_drawingMode) return false;

    if (_dragType == CtDrawingDragType::CreateCanvas) {
        _dragCanvasOrigW = event->x - _dragStartX;
        _dragCanvasOrigH = event->y - _dragStartY;
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::None) {
        if (_createCanvasMode) return false;

        auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
        double hScroll = hAdj ? hAdj->get_value() : 0.0;
        double vScroll = vAdj ? vAdj->get_value() : 0.0;
        double zoom = _pMainWin->get_rt_zoom_scale_factor();

        Gdk::CursorType cursorType = Gdk::ARROW;
        int ci = _hitTestCanvas(event->x, event->y, hScroll, vScroll, zoom);
        if (ci >= 0) {
            auto* bridge = _pMainWin->get_command_bridge();
            if (bridge && bridge->isActive()) {
                auto nodeModel = bridge->getDocumentModel()->getNodeById(_pMainWin->curr_tree_iter().get_node_id());
                if (nodeModel) {
                    auto zone = _hitTest(event->x, event->y, nodeModel->getDrawingCanvases()[ci], hScroll, vScroll, zoom);
                    switch (zone) {
                        case CtDrawingHitZone::TopLeft:     cursorType = Gdk::TOP_LEFT_CORNER; break;
                        case CtDrawingHitZone::TopRight:    cursorType = Gdk::TOP_RIGHT_CORNER; break;
                        case CtDrawingHitZone::BottomLeft:  cursorType = Gdk::BOTTOM_LEFT_CORNER; break;
                        case CtDrawingHitZone::BottomRight: cursorType = Gdk::BOTTOM_RIGHT_CORNER; break;
                        case CtDrawingHitZone::Left:        cursorType = Gdk::LEFT_SIDE; break;
                        case CtDrawingHitZone::Right:       cursorType = Gdk::RIGHT_SIDE; break;
                        case CtDrawingHitZone::Top:         cursorType = Gdk::TOP_SIDE; break;
                        case CtDrawingHitZone::Bottom:      cursorType = Gdk::BOTTOM_SIDE; break;
                        case CtDrawingHitZone::Header:      cursorType = Gdk::FLEUR; break;
                        default: break;
                    }
                }
            }
        }
        auto gdkWin = _drawingArea.get_window();
        if (gdkWin) {
            if (cursorType == Gdk::ARROW) {
                gdkWin->set_cursor();
            } else {
                gdkWin->set_cursor(Gdk::Cursor::create(gdkWin->get_display(), cursorType));
            }
        }
        return false;
    }

    if (_selectedCanvasIdx < 0) return false;

    auto* bridge = _pMainWin->get_command_bridge();
    auto nodeModel = bridge->getDocumentModel()->getNodeById(_pMainWin->curr_tree_iter().get_node_id());
    if (!nodeModel) return false;

    auto& canvases = nodeModel->getDrawingCanvasesMut();
    if (static_cast<size_t>(_selectedCanvasIdx) >= canvases.size()) return false;

    double zoom = _pMainWin->get_rt_zoom_scale_factor();
    double dx = (event->x - _dragStartX) / zoom;
    double dy = (event->y - _dragStartY) / zoom;

    auto& canvas = canvases[_selectedCanvasIdx];

    if (_dragType == CtDrawingDragType::Move) {
        canvas.x = _dragCanvasOrigX + dx;
        canvas.y = _dragCanvasOrigY + dy;
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::Resize) {
        double newX = _dragCanvasOrigX;
        double newY = _dragCanvasOrigY;
        double newW = _dragCanvasOrigW;
        double newH = _dragCanvasOrigH;

        switch (_resizeZone) {
            case CtDrawingHitZone::TopLeft:
                newX += dx; newY += dy; newW -= dx; newH -= dy; break;
            case CtDrawingHitZone::TopRight:
                newY += dy; newW += dx; newH -= dy; break;
            case CtDrawingHitZone::BottomLeft:
                newX += dx; newW -= dx; newH += dy; break;
            case CtDrawingHitZone::BottomRight:
                newW += dx; newH += dy; break;
            case CtDrawingHitZone::Left:
                newX += dx; newW -= dx; break;
            case CtDrawingHitZone::Right:
                newW += dx; break;
            case CtDrawingHitZone::Top:
                newY += dy; newH -= dy; break;
            case CtDrawingHitZone::Bottom:
                newH += dy; break;
            default: break;
        }

        if (newW < MIN_CANVAS_WIDTH) { newW = MIN_CANVAS_WIDTH; newX = _dragCanvasOrigX; }
        if (newH < MIN_CANVAS_HEIGHT) { newH = MIN_CANVAS_HEIGHT; newY = _dragCanvasOrigY; }

        canvas.x = newX;
        canvas.y = newY;
        canvas.width = newW;
        canvas.height = newH;
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::Draw) {
        if (!canvas.strokes.empty()) {
            auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
            auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
            double hScroll = hAdj ? hAdj->get_value() : 0.0;
            double vScroll = vAdj ? vAdj->get_value() : 0.0;

            double cx = canvas.x * zoom - hScroll;
            double cy = canvas.y * zoom - vScroll;
            double px = (event->x - cx) / zoom;
            double py = (event->y - cy) / zoom;
            canvas.strokes.back().points.push_back({px, py});
            _drawingArea.queue_draw();
        }
        return true;
    }

    return false;
}

bool CtDrawingOverlay::_onButtonRelease(GdkEventButton* event)
{
    if (!_drawingMode || event->button != 1) return false;
    if (_dragType == CtDrawingDragType::None) return false;

    if (_dragType == CtDrawingDragType::CreateCanvas) {
        _dragType = CtDrawingDragType::None;
        _createCanvasMode = false;

        auto* bridge = _pMainWin->get_command_bridge();
        auto treeIter = _pMainWin->curr_tree_iter();
        if (!bridge || !bridge->isActive() || !treeIter) {
            _drawingArea.queue_draw();
            return true;
        }

        auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
        double hScroll = hAdj ? hAdj->get_value() : 0.0;
        double vScroll = vAdj ? vAdj->get_value() : 0.0;
        double zoom = _pMainWin->get_rt_zoom_scale_factor();

        double sx = std::min(_dragStartX, event->x);
        double sy = std::min(_dragStartY, event->y);
        double w = std::abs(event->x - _dragStartX) / zoom;
        double h = std::abs(event->y - _dragStartY) / zoom;

        if (w >= MIN_CANVAS_WIDTH && h >= MIN_CANVAS_HEIGHT) {
            CtDrawingCanvas canvas;
            canvas.x = (sx + hScroll) / zoom;
            canvas.y = (sy + vScroll) / zoom;
            canvas.width = w;
            canvas.height = h;

            auto cmd = std::make_unique<AddCanvasCommand>(
                bridge->getDocumentModel(), treeIter.get_node_id(), canvas);
            bridge->executeCommand(std::move(cmd));

            auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
            if (nodeModel) {
                _selectedCanvasIdx = static_cast<int>(nodeModel->getDrawingCanvases().size()) - 1;
            }
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        }

        auto gdkWin = _drawingArea.get_window();
        if (gdkWin) gdkWin->set_cursor();
        _drawingArea.queue_draw();
        return true;
    }

    if (_selectedCanvasIdx < 0) return false;

    auto* bridge = _pMainWin->get_command_bridge();
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!treeIter) { _dragType = CtDrawingDragType::None; return false; }

    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (!nodeModel) { _dragType = CtDrawingDragType::None; return false; }

    auto& canvases = nodeModel->getDrawingCanvasesMut();
    size_t ci = static_cast<size_t>(_selectedCanvasIdx);
    if (ci >= canvases.size()) { _dragType = CtDrawingDragType::None; return false; }

    auto& canvas = canvases[ci];

    if (_dragType == CtDrawingDragType::Move) {
        if (canvas.x != _dragCanvasOrigX || canvas.y != _dragCanvasOrigY) {
            double newX = canvas.x, newY = canvas.y;
            canvas.x = _dragCanvasOrigX;
            canvas.y = _dragCanvasOrigY;
            auto cmd = std::make_unique<MoveCanvasCommand>(
                bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                _dragCanvasOrigX, _dragCanvasOrigY, newX, newY);
            bridge->executeCommand(std::move(cmd));
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        }
    }
    else if (_dragType == CtDrawingDragType::Resize) {
        double newX = canvas.x, newY = canvas.y;
        double newW = canvas.width, newH = canvas.height;
        canvas.x = _dragCanvasOrigX;
        canvas.y = _dragCanvasOrigY;
        canvas.width = _dragCanvasOrigW;
        canvas.height = _dragCanvasOrigH;
        if (newX != _dragCanvasOrigX || newY != _dragCanvasOrigY ||
            newW != _dragCanvasOrigW || newH != _dragCanvasOrigH) {
            auto cmd = std::make_unique<ResizeCanvasCommand>(
                bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                _dragCanvasOrigX, _dragCanvasOrigY, _dragCanvasOrigW, _dragCanvasOrigH,
                newX, newY, newW, newH);
            bridge->executeCommand(std::move(cmd));
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        }
    }
    else if (_dragType == CtDrawingDragType::Draw) {
        if (!canvas.strokes.empty()) {
            CtDrawingStroke finishedStroke = canvas.strokes.back();
            canvas.strokes.pop_back();
            if (finishedStroke.points.size() >= 2) {
                auto cmd = std::make_unique<DrawStrokeCommand>(
                    bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                    std::move(finishedStroke));
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
            }
        }
    }

    _dragType = CtDrawingDragType::None;
    _drawingArea.queue_draw();
    return true;
}

bool CtDrawingOverlay::_onScroll(GdkEventScroll* event)
{
    if (event->state & GDK_CONTROL_MASK) {
        auto& textview = _pMainWin->get_text_view();
        bool zoomIn = (event->direction == GDK_SCROLL_UP) ||
                      (event->direction == GDK_SCROLL_SMOOTH && event->delta_y < 0);
        textview.zoom_text(zoomIn, _pMainWin->curr_tree_iter().get_node_syntax_highlighting());
        return true;
    }
    auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
    if (!vAdj) return false;
    double delta = 0.0;
    if (event->direction == GDK_SCROLL_UP) delta = -vAdj->get_step_increment() * 3;
    else if (event->direction == GDK_SCROLL_DOWN) delta = vAdj->get_step_increment() * 3;
    else if (event->direction == GDK_SCROLL_SMOOTH) delta = event->delta_y * vAdj->get_step_increment() * 3;
    if (delta != 0.0) {
        vAdj->set_value(std::clamp(vAdj->get_value() + delta,
                                   vAdj->get_lower(),
                                   vAdj->get_upper() - vAdj->get_page_size()));
    }
    return true;
}

void CtDrawingOverlay::_showContextMenu(GdkEventButton* event)
{
    auto* menu = Gtk::manage(new Gtk::Menu());
    bool hasSelection = _selectedCanvasIdx >= 0;

    if (hasSelection) {
        // Line Width submenu
        auto* widthItem = Gtk::manage(new Gtk::MenuItem(_("Line Width")));
        auto* widthMenu = Gtk::manage(new Gtk::Menu());
        struct WidthEntry { const char* label; double width; };
        WidthEntry widths[] = {
            {_("Thin (1px)"), 1.0},
            {_("Normal (2px)"), 2.0},
            {_("Thick (4px)"), 4.0},
            {_("Very Thick (8px)"), 8.0}
        };
        for (auto& w : widths) {
            auto* item = Gtk::manage(new Gtk::CheckMenuItem(w.label));
            item->set_active(_currentLineWidth == w.width);
            double capturedWidth = w.width;
            item->signal_activate().connect([this, capturedWidth]() {
                _currentLineWidth = capturedWidth;
            });
            widthMenu->append(*item);
        }
        widthItem->set_submenu(*widthMenu);
        menu->append(*widthItem);

        // Color submenu
        auto* colorItem = Gtk::manage(new Gtk::MenuItem(_("Color")));
        auto* colorMenu = Gtk::manage(new Gtk::Menu());
        struct ColorEntry { const char* label; const char* hex; };
        ColorEntry colors[] = {
            {_("Black"), "#000000"},
            {_("Red"), "#ff0000"},
            {_("Blue"), "#0000ff"},
            {_("Green"), "#008000"},
            {_("Yellow"), "#ffff00"},
            {_("White"), "#ffffff"}
        };
        for (auto& c : colors) {
            auto* item = Gtk::manage(new Gtk::CheckMenuItem(c.label));
            item->set_active(_currentColor == c.hex);
            std::string capturedColor = c.hex;
            item->signal_activate().connect([this, capturedColor]() {
                _currentColor = capturedColor;
            });
            colorMenu->append(*item);
        }
        auto* customColorItem = Gtk::manage(new Gtk::MenuItem(_("Custom...")));
        customColorItem->signal_activate().connect([this]() {
            Gtk::ColorChooserDialog dlg(_("Pick a Color"));
            Gdk::RGBA rgba;
            double r, g, b;
            parseColor(_currentColor, r, g, b);
            rgba.set_red(r); rgba.set_green(g); rgba.set_blue(b); rgba.set_alpha(1.0);
            dlg.set_rgba(rgba);
            dlg.set_transient_for(*_pMainWin);
            if (dlg.run() == Gtk::RESPONSE_OK) {
                auto chosen = dlg.get_rgba();
                char buf[8];
                snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                         static_cast<int>(chosen.get_red() * 255),
                         static_cast<int>(chosen.get_green() * 255),
                         static_cast<int>(chosen.get_blue() * 255));
                _currentColor = buf;
            }
        });
        colorMenu->append(*customColorItem);
        colorItem->set_submenu(*colorMenu);
        menu->append(*colorItem);

        // Opacity submenu
        auto* opacityItem = Gtk::manage(new Gtk::MenuItem(_("Opacity")));
        auto* opacityMenu = Gtk::manage(new Gtk::Menu());
        struct OpacityEntry { const char* label; double value; };
        OpacityEntry opacities[] = {
            {"100%", 1.0}, {"75%", 0.75}, {"50%", 0.5}, {"25%", 0.25}
        };
        for (auto& o : opacities) {
            auto* item = Gtk::manage(new Gtk::CheckMenuItem(o.label));
            item->set_active(std::abs(_currentOpacity - o.value) < 0.01);
            double capturedOpacity = o.value;
            item->signal_activate().connect([this, capturedOpacity]() {
                _currentOpacity = capturedOpacity;
            });
            opacityMenu->append(*item);
        }
        opacityItem->set_submenu(*opacityMenu);
        menu->append(*opacityItem);

        menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

        // Canvas Properties
        auto* propsItem = Gtk::manage(new Gtk::MenuItem(_("Canvas Properties...")));
        propsItem->signal_activate().connect([this]() {
            if (_selectedCanvasIdx < 0) return;
            auto* bridge = _pMainWin->get_command_bridge();
            auto treeIter = _pMainWin->curr_tree_iter();
            if (!treeIter || !bridge) return;
            auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
            if (!nodeModel) return;
            const auto& canvases = nodeModel->getDrawingCanvases();
            size_t ci = static_cast<size_t>(_selectedCanvasIdx);
            if (ci >= canvases.size()) return;
            const auto& canvas = canvases[ci];

            CtDialogs::CtCanvasPropsDialogData data;
            data.name = canvas.name;
            data.bgColor = canvas.bgColor;
            data.bgOpacity = canvas.bgOpacity;
            data.cornerRadius = canvas.cornerRadius;
            data.showBorderWhenInactive = canvas.showBorderWhenInactive;

            if (CtDialogs::canvas_properties_dialog(_pMainWin, data)) {
                if (data.name != canvas.name || data.bgColor != canvas.bgColor ||
                    std::abs(data.bgOpacity - canvas.bgOpacity) > 0.001 ||
                    std::abs(data.cornerRadius - canvas.cornerRadius) > 0.001 ||
                    data.showBorderWhenInactive != canvas.showBorderWhenInactive) {
                    auto cmd = std::make_unique<CanvasPropertiesCommand>(
                        bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                        canvas.name, data.name,
                        canvas.bgColor, data.bgColor,
                        canvas.bgOpacity, data.bgOpacity,
                        canvas.cornerRadius, data.cornerRadius,
                        canvas.showBorderWhenInactive, data.showBorderWhenInactive);
                    bridge->executeCommand(std::move(cmd));
                    _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
                }
            }
        });
        menu->append(*propsItem);

        // Delete Stroke mode toggle
        auto* deleteStrokeItem = Gtk::manage(new Gtk::CheckMenuItem(_("Delete Stroke")));
        deleteStrokeItem->set_active(_deleteStrokeMode);
        deleteStrokeItem->signal_activate().connect([this]() {
            _deleteStrokeMode = !_deleteStrokeMode;
        });
        menu->append(*deleteStrokeItem);

        menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
    }

    // Cut Drawing Canvas
    auto* cutItem = Gtk::manage(new Gtk::MenuItem(_("Cut Drawing Canvas")));
    cutItem->set_sensitive(hasSelection);
    cutItem->signal_activate().connect([this]() {
        if (_selectedCanvasIdx < 0) return;
        auto* bridge = _pMainWin->get_command_bridge();
        auto treeIter = _pMainWin->curr_tree_iter();
        if (!treeIter || !bridge) return;
        auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
        if (!nodeModel) return;
        const auto& canvases = nodeModel->getDrawingCanvases();
        size_t ci = static_cast<size_t>(_selectedCanvasIdx);
        if (ci >= canvases.size()) return;

        _clipboard = canvases[ci];
        auto cmd = std::make_unique<DeleteCanvasCommand>(
            bridge->getDocumentModel(), treeIter.get_node_id(),
            canvases[ci], ci);
        bridge->executeCommand(std::move(cmd));
        _selectedCanvasIdx = -1;
        _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
    });
    menu->append(*cutItem);

    // Copy Drawing Canvas
    auto* copyItem = Gtk::manage(new Gtk::MenuItem(_("Copy Drawing Canvas")));
    copyItem->set_sensitive(hasSelection);
    copyItem->signal_activate().connect([this]() {
        if (_selectedCanvasIdx < 0) return;
        auto* bridge = _pMainWin->get_command_bridge();
        auto treeIter = _pMainWin->curr_tree_iter();
        if (!treeIter || !bridge) return;
        auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
        if (!nodeModel) return;
        const auto& canvases = nodeModel->getDrawingCanvases();
        size_t ci = static_cast<size_t>(_selectedCanvasIdx);
        if (ci >= canvases.size()) return;

        _clipboard = canvases[ci];
    });
    menu->append(*copyItem);

    // Paste Drawing Canvas
    auto* pasteItem = Gtk::manage(new Gtk::MenuItem(_("Paste Drawing Canvas")));
    pasteItem->set_sensitive(_clipboard.has_value());
    pasteItem->signal_activate().connect([this, event]() {
        if (!_clipboard.has_value()) return;
        auto* bridge = _pMainWin->get_command_bridge();
        auto treeIter = _pMainWin->curr_tree_iter();
        if (!treeIter || !bridge) return;

        CtDrawingCanvas canvas = _clipboard.value();

        auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
        double hScroll = hAdj ? hAdj->get_value() : 0.0;
        double vScroll = vAdj ? vAdj->get_value() : 0.0;
        double zoom = _pMainWin->get_rt_zoom_scale_factor();

        canvas.x = (event->x + hScroll) / zoom;
        canvas.y = (event->y + vScroll) / zoom;

        auto cmd = std::make_unique<AddCanvasCommand>(
            bridge->getDocumentModel(), treeIter.get_node_id(),
            std::move(canvas));
        bridge->executeCommand(std::move(cmd));
        _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
    });
    menu->append(*pasteItem);

    if (hasSelection) {
        menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

        // Delete Drawing Canvas
        auto* deleteCanvasItem = Gtk::manage(new Gtk::MenuItem(_("Delete Drawing Canvas")));
        deleteCanvasItem->signal_activate().connect([this]() {
            if (_selectedCanvasIdx < 0) return;
            Gtk::MessageDialog dlg(*_pMainWin, _("Are you sure you want to delete this drawing canvas?"),
                                   false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO);
            if (dlg.run() != Gtk::RESPONSE_YES) return;

            auto* bridge = _pMainWin->get_command_bridge();
            auto treeIter = _pMainWin->curr_tree_iter();
            if (!treeIter) return;
            auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
            if (!nodeModel) return;
            const auto& canvases = nodeModel->getDrawingCanvases();
            size_t ci = static_cast<size_t>(_selectedCanvasIdx);
            if (ci >= canvases.size()) return;

            auto cmd = std::make_unique<DeleteCanvasCommand>(
                bridge->getDocumentModel(), treeIter.get_node_id(),
                canvases[ci], ci);
            bridge->executeCommand(std::move(cmd));
            _selectedCanvasIdx = -1;
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        });
        menu->append(*deleteCanvasItem);
    }

    menu->show_all();
    menu->popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
}
