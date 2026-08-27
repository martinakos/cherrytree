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

static void applyLineStyle(const Cairo::RefPtr<Cairo::Context>& cr, CtDrawingLineStyle style, double lineWidth)
{
    switch (style) {
    case CtDrawingLineStyle::Dashed: {
        std::vector<double> d{lineWidth * 3.0, lineWidth * 3.0};
        cr->set_dash(d, 0.0);
        break;
    }
    case CtDrawingLineStyle::Dotted: {
        std::vector<double> d{lineWidth, lineWidth * 3.0};
        cr->set_dash(d, 0.0);
        break;
    }
    case CtDrawingLineStyle::DashDot: {
        std::vector<double> d{lineWidth * 3.0, lineWidth * 2.5, lineWidth, lineWidth * 2.5};
        cr->set_dash(d, 0.0);
        break;
    }
    case CtDrawingLineStyle::Solid:
    default:
        cr->unset_dash();
        break;
    }
}

std::optional<CtDrawingCanvas> CtDrawingOverlay::_clipboard;
std::vector<CtDrawingStroke> CtDrawingOverlay::_strokeClipboard;

CtDrawingOverlay::CtDrawingOverlay(CtMainWin* pMainWin)
    : _pMainWin(pMainWin)
{
    _drawingArea.set_can_focus(true);
    _drawingArea.set_hexpand(true);
    _drawingArea.set_vexpand(true);

#if GTKMM_MAJOR_VERSION < 4
    _drawingArea.signal_draw().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onDraw));
    _drawingArea.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
                            Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_MOTION_MASK |
                            Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK |
                            Gdk::KEY_PRESS_MASK);
    _drawingArea.signal_button_press_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onButtonPress), false);
    _drawingArea.signal_button_release_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onButtonRelease), false);
    _drawingArea.signal_motion_notify_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onMotionNotify), false);
    _drawingArea.signal_scroll_event().connect(sigc::mem_fun(*this, &CtDrawingOverlay::_onScroll), false);
    _drawingArea.signal_key_press_event().connect([this](GdkEventKey* ev) -> bool {
        if (ev->keyval == GDK_KEY_Escape && _polylineActive) {
            _polylineActive = false;
            _previewActive = false;
            _polylinePoints.clear();
            _drawingArea.queue_draw();
            return true;
        }
        if (ev->keyval == GDK_KEY_Escape && _bezierActive) {
            _bezierActive = false;
            _bezierPhase = 0;
            _previewActive = false;
            _bezierPoints.clear();
            _drawingArea.queue_draw();
            return true;
        }
        if (_currentTool == CtDrawingTool::Select) {
            if (ev->keyval == GDK_KEY_Escape) {
                _clearStrokeSelection();
                _drawingArea.queue_draw();
                return true;
            }
            if ((ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_BackSpace) &&
                !_selectedStrokeIndices.empty()) {
                _deleteSelectedStrokes();
                return true;
            }
            bool ctrl = (ev->state & GDK_CONTROL_MASK) != 0;
            if (ctrl && (ev->keyval == GDK_KEY_c || ev->keyval == GDK_KEY_C)) {
                _copySelectedStrokes();
                return true;
            }
            if (ctrl && (ev->keyval == GDK_KEY_x || ev->keyval == GDK_KEY_X)) {
                _cutSelectedStrokes();
                return true;
            }
            if (ctrl && (ev->keyval == GDK_KEY_v || ev->keyval == GDK_KEY_V)) {
                _pasteStrokes();
                return true;
            }
        }
        return false;
    }, false);
    _drawingArea.signal_realize().connect([this]() {
        if (auto gdkWin = _drawingArea.get_window()) {
            gdk_window_set_pass_through(gdkWin->gobj(), !_drawingMode);
        }
    });
#endif

    _buildToolbar();
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

void CtDrawingOverlay::addToolbarToOverlay()
{
    if (!_pToolbarEventBox) return;
    _overlay.add_overlay(*_pToolbarEventBox);
    _overlay.set_overlay_pass_through(*_pToolbarEventBox, false);
    g_signal_connect(_overlay.gobj(), "get-child-position",
                     G_CALLBACK(&CtDrawingOverlay::_onGetChildPosition), this);
}

gboolean CtDrawingOverlay::_onGetChildPosition(GtkOverlay*, GtkWidget* widget,
                                                GdkRectangle* alloc, gpointer data)
{
    auto* self = static_cast<CtDrawingOverlay*>(data);
    if (widget != GTK_WIDGET(self->_pToolbarEventBox->gobj())) return FALSE;
    GtkRequisition req;
    gtk_widget_get_preferred_size(widget, nullptr, &req);
    alloc->x = self->_toolbarPosX;
    alloc->y = self->_toolbarPosY;
    alloc->width = req.width;
    alloc->height = req.height;
    return TRUE;
}

void CtDrawingOverlay::setDrawingMode(bool on)
{
    _drawingMode = on;
    if (!on) {
        _deleteStrokeMode = false;
        _createCanvasMode = false;
        _dragType = CtDrawingDragType::None;
        _previewActive = false;
        _hideToolbar();
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

    if (on && _selectedCanvasIdx >= 0) {
        _showToolbar();
    }
    if (on) {
        _drawingArea.grab_focus();
    }
}

void CtDrawingOverlay::setCurrentTool(CtDrawingTool tool)
{
    _currentTool = tool;
    _deleteStrokeMode = (tool == CtDrawingTool::Rubber);
    _previewActive = false;
    _polylineActive = false;
    _polylinePoints.clear();
    _bezierActive = false;
    _bezierPhase = 0;
    _bezierPoints.clear();
    _selectStrokeIdx = -1;
    _selectDragPointIdx = -1;
    _selectOrigPoints.clear();
    _selectedStrokeIndices.clear();
    _moveSelOrigPoints.clear();
    _selectBandStartX = _selectBandStartY = 0.0;
    _selectBandEndX = _selectBandEndY = 0.0;
    _scaleHandleIdx = -1;
    _scaleOrigPoints.clear();
    _updateToolButtonStates();
    _drawingArea.queue_draw();
}

void CtDrawingOverlay::resetSelection()
{
    _selectedCanvasIdx = -1;
    _deleteStrokeMode = false;
    _createCanvasMode = false;
    _dragType = CtDrawingDragType::None;
    _previewActive = false;
    _polylineActive = false;
    _polylinePoints.clear();
    _bezierActive = false;
    _bezierPhase = 0;
    _bezierPoints.clear();
    _selectStrokeIdx = -1;
    _selectDragPointIdx = -1;
    _selectOrigPoints.clear();
    _selectedStrokeIndices.clear();
    _moveSelOrigPoints.clear();
    _selectBandStartX = _selectBandStartY = 0.0;
    _selectBandEndX = _selectBandEndY = 0.0;
    _scaleHandleIdx = -1;
    _scaleOrigPoints.clear();
    _hideToolbar();
    _drawingArea.queue_draw();
}

void CtDrawingOverlay::refresh()
{
    _drawingArea.queue_draw();
}

void CtDrawingOverlay::_buildToolbar()
{
    _pToolbarBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 2));
    _pToolbarBox->get_style_context()->add_class("drawing-toolbar");

    auto makeCss = []() {
        auto css = Gtk::CssProvider::create();
        css->load_from_data(
            ".drawing-toolbar {"
            "  background: linear-gradient(to bottom, #f6f5f4, #eeecea);"
            "  border: 1px solid #b5b3af;"
            "  border-radius: 9px;"
            "  padding: 4px 6px;"
            "  box-shadow: 0 2px 4px rgba(0,0,0,0.15);"
            "}"
            ".drawing-toolbar button {"
            "  min-width: 28px; min-height: 28px;"
            "  padding: 2px;"
            "  border-radius: 5px;"
            "  background: transparent;"
            "  border: none;"
            "  box-shadow: none;"
            "}"
            ".drawing-toolbar button:hover {"
            "  background: rgba(0,0,0,0.08);"
            "}"
            ".drawing-toolbar button:checked {"
            "  background: #1c71d8;"
            "}"
            ".drawing-toolbar button:checked image {"
            "  color: white;"
            "}"
            ".drawing-toolbar .separator {"
            "  min-width: 1px;"
            "  background: #c0bfbc;"
            "  margin: 4px 4px;"
            "}"
        );
        return css;
    };
    auto css = makeCss();
    _pToolbarBox->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    auto addToolBtn = [&](const char* iconName, CtDrawingTool tool) -> Gtk::ToggleButton* {
        auto* btn = Gtk::manage(new Gtk::ToggleButton());
        btn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        auto* img = Gtk::manage(new Gtk::Image());
        img->set_from_resource(std::string("/icons/") + iconName);
        img->set_pixel_size(18);
        btn->set_image(*img);
        btn->set_always_show_image(true);
        btn->set_tooltip_text(
            tool == CtDrawingTool::Select ? _("Select") :
            tool == CtDrawingTool::Pencil ? _("Pencil") :
            tool == CtDrawingTool::Line   ? _("Line") :
            tool == CtDrawingTool::Shape  ? _("Shape") :
            tool == CtDrawingTool::Text   ? _("Text") :
                                            _("Rotate"));
        btn->signal_clicked().connect([this, btn, tool]() {
            if (_updatingToolButtons) return;
            if (btn->get_active()) {
                setCurrentTool(tool);
            } else if (_currentTool == tool) {
                btn->set_active(true);
            }
        });
        _pToolbarBox->pack_start(*btn, false, false);
        _toolButtons.push_back(btn);
        return btn;
    };

    addToolBtn("ct_draw_select.svg", CtDrawingTool::Select);

    // Pencil/Line/Polyline/Bezier submenu
    auto* lineBtn = addToolBtn("ct_draw_pencil.svg", CtDrawingTool::Pencil);
    _pLineToolIcon = dynamic_cast<Gtk::Image*>(lineBtn->get_image());
    auto* lineMenu = Gtk::manage(new Gtk::Menu());
    {
        struct LineEntry { const char* label; const char* icon; CtDrawingTool tool; CtDrawingElementType type; };
        LineEntry lineTypes[] = {
            {_("Pencil"), "ct_draw_pencil.svg", CtDrawingTool::Pencil, CtDrawingElementType::Freehand},
            {_("Line"), "ct_draw_line.svg", CtDrawingTool::Line, CtDrawingElementType::Line},
            {_("Polyline"), "ct_draw_polyline.svg", CtDrawingTool::Line, CtDrawingElementType::Polyline},
            {_("Bezier Curve"), "ct_draw_bezier.svg", CtDrawingTool::Line, CtDrawingElementType::BezierCurve}
        };
        for (auto& lt : lineTypes) {
            auto* img = Gtk::manage(new Gtk::Image());
            img->set_from_resource(std::string("/icons/") + lt.icon);
            img->set_pixel_size(16);
            auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
            box->pack_start(*img, false, false);
            box->pack_start(*Gtk::manage(new Gtk::Label(lt.label)), false, false);
            auto* item = Gtk::manage(new Gtk::MenuItem());
            item->add(*box);
            CtDrawingTool ltool = lt.tool;
            CtDrawingElementType ltype = lt.type;
            const char* licon = lt.icon;
            item->signal_activate().connect([this, ltool, ltype, licon, lineBtn]() {
                if (ltool == CtDrawingTool::Line) {
                    _currentLineType = ltype;
                }
                if (_pLineToolIcon) {
                    _pLineToolIcon->set_from_resource(std::string("/icons/") + licon);
                    _pLineToolIcon->set_pixel_size(18);
                }
                lineBtn->set_tooltip_text(
                    ltool == CtDrawingTool::Pencil ? _("Pencil") :
                    ltype == CtDrawingElementType::Line ? _("Line") :
                    ltype == CtDrawingElementType::Polyline ? _("Polyline") :
                                                              _("Bezier Curve"));
                setCurrentTool(ltool);
            });
            lineMenu->append(*item);
        }
        lineMenu->show_all();
    }
    lineBtn->signal_button_press_event().connect([this, lineMenu, lineBtn](GdkEventButton* ev) -> bool {
        if (ev->button == 1) {
            lineMenu->popup_at_widget(lineBtn, Gdk::GRAVITY_SOUTH, Gdk::GRAVITY_NORTH, nullptr);
            return true;
        }
        return false;
    }, false);

    // Shape tool — left click opens submenu (Rectangle / Ellipse / Triangle / Diamond)
    auto* shapeBtn = addToolBtn("ct_draw_rectangle.svg", CtDrawingTool::Shape);
    _pShapeToolIcon = dynamic_cast<Gtk::Image*>(shapeBtn->get_image());
    auto* shapeMenu = Gtk::manage(new Gtk::Menu());
    {
        struct ShapeEntry { const char* label; const char* icon; CtDrawingElementType type; };
        ShapeEntry shapeTypes[] = {
            {_("Rectangle"), "ct_draw_rectangle.svg", CtDrawingElementType::Rectangle},
            {_("Rounded Rectangle"), "ct_draw_rounded_rectangle.svg", CtDrawingElementType::RoundedRectangle},
            {_("Ellipse"), "ct_draw_ellipse.svg", CtDrawingElementType::Ellipse},
            {_("Triangle"), "ct_draw_triangle.svg", CtDrawingElementType::Triangle},
            {_("Diamond"), "ct_draw_diamond.svg", CtDrawingElementType::Diamond}
        };
        for (auto& st : shapeTypes) {
            auto* img = Gtk::manage(new Gtk::Image());
            img->set_from_resource(std::string("/icons/") + st.icon);
            img->set_pixel_size(16);
            auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
            box->pack_start(*img, false, false);
            box->pack_start(*Gtk::manage(new Gtk::Label(st.label)), false, false);
            auto* item = Gtk::manage(new Gtk::MenuItem());
            item->add(*box);
            CtDrawingElementType stype = st.type;
            const char* sicon = st.icon;
            item->signal_activate().connect([this, stype, sicon, shapeBtn]() {
                _currentShapeType = stype;
                if (_pShapeToolIcon) {
                    _pShapeToolIcon->set_from_resource(std::string("/icons/") + sicon);
                    _pShapeToolIcon->set_pixel_size(18);
                }
                shapeBtn->set_tooltip_text(
                    stype == CtDrawingElementType::Rectangle        ? _("Shape (Rectangle)") :
                    stype == CtDrawingElementType::RoundedRectangle ? _("Shape (Rounded Rectangle)") :
                    stype == CtDrawingElementType::Ellipse          ? _("Shape (Ellipse)") :
                    stype == CtDrawingElementType::Triangle          ? _("Shape (Triangle)") :
                                                                       _("Shape (Diamond)"));
                setCurrentTool(CtDrawingTool::Shape);
            });
            shapeMenu->append(*item);
        }
        shapeMenu->show_all();
    }
    shapeBtn->signal_button_press_event().connect([this, shapeMenu, shapeBtn](GdkEventButton* ev) -> bool {
        if (ev->button == 1) {
            shapeMenu->popup_at_widget(shapeBtn, Gdk::GRAVITY_SOUTH, Gdk::GRAVITY_NORTH, nullptr);
            return true;
        }
        return false;
    }, false);

    addToolBtn("ct_draw_text.svg", CtDrawingTool::Text);

    // Rotate/Scale submenu
    auto* rotateBtn = addToolBtn("ct_draw_rotate.svg", CtDrawingTool::Rotate);
    _pRotateToolIcon = dynamic_cast<Gtk::Image*>(rotateBtn->get_image());
    {
        auto* menu = Gtk::manage(new Gtk::Menu());
        struct Entry { const char* label; const char* icon; CtDrawingTool tool; };
        Entry entries[] = {
            {_("Rotate"), "ct_draw_rotate.svg", CtDrawingTool::Rotate},
            {_("Scale"), "ct_draw_scale.svg", CtDrawingTool::Scale}
        };
        for (auto& e : entries) {
            auto* img = Gtk::manage(new Gtk::Image());
            img->set_from_resource(std::string("/icons/") + e.icon);
            img->set_pixel_size(16);
            auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
            box->pack_start(*img, false, false);
            box->pack_start(*Gtk::manage(new Gtk::Label(e.label)), false, false);
            auto* item = Gtk::manage(new Gtk::MenuItem());
            item->add(*box);
            CtDrawingTool etool = e.tool;
            const char* eicon = e.icon;
            item->signal_activate().connect([this, etool, eicon, rotateBtn]() {
                if (_pRotateToolIcon) {
                    _pRotateToolIcon->set_from_resource(std::string("/icons/") + eicon);
                    _pRotateToolIcon->set_pixel_size(18);
                }
                rotateBtn->set_tooltip_text(
                    etool == CtDrawingTool::Rotate ? _("Rotate") : _("Scale"));
                setCurrentTool(etool);
            });
            menu->append(*item);
        }
        menu->show_all();
        rotateBtn->signal_button_press_event().connect([this, menu, rotateBtn](GdkEventButton* ev) -> bool {
            if (ev->button == 1) {
                menu->popup_at_widget(rotateBtn, Gdk::GRAVITY_SOUTH, Gdk::GRAVITY_NORTH, nullptr);
                return true;
            }
            return false;
        }, false);
    }

    // separator
    auto* sep1 = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL));
    sep1->get_style_context()->add_class("separator");
    sep1->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    _pToolbarBox->pack_start(*sep1, false, false);

    // thickness dropdown
    auto* thickBtn = Gtk::manage(new Gtk::MenuButton());
    thickBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    thickBtn->set_tooltip_text(_("Line Width"));
    auto* thickLabel = Gtk::manage(new Gtk::Label("2px"));
    thickBtn->add(*thickLabel);
    auto* thickMenu = Gtk::manage(new Gtk::Menu());
    for (int tw = 1; tw <= 10; ++tw) {
        std::string label = std::to_string(tw) + "px";
        auto* item = Gtk::manage(new Gtk::MenuItem(label));
        item->signal_activate().connect([this, tw, thickLabel]() {
            _currentLineWidth = static_cast<double>(tw);
            thickLabel->set_text(std::to_string(tw) + "px");
            _applyPropertyToSelection([tw](CtDrawingStroke& s) {
                s.lineWidth = static_cast<double>(tw);
            });
        });
        thickMenu->append(*item);
    }
    thickMenu->show_all();
    thickBtn->set_popup(*thickMenu);
    _pToolbarBox->pack_start(*thickBtn, false, false);

    // arrow head dropdown
    auto* arrowBtn = Gtk::manage(new Gtk::MenuButton());
    arrowBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    arrowBtn->set_tooltip_text(_("Arrow Heads"));
    auto* arrowImg = Gtk::manage(new Gtk::Image());
    arrowImg->set_from_resource("/icons/ct_draw_line.svg");
    arrowImg->set_pixel_size(16);
    arrowBtn->set_image(*arrowImg);
    arrowBtn->set_always_show_image(true);
    auto* arrowMenu = Gtk::manage(new Gtk::Menu());
    {
        struct ArrowEntry { const char* label; const char* icon;
                            CtDrawingArrowHead head; CtDrawingArrowStyle style; };
        ArrowEntry arrowTypes[] = {
            {_("None"), "ct_draw_line.svg",
             CtDrawingArrowHead::None, CtDrawingArrowStyle::Solid},
            {_("End (Solid)"), "ct_draw_arrow_end_solid.svg",
             CtDrawingArrowHead::End, CtDrawingArrowStyle::Solid},
            {_("End (Open)"), "ct_draw_arrow_end_open.svg",
             CtDrawingArrowHead::End, CtDrawingArrowStyle::Open},
            {_("Start (Solid)"), "ct_draw_arrow_start_solid.svg",
             CtDrawingArrowHead::Start, CtDrawingArrowStyle::Solid},
            {_("Start (Open)"), "ct_draw_arrow_start_open.svg",
             CtDrawingArrowHead::Start, CtDrawingArrowStyle::Open},
            {_("Both (Solid)"), "ct_draw_arrow_both_solid.svg",
             CtDrawingArrowHead::Both, CtDrawingArrowStyle::Solid},
            {_("Both (Open)"), "ct_draw_arrow_both_open.svg",
             CtDrawingArrowHead::Both, CtDrawingArrowStyle::Open},
        };
        for (auto& at : arrowTypes) {
            auto* img = Gtk::manage(new Gtk::Image());
            img->set_from_resource(std::string("/icons/") + at.icon);
            img->set_pixel_size(16);
            auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
            box->pack_start(*img, false, false);
            box->pack_start(*Gtk::manage(new Gtk::Label(at.label)), false, false);
            auto* item = Gtk::manage(new Gtk::MenuItem());
            item->add(*box);
            CtDrawingArrowHead aHead = at.head;
            CtDrawingArrowStyle aStyle = at.style;
            const char* aIcon = at.icon;
            item->signal_activate().connect([this, aHead, aStyle, aIcon, arrowImg]() {
                _currentArrowHead = aHead;
                _currentArrowStyle = aStyle;
                arrowImg->set_from_resource(std::string("/icons/") + aIcon);
                arrowImg->set_pixel_size(16);
                _applyPropertyToSelection([aHead, aStyle](CtDrawingStroke& s) {
                    s.arrowHead = aHead;
                    s.arrowStyle = aStyle;
                });
            });
            arrowMenu->append(*item);
        }
        arrowMenu->show_all();
    }
    arrowBtn->set_popup(*arrowMenu);
    _pToolbarBox->pack_start(*arrowBtn, false, false);

    // line style dropdown — drawn line previews instead of text
    auto* styleBtn = Gtk::manage(new Gtk::MenuButton());
    styleBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    styleBtn->set_tooltip_text(_("Line Style"));
    auto makeStyleDraw = [](CtDrawingLineStyle st, int w, int h) {
        auto* da = Gtk::manage(new Gtk::DrawingArea());
        da->set_size_request(w, h);
        da->signal_draw().connect([st, w, h](const Cairo::RefPtr<Cairo::Context>& cr) -> bool {
            cr->set_source_rgb(0.3, 0.3, 0.3);
            cr->set_line_width(2.0);
            cr->set_line_cap(Cairo::LINE_CAP_ROUND);
            applyLineStyle(cr, st, 2.0);
            cr->move_to(2, h / 2.0);
            cr->line_to(w - 2, h / 2.0);
            cr->stroke();
            return true;
        });
        return da;
    };
    auto styleBtnStyle = std::make_shared<CtDrawingLineStyle>(CtDrawingLineStyle::Solid);
    auto* styleBtnDraw = Gtk::manage(new Gtk::DrawingArea());
    styleBtnDraw->set_size_request(24, 16);
    styleBtnDraw->signal_draw().connect([styleBtnStyle, styleBtnDraw](const Cairo::RefPtr<Cairo::Context>& cr) -> bool {
        int w = styleBtnDraw->get_allocated_width();
        int h = styleBtnDraw->get_allocated_height();
        cr->set_source_rgb(0.3, 0.3, 0.3);
        cr->set_line_width(2.0);
        cr->set_line_cap(Cairo::LINE_CAP_ROUND);
        applyLineStyle(cr, *styleBtnStyle, 2.0);
        cr->move_to(2, h / 2.0);
        cr->line_to(w - 2, h / 2.0);
        cr->stroke();
        return true;
    });
    styleBtn->add(*styleBtnDraw);
    auto* styleMenu = Gtk::manage(new Gtk::Menu());
    CtDrawingLineStyle styles[] = {
        CtDrawingLineStyle::Solid,
        CtDrawingLineStyle::Dashed,
        CtDrawingLineStyle::Dotted,
        CtDrawingLineStyle::DashDot
    };
    for (auto sv : styles) {
        auto* item = Gtk::manage(new Gtk::MenuItem());
        item->add(*makeStyleDraw(sv, 80, 16));
        item->signal_activate().connect([this, sv, styleBtnStyle, styleBtnDraw]() {
            _currentLineStyle = sv;
            *styleBtnStyle = sv;
            styleBtnDraw->queue_draw();
            _applyPropertyToSelection([sv](CtDrawingStroke& s) {
                s.lineStyle = sv;
            });
        });
        styleMenu->append(*item);
    }
    styleMenu->show_all();
    styleBtn->set_popup(*styleMenu);
    _pToolbarBox->pack_start(*styleBtn, false, false);

    // color button
    auto* colorBtn = Gtk::manage(new Gtk::ColorButton());
    colorBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    colorBtn->set_tooltip_text(_("Color"));
    {
        Gdk::RGBA rgba;
        rgba.set_red(0); rgba.set_green(0); rgba.set_blue(0); rgba.set_alpha(1.0);
        colorBtn->set_rgba(rgba);
    }
    colorBtn->signal_color_set().connect([this, colorBtn]() {
        auto c = colorBtn->get_rgba();
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                 static_cast<int>(c.get_red() * 255),
                 static_cast<int>(c.get_green() * 255),
                 static_cast<int>(c.get_blue() * 255));
        _currentColor = buf;
        std::string newColor = _currentColor;
        _applyPropertyToSelection([newColor](CtDrawingStroke& s) {
            s.color = newColor;
        });
    });
    _pToolbarBox->pack_start(*colorBtn, false, false);

    // opacity dropdown
    auto* opacBtn = Gtk::manage(new Gtk::MenuButton());
    opacBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    opacBtn->set_tooltip_text(_("Opacity"));
    auto* opacLabel = Gtk::manage(new Gtk::Label("100%"));
    opacBtn->add(*opacLabel);
    auto* opacMenu = Gtk::manage(new Gtk::Menu());
    struct OpacEntry { const char* label; double val; };
    OpacEntry opacs[] = {{"100%", 1.0}, {"75%", 0.75}, {"50%", 0.5}, {"25%", 0.25}};
    for (auto& o : opacs) {
        auto* item = Gtk::manage(new Gtk::MenuItem(o.label));
        double ov = o.val;
        const char* ol = o.label;
        item->signal_activate().connect([this, ov, ol, opacLabel]() {
            _currentOpacity = ov;
            opacLabel->set_text(ol);
            _applyPropertyToSelection([ov](CtDrawingStroke& s) {
                s.opacity = ov;
            });
        });
        opacMenu->append(*item);
    }
    opacMenu->show_all();
    opacBtn->set_popup(*opacMenu);
    _pToolbarBox->pack_start(*opacBtn, false, false);

    // filled toggle
    auto* filledBtn = Gtk::manage(new Gtk::ToggleButton(_("Fill")));
    filledBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    filledBtn->set_tooltip_text(_("Fill shapes"));
    filledBtn->signal_toggled().connect([this, filledBtn]() {
        _currentFilled = filledBtn->get_active();
        bool filled = _currentFilled;
        _applyPropertyToSelection([filled](CtDrawingStroke& s) {
            s.filled = filled;
        });
    });
    _pToolbarBox->pack_start(*filledBtn, false, false);

    // separator
    auto* sep2 = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL));
    sep2->get_style_context()->add_class("separator");
    sep2->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    _pToolbarBox->pack_start(*sep2, false, false);

    // canvas properties button
    auto* propsBtn = Gtk::manage(new Gtk::Button());
    propsBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    propsBtn->set_tooltip_text(_("Canvas Properties"));
    auto* propsImg = Gtk::manage(new Gtk::Image());
    propsImg->set_from_icon_name("preferences-system", Gtk::ICON_SIZE_SMALL_TOOLBAR);
    propsBtn->set_image(*propsImg);
    propsBtn->set_always_show_image(true);
    propsBtn->signal_clicked().connect([this]() {
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
        data.tsCreation = canvas.tsCreation;
        data.tsLastSave = canvas.tsLastSave;
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
    _pToolbarBox->pack_start(*propsBtn, false, false);

    // delete canvas button
    auto* delBtn = Gtk::manage(new Gtk::Button());
    delBtn->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    delBtn->set_tooltip_text(_("Delete Drawing Canvas"));
    auto* delImg = Gtk::manage(new Gtk::Image());
    delImg->set_from_icon_name("edit-delete", Gtk::ICON_SIZE_SMALL_TOOLBAR);
    delBtn->set_image(*delImg);
    delBtn->set_always_show_image(true);
    delBtn->signal_clicked().connect([this]() {
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
        _hideToolbar();
        _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
    });
    _pToolbarBox->pack_start(*delBtn, false, false);

    _pToolbarEventBox = Gtk::manage(new Gtk::EventBox());
    _pToolbarEventBox->add(*_pToolbarBox);
    _pToolbarEventBox->set_above_child(false);

    _pToolbarBox->show_all();
    _pToolbarEventBox->set_no_show_all(true);
    _pToolbarEventBox->hide();

    auto connectScrollUpdate = [this]() {
        auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
        if (hAdj) hAdj->signal_value_changed().connect([this]() {
            if (_toolbarVisible) _updateToolbarPosition();
        });
        if (vAdj) vAdj->signal_value_changed().connect([this]() {
            if (_toolbarVisible) _updateToolbarPosition();
        });
    };
    _pMainWin->getScrolledwindowText().signal_realize().connect(connectScrollUpdate);

    _updateToolButtonStates();
}

void CtDrawingOverlay::_showToolbar()
{
    if (!_pToolbarEventBox) return;
    _toolbarVisible = true;
    _pToolbarEventBox->show();
    _updateToolbarPosition();
}

void CtDrawingOverlay::_hideToolbar()
{
    if (!_pToolbarEventBox) return;
    _toolbarVisible = false;
    _pToolbarEventBox->hide();
}

void CtDrawingOverlay::_updateToolbarPosition()
{
    if (!_pToolbarEventBox || _selectedCanvasIdx < 0) return;

    auto* bridge = _pMainWin->get_command_bridge();
    if (!bridge || !bridge->isActive()) return;
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!treeIter) return;
    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return;
    const auto& canvases = nodeModel->getDrawingCanvases();
    if (static_cast<size_t>(_selectedCanvasIdx) >= canvases.size()) return;

    auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
    double hScroll = hAdj ? hAdj->get_value() : 0.0;
    double vScroll = vAdj ? vAdj->get_value() : 0.0;
    double zoom = _pMainWin->get_rt_zoom_scale_factor();

    const auto& canvas = canvases[_selectedCanvasIdx];
    double cx = canvas.x * zoom - hScroll;
    double cy = canvas.y * zoom - vScroll;
    double cw = canvas.width * zoom;
    double ch = canvas.height * zoom;

    int tbW = 0, tbH = 0;
    _pToolbarEventBox->get_preferred_width(tbW, tbW);
    _pToolbarEventBox->get_preferred_height(tbH, tbH);

    int areaH = _drawingArea.get_allocated_height();
    double tbX = cx + (cw - tbW) / 2.0;
    double tbY = cy + ch + TOOLBAR_GAP;

    if (tbY + tbH > areaH) {
        tbY = cy - tbH - TOOLBAR_GAP;
    }

    int newX = static_cast<int>(std::max(0.0, tbX));
    int newY = static_cast<int>(std::max(0.0, tbY));
    if (_toolbarPosX != newX || _toolbarPosY != newY) {
        _toolbarPosX = newX;
        _toolbarPosY = newY;
        _pToolbarEventBox->queue_resize();
    }
}

void CtDrawingOverlay::_updateToolButtonStates()
{
    if (_toolButtons.size() < 5) return;
    _updatingToolButtons = true;
    for (size_t i = 0; i < 5; ++i) {
        if (i == 0) {
            _toolButtons[i]->set_active(_currentTool == CtDrawingTool::Select);
        } else if (i == 1) {
            _toolButtons[i]->set_active(_currentTool == CtDrawingTool::Pencil ||
                                        _currentTool == CtDrawingTool::Line);
        } else if (i == 2) {
            _toolButtons[i]->set_active(_currentTool == CtDrawingTool::Shape);
        } else if (i == 3) {
            _toolButtons[i]->set_active(_currentTool == CtDrawingTool::Text);
        } else if (i == 4) {
            _toolButtons[i]->set_active(_currentTool == CtDrawingTool::Rotate ||
                                        _currentTool == CtDrawingTool::Scale);
        }
    }
    _updatingToolButtons = false;
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

    if (_dragType == CtDrawingDragType::SelectBand) {
        double sx = std::min(_selectBandStartX, _selectBandEndX);
        double sy = std::min(_selectBandStartY, _selectBandEndY);
        double sw = std::abs(_selectBandEndX - _selectBandStartX);
        double sh = std::abs(_selectBandEndY - _selectBandStartY);
        cr->rectangle(sx, sy, sw, sh);
        cr->set_source_rgba(0.2, 0.5, 1.0, 0.15);
        cr->fill_preserve();
        cr->set_source_rgba(0.2, 0.5, 1.0, 0.7);
        cr->set_line_width(1.0);
        std::vector<double> dashes{4.0, 3.0};
        cr->set_dash(dashes, 0.0);
        cr->stroke();
        cr->unset_dash();
    }

    // draw preview for line/shape tools (not polyline/bezier — those have their own blocks)
    if (_previewActive && !_polylineActive && !_bezierActive && _selectedCanvasIdx >= 0) {
        auto nodeModel2 = docModel->getNodeById(treeIter.get_node_id());
        if (nodeModel2) {
            const auto& pCanvas = nodeModel2->getDrawingCanvases();
            if (static_cast<size_t>(_selectedCanvasIdx) < pCanvas.size()) {
                const auto& canvas = pCanvas[_selectedCanvasIdx];
                double pcx = canvas.x * zoom - hScroll;
                double pcy = canvas.y * zoom - vScroll;

                cr->save();
                double pcw = canvas.width * zoom;
                double pch = canvas.height * zoom;
                _drawRoundedRect(cr, pcx, pcy, pcw, pch, canvas.cornerRadius * zoom);
                cr->clip();

                double r, g, b;
                parseColor(_currentColor, r, g, b);
                cr->set_source_rgba(r, g, b, _currentOpacity);
                cr->set_line_width(_currentLineWidth * zoom);
                cr->set_line_cap(Cairo::LINE_CAP_ROUND);
                std::vector<double> dashes{6.0, 4.0};
                cr->set_dash(dashes, 0.0);

                double sx = pcx + _previewStart.x * zoom;
                double sy = pcy + _previewStart.y * zoom;
                double ex = pcx + _previewEnd.x * zoom;
                double ey = pcy + _previewEnd.y * zoom;

                if (_currentTool == CtDrawingTool::Line) {
                    cr->move_to(sx, sy);
                    cr->line_to(ex, ey);
                    cr->stroke();
                    if (_currentArrowHead == CtDrawingArrowHead::End || _currentArrowHead == CtDrawingArrowHead::Both) {
                        _drawArrowHead(cr, ex, ey, sx, sy, _currentLineWidth, zoom, _currentArrowStyle);
                    }
                    if (_currentArrowHead == CtDrawingArrowHead::Start || _currentArrowHead == CtDrawingArrowHead::Both) {
                        _drawArrowHead(cr, sx, sy, ex, ey, _currentLineWidth, zoom, _currentArrowStyle);
                    }
                } else if (_currentTool == CtDrawingTool::Shape) {
                    double rx = std::min(sx, ex);
                    double ry = std::min(sy, ey);
                    double rw = std::abs(ex - sx);
                    double rh = std::abs(ey - sy);
                    if (_currentShapeType == CtDrawingElementType::Ellipse) {
                        if (rw > 0 && rh > 0) {
                            cr->save();
                            cr->translate(rx + rw / 2.0, ry + rh / 2.0);
                            cr->scale(rw / 2.0, rh / 2.0);
                            cr->arc(0, 0, 1.0, 0, 2.0 * M_PI);
                            cr->restore();
                            cr->stroke();
                        }
                    } else if (_currentShapeType == CtDrawingElementType::Triangle) {
                        cr->move_to(rx + rw / 2.0, ry);
                        cr->line_to(rx, ry + rh);
                        cr->line_to(rx + rw, ry + rh);
                        cr->close_path();
                        cr->stroke();
                    } else if (_currentShapeType == CtDrawingElementType::Diamond) {
                        cr->move_to(rx + rw / 2.0, ry);
                        cr->line_to(rx + rw, ry + rh / 2.0);
                        cr->line_to(rx + rw / 2.0, ry + rh);
                        cr->line_to(rx, ry + rh / 2.0);
                        cr->close_path();
                        cr->stroke();
                    } else if (_currentShapeType == CtDrawingElementType::RoundedRectangle) {
                        double rr = std::min(std::min(rw, rh) * 0.25, 12.0 * zoom);
                        _drawRoundedRect(cr, rx, ry, rw, rh, rr);
                        cr->stroke();
                    } else {
                        cr->rectangle(rx, ry, rw, rh);
                        cr->stroke();
                    }
                }

                cr->unset_dash();
                cr->restore();
            }
        }
    }

    // draw polyline in-progress preview
    if (_polylineActive && _selectedCanvasIdx >= 0 && !_polylinePoints.empty()) {
        auto nodeModel3 = docModel->getNodeById(treeIter.get_node_id());
        if (nodeModel3) {
            const auto& pCanvas = nodeModel3->getDrawingCanvases();
            if (static_cast<size_t>(_selectedCanvasIdx) < pCanvas.size()) {
                const auto& canvas = pCanvas[_selectedCanvasIdx];
                double pcx = canvas.x * zoom - hScroll;
                double pcy = canvas.y * zoom - vScroll;

                cr->save();
                double pcw = canvas.width * zoom;
                double pch = canvas.height * zoom;
                _drawRoundedRect(cr, pcx, pcy, pcw, pch, canvas.cornerRadius * zoom);
                cr->clip();

                double r, g, b;
                parseColor(_currentColor, r, g, b);
                cr->set_source_rgba(r, g, b, _currentOpacity);
                cr->set_line_width(_currentLineWidth * zoom);
                cr->set_line_cap(Cairo::LINE_CAP_ROUND);
                cr->set_line_join(Cairo::LINE_JOIN_ROUND);

                // committed segments (solid)
                if (_polylinePoints.size() >= 2) {
                    cr->move_to(pcx + _polylinePoints[0].x * zoom,
                                pcy + _polylinePoints[0].y * zoom);
                    for (size_t pi = 1; pi < _polylinePoints.size(); ++pi) {
                        cr->line_to(pcx + _polylinePoints[pi].x * zoom,
                                    pcy + _polylinePoints[pi].y * zoom);
                    }
                    cr->stroke();
                }

                // dashed segment from last point to current mouse
                if (_previewActive) {
                    std::vector<double> dashes{6.0, 4.0};
                    cr->set_dash(dashes, 0.0);
                    cr->move_to(pcx + _polylinePoints.back().x * zoom,
                                pcy + _polylinePoints.back().y * zoom);
                    cr->line_to(pcx + _previewEnd.x * zoom,
                                pcy + _previewEnd.y * zoom);
                    cr->stroke();
                    cr->unset_dash();
                }

                cr->restore();
            }
        }
    }

    // draw bezier curve in-progress preview
    if (_bezierActive && _selectedCanvasIdx >= 0 && !_bezierPoints.empty()) {
        auto nodeModel4 = docModel->getNodeById(treeIter.get_node_id());
        if (nodeModel4) {
            const auto& bCanvas = nodeModel4->getDrawingCanvases();
            if (static_cast<size_t>(_selectedCanvasIdx) < bCanvas.size()) {
                const auto& canvas = bCanvas[_selectedCanvasIdx];
                double pcx = canvas.x * zoom - hScroll;
                double pcy = canvas.y * zoom - vScroll;

                cr->save();
                double pcw = canvas.width * zoom;
                double pch = canvas.height * zoom;
                _drawRoundedRect(cr, pcx, pcy, pcw, pch, canvas.cornerRadius * zoom);
                cr->clip();

                double r, g, b;
                parseColor(_currentColor, r, g, b);
                cr->set_source_rgba(r, g, b, _currentOpacity);
                cr->set_line_width(_currentLineWidth * zoom);
                cr->set_line_cap(Cairo::LINE_CAP_ROUND);

                double p0x = pcx + _bezierPoints[0].x * zoom;
                double p0y = pcy + _bezierPoints[0].y * zoom;
                double mx = pcx + _previewEnd.x * zoom;
                double my = pcy + _previewEnd.y * zoom;

                if (_bezierPhase == 1) {
                    std::vector<double> dashes{6.0, 4.0};
                    cr->set_dash(dashes, 0.0);
                    cr->move_to(p0x, p0y);
                    cr->line_to(mx, my);
                    cr->stroke();
                    cr->unset_dash();
                } else if (_bezierPhase == 2) {
                    double p3x = pcx + _bezierPoints[1].x * zoom;
                    double p3y = pcy + _bezierPoints[1].y * zoom;
                    // default CP2: 1/3 from P3 toward P0 (straight arrival at end)
                    double c2x = p3x + (p0x - p3x) / 3.0;
                    double c2y = p3y + (p0y - p3y) / 3.0;
                    cr->move_to(p0x, p0y);
                    cr->curve_to(mx, my, c2x, c2y, p3x, p3y);
                    cr->stroke();
                    // handle line from P0 to CP1
                    std::vector<double> dashes{4.0, 3.0};
                    cr->set_dash(dashes, 0.0);
                    cr->set_line_width(1.0);
                    cr->move_to(p0x, p0y);
                    cr->line_to(mx, my);
                    cr->stroke();
                    cr->unset_dash();
                    cr->arc(mx, my, 3.0, 0, 2.0 * M_PI);
                    cr->fill();
                } else if (_bezierPhase == 3) {
                    double p3x = pcx + _bezierPoints[1].x * zoom;
                    double p3y = pcy + _bezierPoints[1].y * zoom;
                    double c1x = pcx + _bezierPoints[2].x * zoom;
                    double c1y = pcy + _bezierPoints[2].y * zoom;
                    cr->move_to(p0x, p0y);
                    cr->curve_to(c1x, c1y, mx, my, p3x, p3y);
                    cr->stroke();
                    // handle lines
                    std::vector<double> dashes{4.0, 3.0};
                    cr->set_dash(dashes, 0.0);
                    cr->set_line_width(1.0);
                    cr->move_to(p0x, p0y);
                    cr->line_to(c1x, c1y);
                    cr->stroke();
                    cr->move_to(p3x, p3y);
                    cr->line_to(mx, my);
                    cr->stroke();
                    cr->unset_dash();
                    cr->arc(c1x, c1y, 3.0, 0, 2.0 * M_PI);
                    cr->fill();
                    cr->arc(mx, my, 3.0, 0, 2.0 * M_PI);
                    cr->fill();
                }

                cr->restore();
            }
        }
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
        _drawStroke(cr, stroke, cx, cy, zoom);
    }

    // draw selection highlights for multi-selected strokes
    if ((_currentTool == CtDrawingTool::Select || _currentTool == CtDrawingTool::Scale)
        && idx == _selectedCanvasIdx)
    {
        for (int si : _selectedStrokeIndices) {
            if (static_cast<size_t>(si) < canvas.strokes.size()) {
                _drawSelectionHighlight(cr, canvas.strokes[si], cx, cy, zoom);
            }
        }
    }

    // draw scale handles for Scale tool
    if (_currentTool == CtDrawingTool::Scale && idx == _selectedCanvasIdx
        && !_selectedStrokeIndices.empty())
    {
        double bMinX = 1e9, bMinY = 1e9, bMaxX = -1e9, bMaxY = -1e9;
        for (int si : _selectedStrokeIndices) {
            if (static_cast<size_t>(si) < canvas.strokes.size()) {
                double sMinX, sMinY, sMaxX, sMaxY;
                _strokeBoundingBox(canvas.strokes[si], sMinX, sMinY, sMaxX, sMaxY);
                bMinX = std::min(bMinX, sMinX);
                bMinY = std::min(bMinY, sMinY);
                bMaxX = std::max(bMaxX, sMaxX);
                bMaxY = std::max(bMaxY, sMaxY);
            }
        }
        constexpr double hs = 5.0;
        double corners[][2] = {
            {cx + bMinX * zoom, cy + bMinY * zoom},
            {cx + bMaxX * zoom, cy + bMinY * zoom},
            {cx + bMinX * zoom, cy + bMaxY * zoom},
            {cx + bMaxX * zoom, cy + bMaxY * zoom}
        };
        cr->set_source_rgba(0.2, 0.5, 1.0, 0.9);
        cr->set_line_width(1.5);
        for (int h = 0; h < 4; ++h) {
            cr->rectangle(corners[h][0] - hs, corners[h][1] - hs, hs * 2, hs * 2);
            cr->fill_preserve();
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.9);
            cr->stroke();
            cr->set_source_rgba(0.2, 0.5, 1.0, 0.9);
        }
    }

    // draw handles for selected Bezier stroke
    if (_currentTool == CtDrawingTool::Select &&
        _selectStrokeIdx >= 0 &&
        idx == _selectedCanvasIdx &&
        static_cast<size_t>(_selectStrokeIdx) < canvas.strokes.size())
    {
        const auto& sel = canvas.strokes[_selectStrokeIdx];
        if (sel.type == CtDrawingElementType::BezierCurve && sel.points.size() >= 4) {
            double p0x = cx + sel.points[0].x * zoom;
            double p0y = cy + sel.points[0].y * zoom;
            double c1x = cx + sel.points[1].x * zoom;
            double c1y = cy + sel.points[1].y * zoom;
            double c2x = cx + sel.points[2].x * zoom;
            double c2y = cy + sel.points[2].y * zoom;
            double p3x = cx + sel.points[3].x * zoom;
            double p3y = cy + sel.points[3].y * zoom;

            // tangent lines (dashed)
            cr->set_source_rgba(0.3, 0.3, 0.8, 0.7);
            cr->set_line_width(1.0);
            std::vector<double> handleDashes{4.0, 3.0};
            cr->set_dash(handleDashes, 0.0);
            cr->move_to(p0x, p0y);
            cr->line_to(c1x, c1y);
            cr->stroke();
            cr->move_to(p3x, p3y);
            cr->line_to(c2x, c2y);
            cr->stroke();
            cr->unset_dash();

            constexpr double handleR = 5.0;

            // control points CP1, CP2 — filled circles
            cr->set_source_rgba(0.2, 0.4, 0.9, 0.85);
            cr->arc(c1x, c1y, handleR, 0, 2 * M_PI);
            cr->fill();
            cr->arc(c2x, c2y, handleR, 0, 2 * M_PI);
            cr->fill();

            // endpoints P0, P3 — hollow circles
            cr->set_source_rgba(0.2, 0.4, 0.9, 0.85);
            cr->set_line_width(1.5);
            cr->arc(p0x, p0y, handleR, 0, 2 * M_PI);
            cr->stroke();
            cr->arc(p3x, p3y, handleR, 0, 2 * M_PI);
            cr->stroke();
        }
    }

    cr->restore();
}

void CtDrawingOverlay::_strokeCenter(const CtDrawingStroke& stroke, double& centerX, double& centerY)
{
    if (stroke.points.empty()) { centerX = centerY = 0.0; return; }
    if (stroke.type == CtDrawingElementType::Text && !stroke.textContent.empty()) {
        auto layout = _drawingArea.create_pango_layout(stroke.textContent);
        Pango::FontDescription fd;
        fd.set_family(stroke.fontFamily);
        fd.set_size(static_cast<int>(stroke.fontSize * Pango::SCALE));
        layout->set_font_description(fd);
        int pw, ph;
        layout->get_pixel_size(pw, ph);
        centerX = stroke.points[0].x + pw / 2.0;
        centerY = stroke.points[0].y + ph / 2.0;
        return;
    }
    if (stroke.points.size() == 1) {
        centerX = stroke.points[0].x;
        centerY = stroke.points[0].y;
        return;
    }
    if (stroke.type == CtDrawingElementType::Freehand || stroke.type == CtDrawingElementType::Polyline || stroke.type == CtDrawingElementType::BezierCurve) {
        centerX = centerY = 0.0;
        for (const auto& p : stroke.points) {
            centerX += p.x;
            centerY += p.y;
        }
        centerX /= stroke.points.size();
        centerY /= stroke.points.size();
    } else {
        centerX = (stroke.points[0].x + stroke.points[1].x) / 2.0;
        centerY = (stroke.points[0].y + stroke.points[1].y) / 2.0;
    }
}

void CtDrawingOverlay::_drawStroke(const Cairo::RefPtr<Cairo::Context>& cr,
                                    const CtDrawingStroke& stroke,
                                    double cx, double cy, double zoom)
{
    double r, g, b;
    parseColor(stroke.color, r, g, b);
    cr->set_source_rgba(r, g, b, stroke.opacity);
    cr->set_line_width(stroke.lineWidth * zoom);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);

    applyLineStyle(cr, stroke.lineStyle, stroke.lineWidth * zoom);

    if (std::abs(stroke.rotation) > 1e-6) {
        double scx, scy;
        _strokeCenter(stroke, scx, scy);
        double pivotX = cx + scx * zoom;
        double pivotY = cy + scy * zoom;
        cr->save();
        cr->translate(pivotX, pivotY);
        cr->rotate(stroke.rotation);
        cr->translate(-pivotX, -pivotY);
    }

    switch (stroke.type) {
    case CtDrawingElementType::Text: {
        cr->unset_dash();
        if (stroke.points.empty() || stroke.textContent.empty()) break;
        auto layout = Pango::Layout::create(cr);
        layout->set_text(stroke.textContent);
        Pango::FontDescription fd;
        fd.set_family(stroke.fontFamily);
        fd.set_size(static_cast<int>(stroke.fontSize * zoom * Pango::SCALE));
        layout->set_font_description(fd);
        cr->move_to(cx + stroke.points[0].x * zoom, cy + stroke.points[0].y * zoom);
        layout->show_in_cairo_context(cr);
        break;
    }
    case CtDrawingElementType::Line: {
        if (stroke.points.size() < 2) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double x1 = cx + stroke.points[1].x * zoom;
        double y1 = cy + stroke.points[1].y * zoom;
        cr->move_to(x0, y0);
        cr->line_to(x1, y1);
        cr->stroke();
        if (stroke.arrowHead == CtDrawingArrowHead::End || stroke.arrowHead == CtDrawingArrowHead::Both) {
            _drawArrowHead(cr, x1, y1, x0, y0, stroke.lineWidth, zoom, stroke.arrowStyle);
        }
        if (stroke.arrowHead == CtDrawingArrowHead::Start || stroke.arrowHead == CtDrawingArrowHead::Both) {
            _drawArrowHead(cr, x0, y0, x1, y1, stroke.lineWidth, zoom, stroke.arrowStyle);
        }
        break;
    }
    case CtDrawingElementType::Polyline: {
        if (stroke.points.size() < 2) break;
        cr->move_to(cx + stroke.points[0].x * zoom, cy + stroke.points[0].y * zoom);
        for (size_t j = 1; j < stroke.points.size(); ++j) {
            cr->line_to(cx + stroke.points[j].x * zoom, cy + stroke.points[j].y * zoom);
        }
        cr->stroke();
        if (stroke.arrowHead == CtDrawingArrowHead::End || stroke.arrowHead == CtDrawingArrowHead::Both) {
            size_t last = stroke.points.size() - 1;
            _drawArrowHead(cr, cx + stroke.points[last].x * zoom, cy + stroke.points[last].y * zoom,
                           cx + stroke.points[last - 1].x * zoom, cy + stroke.points[last - 1].y * zoom,
                           stroke.lineWidth, zoom, stroke.arrowStyle);
        }
        if (stroke.arrowHead == CtDrawingArrowHead::Start || stroke.arrowHead == CtDrawingArrowHead::Both) {
            _drawArrowHead(cr, cx + stroke.points[0].x * zoom, cy + stroke.points[0].y * zoom,
                           cx + stroke.points[1].x * zoom, cy + stroke.points[1].y * zoom,
                           stroke.lineWidth, zoom, stroke.arrowStyle);
        }
        break;
    }
    case CtDrawingElementType::BezierCurve: {
        if (stroke.points.size() < 4) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double c1x = cx + stroke.points[1].x * zoom;
        double c1y = cy + stroke.points[1].y * zoom;
        double c2x = cx + stroke.points[2].x * zoom;
        double c2y = cy + stroke.points[2].y * zoom;
        double x3 = cx + stroke.points[3].x * zoom;
        double y3 = cy + stroke.points[3].y * zoom;
        cr->move_to(x0, y0);
        cr->curve_to(c1x, c1y, c2x, c2y, x3, y3);
        cr->stroke();
        if (stroke.arrowHead == CtDrawingArrowHead::End || stroke.arrowHead == CtDrawingArrowHead::Both) {
            _drawArrowHead(cr, x3, y3, c2x, c2y, stroke.lineWidth, zoom, stroke.arrowStyle);
        }
        if (stroke.arrowHead == CtDrawingArrowHead::Start || stroke.arrowHead == CtDrawingArrowHead::Both) {
            _drawArrowHead(cr, x0, y0, c1x, c1y, stroke.lineWidth, zoom, stroke.arrowStyle);
        }
        break;
    }
    case CtDrawingElementType::Rectangle: {
        if (stroke.points.size() < 2) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double x1 = cx + stroke.points[1].x * zoom;
        double y1 = cy + stroke.points[1].y * zoom;
        double rx = std::min(x0, x1), ry = std::min(y0, y1);
        double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
        cr->rectangle(rx, ry, rw, rh);
        if (stroke.filled) { cr->fill_preserve(); }
        cr->stroke();
        break;
    }
    case CtDrawingElementType::RoundedRectangle: {
        if (stroke.points.size() < 2) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double x1 = cx + stroke.points[1].x * zoom;
        double y1 = cy + stroke.points[1].y * zoom;
        double rx = std::min(x0, x1), ry = std::min(y0, y1);
        double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
        double rr = std::min(std::min(rw, rh) * 0.25, 12.0 * zoom);
        _drawRoundedRect(cr, rx, ry, rw, rh, rr);
        if (stroke.filled) { cr->fill_preserve(); }
        cr->stroke();
        break;
    }
    case CtDrawingElementType::Ellipse: {
        if (stroke.points.size() < 2) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double x1 = cx + stroke.points[1].x * zoom;
        double y1 = cy + stroke.points[1].y * zoom;
        double ex = std::min(x0, x1), ey = std::min(y0, y1);
        double ew = std::abs(x1 - x0), eh = std::abs(y1 - y0);
        if (ew > 0 && eh > 0) {
            cr->save();
            cr->translate(ex + ew / 2.0, ey + eh / 2.0);
            cr->scale(ew / 2.0, eh / 2.0);
            cr->arc(0, 0, 1.0, 0, 2.0 * M_PI);
            cr->restore();
            if (stroke.filled) { cr->fill_preserve(); }
            cr->stroke();
        }
        break;
    }
    case CtDrawingElementType::Triangle: {
        if (stroke.points.size() < 2) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double x1 = cx + stroke.points[1].x * zoom;
        double y1 = cy + stroke.points[1].y * zoom;
        double rx = std::min(x0, x1), ry = std::min(y0, y1);
        double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
        cr->move_to(rx + rw / 2.0, ry);
        cr->line_to(rx, ry + rh);
        cr->line_to(rx + rw, ry + rh);
        cr->close_path();
        if (stroke.filled) { cr->fill_preserve(); }
        cr->stroke();
        break;
    }
    case CtDrawingElementType::Diamond: {
        if (stroke.points.size() < 2) break;
        double x0 = cx + stroke.points[0].x * zoom;
        double y0 = cy + stroke.points[0].y * zoom;
        double x1 = cx + stroke.points[1].x * zoom;
        double y1 = cy + stroke.points[1].y * zoom;
        double rx = std::min(x0, x1), ry = std::min(y0, y1);
        double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
        cr->move_to(rx + rw / 2.0, ry);
        cr->line_to(rx + rw, ry + rh / 2.0);
        cr->line_to(rx + rw / 2.0, ry + rh);
        cr->line_to(rx, ry + rh / 2.0);
        cr->close_path();
        if (stroke.filled) { cr->fill_preserve(); }
        cr->stroke();
        break;
    }
    case CtDrawingElementType::Freehand:
    default: {
        if (stroke.points.size() < 2) break;
        cr->move_to(cx + stroke.points[0].x * zoom, cy + stroke.points[0].y * zoom);
        for (size_t j = 1; j < stroke.points.size(); ++j) {
            cr->line_to(cx + stroke.points[j].x * zoom, cy + stroke.points[j].y * zoom);
        }
        cr->stroke();
        break;
    }
    }
    cr->unset_dash();

    if (std::abs(stroke.rotation) > 1e-6) {
        cr->restore();
    }
}

void CtDrawingOverlay::_drawArrowHead(const Cairo::RefPtr<Cairo::Context>& cr,
                                       double tipX, double tipY,
                                       double fromX, double fromY,
                                       double lineWidth, double zoom,
                                       CtDrawingArrowStyle style)
{
    double dx = tipX - fromX;
    double dy = tipY - fromY;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    dx /= len;
    dy /= len;

    double arrowLen = std::max(10.0, lineWidth * zoom * 4.0);
    double arrowHalfW = arrowLen * 0.4;
    double bx = tipX - dx * arrowLen;
    double by = tipY - dy * arrowLen;
    double px = -dy, py = dx;
    double lx = bx + px * arrowHalfW, ly = by + py * arrowHalfW;
    double rx = bx - px * arrowHalfW, ry = by - py * arrowHalfW;

    cr->unset_dash();
    if (style == CtDrawingArrowStyle::Solid) {
        cr->move_to(tipX, tipY);
        cr->line_to(lx, ly);
        cr->line_to(rx, ry);
        cr->close_path();
        cr->fill();
    } else {
        cr->set_line_cap(Cairo::LINE_CAP_ROUND);
        cr->set_line_join(Cairo::LINE_JOIN_ROUND);
        cr->move_to(lx, ly);
        cr->line_to(tipX, tipY);
        cr->line_to(rx, ry);
        cr->stroke();
    }
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
        const auto& stroke = canvas.strokes[si];
        const auto& pts = stroke.points;
        double dist = threshold + 1.0;

        double lmx = mx, lmy = my;
        if (std::abs(stroke.rotation) > 1e-6) {
            double scx, scy;
            _strokeCenter(stroke, scx, scy);
            double pivotX = cx + scx * zoom;
            double pivotY = cy + scy * zoom;
            double cosA = std::cos(-stroke.rotation);
            double sinA = std::sin(-stroke.rotation);
            double rdx = mx - pivotX, rdy = my - pivotY;
            lmx = pivotX + rdx * cosA - rdy * sinA;
            lmy = pivotY + rdx * sinA + rdy * cosA;
        }
        // use lmx/lmy (inverse-rotated mouse) for all hit tests below
        #define mx lmx
        #define my lmy

        switch (stroke.type) {
        case CtDrawingElementType::Text: {
            if (pts.empty() || stroke.textContent.empty()) continue;
            auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 1, 1);
            auto tmpCr = Cairo::Context::create(surface);
            auto layout = Pango::Layout::create(tmpCr);
            layout->set_text(stroke.textContent);
            Pango::FontDescription fd;
            fd.set_family(stroke.fontFamily);
            fd.set_size(static_cast<int>(stroke.fontSize * zoom * Pango::SCALE));
            layout->set_font_description(fd);
            int tw, th;
            layout->get_pixel_size(tw, th);
            double tx = cx + pts[0].x * zoom;
            double ty = cy + pts[0].y * zoom;
            if (mx >= tx && mx <= tx + tw && my >= ty && my <= ty + th) {
                dist = 0.0;
            }
            break;
        }
        case CtDrawingElementType::Line: {
            if (pts.size() < 2) continue;
            dist = _distPointToSegment(mx, my,
                cx + pts[0].x * zoom, cy + pts[0].y * zoom,
                cx + pts[1].x * zoom, cy + pts[1].y * zoom);
            break;
        }
        case CtDrawingElementType::RoundedRectangle:
        case CtDrawingElementType::Rectangle: {
            if (pts.size() < 2) continue;
            double x0 = cx + pts[0].x * zoom, y0 = cy + pts[0].y * zoom;
            double x1 = cx + pts[1].x * zoom, y1 = cy + pts[1].y * zoom;
            double rx = std::min(x0, x1), ry = std::min(y0, y1);
            double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
            if (stroke.filled && mx >= rx && mx <= rx + rw && my >= ry && my <= ry + rh) {
                dist = 0.0;
            } else {
                double d1 = _distPointToSegment(mx, my, rx, ry, rx + rw, ry);
                double d2 = _distPointToSegment(mx, my, rx + rw, ry, rx + rw, ry + rh);
                double d3 = _distPointToSegment(mx, my, rx + rw, ry + rh, rx, ry + rh);
                double d4 = _distPointToSegment(mx, my, rx, ry + rh, rx, ry);
                dist = std::min({d1, d2, d3, d4});
            }
            break;
        }
        case CtDrawingElementType::Ellipse: {
            if (pts.size() < 2) continue;
            double x0 = cx + pts[0].x * zoom, y0 = cy + pts[0].y * zoom;
            double x1 = cx + pts[1].x * zoom, y1 = cy + pts[1].y * zoom;
            double ecx = (x0 + x1) / 2.0, ecy = (y0 + y1) / 2.0;
            double a = std::abs(x1 - x0) / 2.0, b = std::abs(y1 - y0) / 2.0;
            if (a > 0 && b > 0) {
                double nx = (mx - ecx) / a, ny = (my - ecy) / b;
                double ellipseR = nx * nx + ny * ny;
                if (stroke.filled && ellipseR <= 1.0) {
                    dist = 0.0;
                } else {
                    double boundaryDist = std::abs(std::sqrt(ellipseR) - 1.0) * std::min(a, b);
                    dist = boundaryDist;
                }
            }
            break;
        }
        case CtDrawingElementType::Polyline: {
            for (size_t j = 1; j < pts.size(); ++j) {
                double d = _distPointToSegment(mx, my,
                    cx + pts[j-1].x * zoom, cy + pts[j-1].y * zoom,
                    cx + pts[j].x * zoom, cy + pts[j].y * zoom);
                if (d < dist) dist = d;
            }
            break;
        }
        case CtDrawingElementType::BezierCurve: {
            if (pts.size() < 4) continue;
            double bx0 = cx + pts[0].x * zoom, by0 = cy + pts[0].y * zoom;
            double bc1x = cx + pts[1].x * zoom, bc1y = cy + pts[1].y * zoom;
            double bc2x = cx + pts[2].x * zoom, bc2y = cy + pts[2].y * zoom;
            double bx3 = cx + pts[3].x * zoom, by3 = cy + pts[3].y * zoom;
            constexpr int N = 20;
            double prevX = bx0, prevY = by0;
            for (int k = 1; k <= N; ++k) {
                double t = static_cast<double>(k) / N;
                double u = 1.0 - t;
                double sx = u*u*u*bx0 + 3*u*u*t*bc1x + 3*u*t*t*bc2x + t*t*t*bx3;
                double sy = u*u*u*by0 + 3*u*u*t*bc1y + 3*u*t*t*bc2y + t*t*t*by3;
                double d = _distPointToSegment(mx, my, prevX, prevY, sx, sy);
                if (d < dist) dist = d;
                prevX = sx;
                prevY = sy;
            }
            break;
        }
        case CtDrawingElementType::Triangle: {
            if (pts.size() < 2) continue;
            double x0 = cx + pts[0].x * zoom, y0 = cy + pts[0].y * zoom;
            double x1 = cx + pts[1].x * zoom, y1 = cy + pts[1].y * zoom;
            double rx = std::min(x0, x1), ry = std::min(y0, y1);
            double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
            double tx = rx + rw / 2.0, ty = ry;
            double bx1 = rx, by1 = ry + rh;
            double bx2 = rx + rw, by2 = ry + rh;
            if (stroke.filled) {
                double d1 = (mx - bx1) * (ty - by1) - (tx - bx1) * (my - by1);
                double d2 = (mx - bx2) * (by1 - by2) - (bx1 - bx2) * (my - by2);
                double d3 = (mx - tx) * (by2 - ty) - (bx2 - tx) * (my - ty);
                bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
                bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
                if (!(hasNeg && hasPos)) dist = 0.0;
            }
            if (dist > 0.0) {
                double da = _distPointToSegment(mx, my, tx, ty, bx1, by1);
                double db = _distPointToSegment(mx, my, bx1, by1, bx2, by2);
                double dc = _distPointToSegment(mx, my, bx2, by2, tx, ty);
                dist = std::min({da, db, dc});
            }
            break;
        }
        case CtDrawingElementType::Diamond: {
            if (pts.size() < 2) continue;
            double x0 = cx + pts[0].x * zoom, y0 = cy + pts[0].y * zoom;
            double x1 = cx + pts[1].x * zoom, y1 = cy + pts[1].y * zoom;
            double rx = std::min(x0, x1), ry = std::min(y0, y1);
            double rw = std::abs(x1 - x0), rh = std::abs(y1 - y0);
            double dtop_x = rx + rw / 2.0, dtop_y = ry;
            double dright_x = rx + rw, dright_y = ry + rh / 2.0;
            double dbottom_x = rx + rw / 2.0, dbottom_y = ry + rh;
            double dleft_x = rx, dleft_y = ry + rh / 2.0;
            if (stroke.filled) {
                double hw = rw / 2.0, hh = rh / 2.0;
                double dcx = rx + hw, dcy = ry + hh;
                if (hw > 0 && hh > 0) {
                    double nx = std::abs(mx - dcx) / hw;
                    double ny = std::abs(my - dcy) / hh;
                    if (nx + ny <= 1.0) dist = 0.0;
                }
            }
            if (dist > 0.0) {
                double da = _distPointToSegment(mx, my, dtop_x, dtop_y, dright_x, dright_y);
                double db = _distPointToSegment(mx, my, dright_x, dright_y, dbottom_x, dbottom_y);
                double dc = _distPointToSegment(mx, my, dbottom_x, dbottom_y, dleft_x, dleft_y);
                double dd = _distPointToSegment(mx, my, dleft_x, dleft_y, dtop_x, dtop_y);
                dist = std::min({da, db, dc, dd});
            }
            break;
        }
        case CtDrawingElementType::Freehand:
        default: {
            for (size_t j = 1; j < pts.size(); ++j) {
                double d = _distPointToSegment(mx, my,
                    cx + pts[j-1].x * zoom, cy + pts[j-1].y * zoom,
                    cx + pts[j].x * zoom, cy + pts[j].y * zoom);
                if (d < dist) dist = d;
            }
            break;
        }
        }

        #undef mx
        #undef my

        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = static_cast<int>(si);
        }
    }

    return bestDist <= threshold ? bestIdx : -1;
}

bool CtDrawingOverlay::_onButtonPress(GdkEventButton* event)
{
    if (!_drawingMode) return false;

    if (_toolbarVisible && _pToolbarEventBox && _pToolbarEventBox->get_visible()) {
        auto alloc = _pToolbarEventBox->get_allocation();
        if (event->x >= alloc.get_x() && event->x <= alloc.get_x() + alloc.get_width() &&
            event->y >= alloc.get_y() && event->y <= alloc.get_y() + alloc.get_height()) {
            return true;
        }
    }

    auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
    auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
    double hScroll = hAdj ? hAdj->get_value() : 0.0;
    double vScroll = vAdj ? vAdj->get_value() : 0.0;
    double zoom = _pMainWin->get_rt_zoom_scale_factor();

    if (event->button == 3) {
        int ci = _hitTestCanvas(event->x, event->y, hScroll, vScroll, zoom);
        if (ci >= 0) {
            _selectedCanvasIdx = ci;
            auto* bridge2 = _pMainWin->get_command_bridge();
            if (bridge2 && bridge2->isActive()) {
                auto nm = bridge2->getDocumentModel()->getNodeById(_pMainWin->curr_tree_iter().get_node_id());
                if (nm && static_cast<size_t>(ci) < nm->getDrawingCanvases().size()) {
                    double csx = nm->getDrawingCanvases()[ci].x * zoom - hScroll;
                    double csy = nm->getDrawingCanvases()[ci].y * zoom - vScroll;
                    _lastClickCanvasX = (event->x - csx) / zoom;
                    _lastClickCanvasY = (event->y - csy) / zoom;
                }
            }
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
        _hideToolbar();
        return true;
    }

    if (_selectedCanvasIdx != ci) {
        _selectedCanvasIdx = ci;
        _showToolbar();
    } else {
        _selectedCanvasIdx = ci;
    }
    _drawingArea.grab_focus();

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
        if (_currentTool == CtDrawingTool::Rotate) {
            int si = _hitTestStroke(event->x, event->y, canvas, hScroll, vScroll, zoom);
            if (si >= 0) {
                _rotateStrokeIdx = si;
                _rotateOrigRotation = canvas.strokes[si].rotation;
                double scx, scy;
                _strokeCenter(canvas.strokes[si], scx, scy);
                double canvasScreenX = canvas.x * zoom - hScroll;
                double canvasScreenY = canvas.y * zoom - vScroll;
                double emx = (event->x - canvasScreenX) / zoom;
                double emy = (event->y - canvasScreenY) / zoom;
                _rotateStartAngle = std::atan2(emy - scy, emx - scx);
                _dragType = CtDrawingDragType::RotateStroke;
            }
            return true;
        }

        if (_currentTool == CtDrawingTool::Select) {
            double canvasScreenX = canvas.x * zoom - hScroll;
            double canvasScreenY = canvas.y * zoom - vScroll;
            double mx = (event->x - canvasScreenX) / zoom;
            double my = (event->y - canvasScreenY) / zoom;
            _lastClickCanvasX = mx;
            _lastClickCanvasY = my;
            bool ctrlHeld = (event->state & GDK_CONTROL_MASK) != 0;

            // check Bezier handle hit first (when exactly one BezierCurve selected)
            if (_selectStrokeIdx >= 0 &&
                static_cast<size_t>(_selectStrokeIdx) < canvas.strokes.size())
            {
                const auto& sel = canvas.strokes[_selectStrokeIdx];
                if (sel.type == CtDrawingElementType::BezierCurve && sel.points.size() >= 4) {
                    constexpr double hitR = 8.0;
                    for (int pi = 0; pi < 4; ++pi) {
                        double ddx = mx - sel.points[pi].x;
                        double ddy = my - sel.points[pi].y;
                        if (ddx * ddx + ddy * ddy <= hitR * hitR) {
                            _selectOrigPoints = sel.points;
                            _selectDragPointIdx = pi;
                            _dragType = CtDrawingDragType::EditPoint;
                            return true;
                        }
                    }
                }
            }

            // hit-test strokes
            int si = _hitTestStroke(event->x, event->y, canvas, hScroll, vScroll, zoom);

            if (si >= 0) {
                if (ctrlHeld) {
                    // toggle stroke in/out of selection
                    if (_selectedStrokeIndices.count(si)) {
                        _selectedStrokeIndices.erase(si);
                    } else {
                        _selectedStrokeIndices.insert(si);
                    }
                } else if (_selectedStrokeIndices.count(si)) {
                    // clicked on already-selected stroke: start MoveSelection
                    _moveSelOrigPoints.clear();
                    for (int idx : _selectedStrokeIndices) {
                        if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                            _moveSelOrigPoints[idx] = canvas.strokes[idx].points;
                        }
                    }
                    _dragType = CtDrawingDragType::MoveSelection;
                } else {
                    // plain click on stroke: select just this one
                    _selectedStrokeIndices.clear();
                    _selectedStrokeIndices.insert(si);
                }
            } else {
                // clicked empty space
                if (!ctrlHeld) {
                    _selectedStrokeIndices.clear();
                }
                // start rubber-band selection
                _selectBandStartX = event->x;
                _selectBandStartY = event->y;
                _selectBandEndX = event->x;
                _selectBandEndY = event->y;
                _dragType = CtDrawingDragType::SelectBand;
            }

            // show Bezier handles only when exactly one BezierCurve is selected
            if (_selectedStrokeIndices.size() == 1) {
                int onlyIdx = *_selectedStrokeIndices.begin();
                if (static_cast<size_t>(onlyIdx) < canvas.strokes.size() &&
                    canvas.strokes[onlyIdx].type == CtDrawingElementType::BezierCurve) {
                    _selectStrokeIdx = onlyIdx;
                } else {
                    _selectStrokeIdx = -1;
                }
            } else {
                _selectStrokeIdx = -1;
            }
            _selectDragPointIdx = -1;
            _selectOrigPoints.clear();
            _drawingArea.queue_draw();
            return true;
        }

        if (_currentTool == CtDrawingTool::Scale) {
            double canvasScreenX = canvas.x * zoom - hScroll;
            double canvasScreenY = canvas.y * zoom - vScroll;
            double mx = (event->x - canvasScreenX) / zoom;
            double my = (event->y - canvasScreenY) / zoom;

            // if we have a selected stroke, check handle hit first
            if (!_selectedStrokeIndices.empty()) {
                // compute combined bounding box
                double bMinX = 1e9, bMinY = 1e9, bMaxX = -1e9, bMaxY = -1e9;
                for (int idx : _selectedStrokeIndices) {
                    if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                        double sMinX, sMinY, sMaxX, sMaxY;
                        _strokeBoundingBox(canvas.strokes[idx], sMinX, sMinY, sMaxX, sMaxY);
                        bMinX = std::min(bMinX, sMinX);
                        bMinY = std::min(bMinY, sMinY);
                        bMaxX = std::max(bMaxX, sMaxX);
                        bMaxY = std::max(bMaxY, sMaxY);
                    }
                }
                constexpr double handleR = 6.0;
                double corners[][2] = {
                    {bMinX, bMinY}, {bMaxX, bMinY},
                    {bMinX, bMaxY}, {bMaxX, bMaxY}
                };
                for (int h = 0; h < 4; ++h) {
                    double ddx = mx - corners[h][0];
                    double ddy = my - corners[h][1];
                    if (ddx * ddx + ddy * ddy <= handleR * handleR) {
                        _scaleHandleIdx = h;
                        // anchor is the opposite corner
                        _scaleAnchorX = corners[3 - h][0];
                        _scaleAnchorY = corners[3 - h][1];
                        _scaleBBoxMinX = bMinX;
                        _scaleBBoxMinY = bMinY;
                        _scaleBBoxMaxX = bMaxX;
                        _scaleBBoxMaxY = bMaxY;
                        _scaleOrigPoints.clear();
                        for (int idx : _selectedStrokeIndices) {
                            if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                                _scaleOrigPoints[idx] = canvas.strokes[idx].points;
                            }
                        }
                        _dragType = CtDrawingDragType::ScaleSelection;
                        return true;
                    }
                }
            }

            // hit-test strokes for selection
            int si = _hitTestStroke(event->x, event->y, canvas, hScroll, vScroll, zoom);
            if (si >= 0) {
                _selectedStrokeIndices.clear();
                _selectedStrokeIndices.insert(si);
            } else {
                _selectedStrokeIndices.clear();
            }
            _scaleHandleIdx = -1;
            _scaleOrigPoints.clear();
            _drawingArea.queue_draw();
            return true;
        }

        if (_currentTool == CtDrawingTool::Rubber) {
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

        double canvasScreenX = canvas.x * zoom - hScroll;
        double canvasScreenY = canvas.y * zoom - vScroll;
        double px = (event->x - canvasScreenX) / zoom;
        double py = (event->y - canvasScreenY) / zoom;

        if (_currentTool == CtDrawingTool::Line && _currentLineType == CtDrawingElementType::BezierCurve) {
            if (!_bezierActive) {
                _bezierActive = true;
                _bezierPhase = 1;
                _bezierPoints.clear();
                _bezierPoints.push_back({px, py});
                _previewActive = true;
                _previewEnd = {px, py};
            } else if (_bezierPhase == 1) {
                _bezierPoints.push_back({px, py});
                _bezierPhase = 2;
            } else if (_bezierPhase == 2) {
                _bezierPoints.push_back({px, py});
                _bezierPhase = 3;
            } else if (_bezierPhase == 3) {
                _previewActive = false;
                _bezierActive = false;
                CtDrawingPoint cp2 = {px, py};
                CtDrawingStroke newStroke;
                newStroke.color = _currentColor;
                newStroke.lineWidth = _currentLineWidth;
                newStroke.opacity = _currentOpacity;
                newStroke.lineStyle = _currentLineStyle;
                newStroke.type = CtDrawingElementType::BezierCurve;
                newStroke.arrowHead = _currentArrowHead;
                newStroke.arrowStyle = _currentArrowStyle;
                newStroke.points.push_back(_bezierPoints[0]);
                newStroke.points.push_back(_bezierPoints[2]);
                newStroke.points.push_back(cp2);
                newStroke.points.push_back(_bezierPoints[1]);
                auto cmd = std::make_unique<DrawStrokeCommand>(
                    bridge->getDocumentModel(), _pMainWin->curr_tree_iter().get_node_id(), ci,
                    std::move(newStroke));
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
                _bezierPhase = 0;
                _bezierPoints.clear();
            }
            _drawingArea.queue_draw();
            return true;
        } else if (_currentTool == CtDrawingTool::Line && _currentLineType == CtDrawingElementType::Polyline) {
            if (!_polylineActive) {
                _polylineActive = true;
                _polylinePoints.clear();
                _polylinePoints.push_back({px, py});
                _previewActive = true;
                _previewEnd = {px, py};
            } else {
                if (event->type == GDK_2BUTTON_PRESS) {
                    _previewActive = false;
                    _polylineActive = false;
                    while (_polylinePoints.size() >= 2) {
                        auto& a = _polylinePoints[_polylinePoints.size() - 2];
                        auto& b = _polylinePoints[_polylinePoints.size() - 1];
                        if (std::abs(a.x - b.x) < 1.0 && std::abs(a.y - b.y) < 1.0)
                            _polylinePoints.pop_back();
                        else break;
                    }
                    if (_polylinePoints.size() >= 2) {
                        CtDrawingStroke newStroke;
                        newStroke.color = _currentColor;
                        newStroke.lineWidth = _currentLineWidth;
                        newStroke.opacity = _currentOpacity;
                        newStroke.lineStyle = _currentLineStyle;
                        newStroke.type = CtDrawingElementType::Polyline;
                        newStroke.arrowHead = _currentArrowHead;
                        newStroke.arrowStyle = _currentArrowStyle;
                        newStroke.points = _polylinePoints;
                        auto cmd = std::make_unique<DrawStrokeCommand>(
                            bridge->getDocumentModel(), _pMainWin->curr_tree_iter().get_node_id(), ci,
                            std::move(newStroke));
                        bridge->executeCommand(std::move(cmd));
                        _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
                    }
                    _polylinePoints.clear();
                } else {
                    _polylinePoints.push_back({px, py});
                }
            }
            _drawingArea.queue_draw();
            return true;
        } else if (_currentTool == CtDrawingTool::Line || _currentTool == CtDrawingTool::Shape) {
            _previewActive = true;
            _previewStart = {px, py};
            _previewEnd = {px, py};
            _dragType = CtDrawingDragType::Draw;
        } else if (_currentTool == CtDrawingTool::Text) {
            if (event->type == GDK_2BUTTON_PRESS) {
                int si = _hitTestStroke(event->x, event->y, canvas, hScroll, vScroll, zoom);
                if (si >= 0 && canvas.strokes[si].type == CtDrawingElementType::Text) {
                    _showTextDialog(px, py, ci, &canvas.strokes[si], si);
                }
            } else {
                _showTextDialog(px, py, ci);
            }
            return true;
        } else {
            // Pencil — freehand drawing
            _dragType = CtDrawingDragType::Draw;
            auto& canvasMut = nodeModel->getDrawingCanvasesMut()[ci];
            CtDrawingStroke newStroke;
            newStroke.color = _currentColor;
            newStroke.lineWidth = _currentLineWidth;
            newStroke.opacity = _currentOpacity;
            newStroke.lineStyle = _currentLineStyle;
            newStroke.type = CtDrawingElementType::Freehand;
            newStroke.points.push_back({px, py});
            canvasMut.strokes.push_back(std::move(newStroke));
            _drawingArea.queue_draw();
        }
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

    if ((_polylineActive || _bezierActive) && _previewActive && _selectedCanvasIdx >= 0) {
        auto* bridge2 = _pMainWin->get_command_bridge();
        if (bridge2 && bridge2->isActive()) {
            auto nm = bridge2->getDocumentModel()->getNodeById(_pMainWin->curr_tree_iter().get_node_id());
            if (nm) {
                const auto& canvases2 = nm->getDrawingCanvases();
                if (static_cast<size_t>(_selectedCanvasIdx) < canvases2.size()) {
                    double zoom2 = _pMainWin->get_rt_zoom_scale_factor();
                    auto hAdj2 = _pMainWin->getScrolledwindowText().get_hadjustment();
                    auto vAdj2 = _pMainWin->getScrolledwindowText().get_vadjustment();
                    double hS = hAdj2 ? hAdj2->get_value() : 0.0;
                    double vS = vAdj2 ? vAdj2->get_value() : 0.0;
                    double ccx = canvases2[_selectedCanvasIdx].x * zoom2 - hS;
                    double ccy = canvases2[_selectedCanvasIdx].y * zoom2 - vS;
                    _previewEnd.x = (event->x - ccx) / zoom2;
                    _previewEnd.y = (event->y - ccy) / zoom2;
                    _drawingArea.queue_draw();
                }
            }
        }
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
                        case CtDrawingHitZone::Interior:
                            if (_currentTool == CtDrawingTool::Rotate) {
                                const auto& canvases2 = nodeModel->getDrawingCanvases();
                                int si = _hitTestStroke(event->x, event->y, canvases2[ci], hScroll, vScroll, zoom);
                                if (si >= 0) cursorType = Gdk::EXCHANGE;
                            }
                            else if (_currentTool == CtDrawingTool::Select) {
                                const auto& canvases2 = nodeModel->getDrawingCanvases();
                                // check Bezier handle hover
                                if (_selectStrokeIdx >= 0 &&
                                    static_cast<size_t>(_selectStrokeIdx) < canvases2[ci].strokes.size())
                                {
                                    const auto& sel = canvases2[ci].strokes[_selectStrokeIdx];
                                    if (sel.type == CtDrawingElementType::BezierCurve && sel.points.size() >= 4) {
                                        double csx = canvases2[ci].x * zoom - hScroll;
                                        double csy = canvases2[ci].y * zoom - vScroll;
                                        double smx = (event->x - csx) / zoom;
                                        double smy = (event->y - csy) / zoom;
                                        constexpr double hitR = 8.0;
                                        for (int pi = 0; pi < 4; ++pi) {
                                            double ddx = smx - sel.points[pi].x;
                                            double ddy = smy - sel.points[pi].y;
                                            if (ddx * ddx + ddy * ddy <= hitR * hitR) {
                                                cursorType = Gdk::HAND2;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (cursorType == Gdk::ARROW) {
                                    int si = _hitTestStroke(event->x, event->y, canvases2[ci], hScroll, vScroll, zoom);
                                    if (si >= 0) {
                                        if (_selectedStrokeIndices.count(si)) {
                                            cursorType = Gdk::FLEUR;
                                        } else {
                                            cursorType = Gdk::HAND1;
                                        }
                                    }
                                }
                            }
                            else if (_currentTool == CtDrawingTool::Scale) {
                                const auto& canvases2 = nodeModel->getDrawingCanvases();
                                if (!_selectedStrokeIndices.empty()) {
                                    double bMinX = 1e9, bMinY = 1e9, bMaxX = -1e9, bMaxY = -1e9;
                                    for (int si2 : _selectedStrokeIndices) {
                                        if (static_cast<size_t>(si2) < canvases2[ci].strokes.size()) {
                                            double sMinX, sMinY, sMaxX, sMaxY;
                                            _strokeBoundingBox(canvases2[ci].strokes[si2], sMinX, sMinY, sMaxX, sMaxY);
                                            bMinX = std::min(bMinX, sMinX);
                                            bMinY = std::min(bMinY, sMinY);
                                            bMaxX = std::max(bMaxX, sMaxX);
                                            bMaxY = std::max(bMaxY, sMaxY);
                                        }
                                    }
                                    double csx = canvases2[ci].x * zoom - hScroll;
                                    double csy = canvases2[ci].y * zoom - vScroll;
                                    double smx = (event->x - csx) / zoom;
                                    double smy = (event->y - csy) / zoom;
                                    constexpr double handleR = 6.0;
                                    double corners[][2] = {
                                        {bMinX, bMinY}, {bMaxX, bMinY},
                                        {bMinX, bMaxY}, {bMaxX, bMaxY}
                                    };
                                    Gdk::CursorType cornerCursors[] = {
                                        Gdk::TOP_LEFT_CORNER, Gdk::TOP_RIGHT_CORNER,
                                        Gdk::BOTTOM_LEFT_CORNER, Gdk::BOTTOM_RIGHT_CORNER
                                    };
                                    for (int h = 0; h < 4; ++h) {
                                        double ddx = smx - corners[h][0];
                                        double ddy = smy - corners[h][1];
                                        if (ddx * ddx + ddy * ddy <= handleR * handleR) {
                                            cursorType = cornerCursors[h];
                                            break;
                                        }
                                    }
                                }
                                if (cursorType == Gdk::ARROW) {
                                    int si = _hitTestStroke(event->x, event->y, canvases2[ci], hScroll, vScroll, zoom);
                                    if (si >= 0) cursorType = Gdk::HAND1;
                                }
                            }
                            break;
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
        if (_toolbarVisible) _updateToolbarPosition();
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::MoveStroke) {
        if (_moveStrokeIdx >= 0 && static_cast<size_t>(_moveStrokeIdx) < canvas.strokes.size()) {
            auto& pts = canvas.strokes[_moveStrokeIdx].points;
            for (size_t i = 0; i < pts.size(); ++i) {
                pts[i].x = _moveStrokeOrigPoints[i].x + dx;
                pts[i].y = _moveStrokeOrigPoints[i].y + dy;
            }
            _drawingArea.queue_draw();
        }
        return true;
    }

    if (_dragType == CtDrawingDragType::RotateStroke) {
        if (_rotateStrokeIdx >= 0 && static_cast<size_t>(_rotateStrokeIdx) < canvas.strokes.size()) {
            auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
            auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
            double hScroll = hAdj ? hAdj->get_value() : 0.0;
            double vScroll = vAdj ? vAdj->get_value() : 0.0;
            double scx, scy;
            _strokeCenter(canvas.strokes[_rotateStrokeIdx], scx, scy);
            double canvasScreenX = canvas.x * zoom - hScroll;
            double canvasScreenY = canvas.y * zoom - vScroll;
            double emx = (event->x - canvasScreenX) / zoom;
            double emy = (event->y - canvasScreenY) / zoom;
            double currentAngle = std::atan2(emy - scy, emx - scx);
            double delta = currentAngle - _rotateStartAngle;
            canvas.strokes[_rotateStrokeIdx].rotation = _rotateOrigRotation + delta;
            _drawingArea.queue_draw();
        }
        return true;
    }

    if (_dragType == CtDrawingDragType::EditPoint) {
        if (_selectStrokeIdx >= 0 &&
            _selectDragPointIdx >= 0 &&
            static_cast<size_t>(_selectStrokeIdx) < canvas.strokes.size())
        {
            auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
            auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
            double hScroll = hAdj ? hAdj->get_value() : 0.0;
            double vScroll = vAdj ? vAdj->get_value() : 0.0;
            double canvasScreenX = canvas.x * zoom - hScroll;
            double canvasScreenY = canvas.y * zoom - vScroll;
            double mx = (event->x - canvasScreenX) / zoom;
            double my = (event->y - canvasScreenY) / zoom;
            canvas.strokes[_selectStrokeIdx].points[_selectDragPointIdx] = {mx, my};
            _drawingArea.queue_draw();
        }
        return true;
    }

    if (_dragType == CtDrawingDragType::SelectBand) {
        _selectBandEndX = event->x;
        _selectBandEndY = event->y;
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::MoveSelection) {
        for (auto& [idx, origPts] : _moveSelOrigPoints) {
            if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                auto& pts = canvas.strokes[idx].points;
                for (size_t i = 0; i < pts.size() && i < origPts.size(); ++i) {
                    pts[i].x = origPts[i].x + dx;
                    pts[i].y = origPts[i].y + dy;
                }
            }
        }
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::ScaleSelection) {
        double bboxW = _scaleBBoxMaxX - _scaleBBoxMinX;
        double bboxH = _scaleBBoxMaxY - _scaleBBoxMinY;
        if (bboxW < 1.0) bboxW = 1.0;
        if (bboxH < 1.0) bboxH = 1.0;
        auto hAdjS = _pMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdjS = _pMainWin->getScrolledwindowText().get_vadjustment();
        double hScrollS = hAdjS ? hAdjS->get_value() : 0.0;
        double vScrollS = vAdjS ? vAdjS->get_value() : 0.0;
        double canvasScreenX = canvas.x * zoom - hScrollS;
        double canvasScreenY = canvas.y * zoom - vScrollS;
        double mx = (event->x - canvasScreenX) / zoom;
        double my = (event->y - canvasScreenY) / zoom;
        double newW = std::abs(mx - _scaleAnchorX);
        double newH = std::abs(my - _scaleAnchorY);
        double scaleX = newW / bboxW;
        double scaleY = newH / bboxH;
        if (scaleX < 0.05) scaleX = 0.05;
        if (scaleY < 0.05) scaleY = 0.05;

        for (auto& [idx, origPts] : _scaleOrigPoints) {
            if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                auto& pts = canvas.strokes[idx].points;
                for (size_t i = 0; i < pts.size() && i < origPts.size(); ++i) {
                    pts[i].x = _scaleAnchorX + (origPts[i].x - _scaleAnchorX) * scaleX;
                    pts[i].y = _scaleAnchorY + (origPts[i].y - _scaleAnchorY) * scaleY;
                }
            }
        }
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
        if (_toolbarVisible) _updateToolbarPosition();
        _drawingArea.queue_draw();
        return true;
    }

    if (_dragType == CtDrawingDragType::Draw) {
        if (_previewActive) {
            auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
            auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
            double hScroll = hAdj ? hAdj->get_value() : 0.0;
            double vScroll = vAdj ? vAdj->get_value() : 0.0;

            double cx = canvas.x * zoom - hScroll;
            double cy = canvas.y * zoom - vScroll;
            _previewEnd.x = (event->x - cx) / zoom;
            _previewEnd.y = (event->y - cy) / zoom;
            _drawingArea.queue_draw();
        } else if (!canvas.strokes.empty()) {
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
            canvas.tsCreation = std::time(nullptr);
            canvas.tsLastSave = canvas.tsCreation;

            auto cmd = std::make_unique<AddCanvasCommand>(
                bridge->getDocumentModel(), treeIter.get_node_id(), canvas);
            bridge->executeCommand(std::move(cmd));

            auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
            if (nodeModel) {
                _selectedCanvasIdx = static_cast<int>(nodeModel->getDrawingCanvases().size()) - 1;
                _showToolbar();
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
    else if (_dragType == CtDrawingDragType::MoveStroke) {
        if (_moveStrokeIdx >= 0 && static_cast<size_t>(_moveStrokeIdx) < canvas.strokes.size()) {
            auto newPoints = canvas.strokes[_moveStrokeIdx].points;
            canvas.strokes[_moveStrokeIdx].points = _moveStrokeOrigPoints;
            bool moved = false;
            for (size_t i = 0; i < newPoints.size(); ++i) {
                if (newPoints[i].x != _moveStrokeOrigPoints[i].x ||
                    newPoints[i].y != _moveStrokeOrigPoints[i].y) {
                    moved = true;
                    break;
                }
            }
            if (moved) {
                auto cmd = std::make_unique<MoveStrokeCommand>(
                    bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                    static_cast<size_t>(_moveStrokeIdx),
                    _moveStrokeOrigPoints, std::move(newPoints));
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
            }
        }
        _moveStrokeIdx = -1;
        _moveStrokeOrigPoints.clear();
    }
    else if (_dragType == CtDrawingDragType::RotateStroke) {
        if (_rotateStrokeIdx >= 0 && static_cast<size_t>(_rotateStrokeIdx) < canvas.strokes.size()) {
            double newRotation = canvas.strokes[_rotateStrokeIdx].rotation;
            canvas.strokes[_rotateStrokeIdx].rotation = _rotateOrigRotation;
            if (std::abs(newRotation - _rotateOrigRotation) > 1e-6) {
                auto cmd = std::make_unique<RotateStrokeCommand>(
                    bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                    static_cast<size_t>(_rotateStrokeIdx),
                    _rotateOrigRotation, newRotation);
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
            }
        }
        _rotateStrokeIdx = -1;
    }
    else if (_dragType == CtDrawingDragType::EditPoint) {
        if (_selectStrokeIdx >= 0 &&
            _selectDragPointIdx >= 0 &&
            static_cast<size_t>(_selectStrokeIdx) < canvas.strokes.size())
        {
            auto newPoints = canvas.strokes[_selectStrokeIdx].points;
            canvas.strokes[_selectStrokeIdx].points = _selectOrigPoints;
            bool changed = false;
            for (size_t i = 0; i < newPoints.size() && i < _selectOrigPoints.size(); ++i) {
                if (newPoints[i].x != _selectOrigPoints[i].x ||
                    newPoints[i].y != _selectOrigPoints[i].y) {
                    changed = true;
                    break;
                }
            }
            if (changed) {
                auto cmd = std::make_unique<MoveStrokeCommand>(
                    bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                    static_cast<size_t>(_selectStrokeIdx),
                    _selectOrigPoints, std::move(newPoints));
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
            }
        }
        _selectDragPointIdx = -1;
        _selectOrigPoints.clear();
    }
    else if (_dragType == CtDrawingDragType::SelectBand) {
        auto hAdj = _pMainWin->getScrolledwindowText().get_hadjustment();
        auto vAdj = _pMainWin->getScrolledwindowText().get_vadjustment();
        double hScroll = hAdj ? hAdj->get_value() : 0.0;
        double vScroll = vAdj ? vAdj->get_value() : 0.0;
        double zoom2 = _pMainWin->get_rt_zoom_scale_factor();
        double canvasScreenX = canvas.x * zoom2 - hScroll;
        double canvasScreenY = canvas.y * zoom2 - vScroll;

        double bandMinX = (std::min(_selectBandStartX, _selectBandEndX) - canvasScreenX) / zoom2;
        double bandMinY = (std::min(_selectBandStartY, _selectBandEndY) - canvasScreenY) / zoom2;
        double bandMaxX = (std::max(_selectBandStartX, _selectBandEndX) - canvasScreenX) / zoom2;
        double bandMaxY = (std::max(_selectBandStartY, _selectBandEndY) - canvasScreenY) / zoom2;

        bool ctrlHeld = (event->state & GDK_CONTROL_MASK) != 0;
        std::set<int> hits;
        for (size_t si = 0; si < canvas.strokes.size(); ++si) {
            double sMinX, sMinY, sMaxX, sMaxY;
            _strokeBoundingBox(canvas.strokes[si], sMinX, sMinY, sMaxX, sMaxY);
            if (sMaxX >= bandMinX && sMinX <= bandMaxX &&
                sMaxY >= bandMinY && sMinY <= bandMaxY) {
                hits.insert(static_cast<int>(si));
            }
        }

        if (ctrlHeld) {
            for (int h : hits) {
                if (_selectedStrokeIndices.count(h))
                    _selectedStrokeIndices.erase(h);
                else
                    _selectedStrokeIndices.insert(h);
            }
        } else {
            _selectedStrokeIndices = hits;
        }

        if (_selectedStrokeIndices.size() == 1) {
            int onlyIdx = *_selectedStrokeIndices.begin();
            if (static_cast<size_t>(onlyIdx) < canvas.strokes.size() &&
                canvas.strokes[onlyIdx].type == CtDrawingElementType::BezierCurve) {
                _selectStrokeIdx = onlyIdx;
            } else {
                _selectStrokeIdx = -1;
            }
        } else {
            _selectStrokeIdx = -1;
        }

        _selectBandStartX = _selectBandStartY = 0.0;
        _selectBandEndX = _selectBandEndY = 0.0;
    }
    else if (_dragType == CtDrawingDragType::MoveSelection) {
        auto compound = std::make_unique<CompoundCommand>("Move strokes");
        compound->setNodeId(treeIter.get_node_id());
        bool anyMoved = false;

        for (auto& [idx, origPts] : _moveSelOrigPoints) {
            if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                auto newPoints = canvas.strokes[idx].points;
                canvas.strokes[idx].points = origPts;
                bool moved = false;
                for (size_t i = 0; i < newPoints.size() && i < origPts.size(); ++i) {
                    if (newPoints[i].x != origPts[i].x || newPoints[i].y != origPts[i].y) {
                        moved = true;
                        break;
                    }
                }
                if (moved) {
                    compound->addCommand(std::make_unique<MoveStrokeCommand>(
                        bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                        static_cast<size_t>(idx), origPts, std::move(newPoints)));
                    anyMoved = true;
                }
            }
        }

        if (anyMoved) {
            bridge->executeCommand(std::move(compound));
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        }
        _moveSelOrigPoints.clear();
    }
    else if (_dragType == CtDrawingDragType::ScaleSelection) {
        auto compound = std::make_unique<CompoundCommand>("Scale strokes");
        compound->setNodeId(treeIter.get_node_id());
        bool anyScaled = false;

        for (auto& [idx, origPts] : _scaleOrigPoints) {
            if (static_cast<size_t>(idx) < canvas.strokes.size()) {
                auto newPoints = canvas.strokes[idx].points;
                canvas.strokes[idx].points = origPts;
                bool changed = false;
                for (size_t i = 0; i < newPoints.size() && i < origPts.size(); ++i) {
                    if (newPoints[i].x != origPts[i].x || newPoints[i].y != origPts[i].y) {
                        changed = true;
                        break;
                    }
                }
                if (changed) {
                    compound->addCommand(std::make_unique<MoveStrokeCommand>(
                        bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                        static_cast<size_t>(idx), origPts, std::move(newPoints)));
                    anyScaled = true;
                }
            }
        }

        if (anyScaled) {
            bridge->executeCommand(std::move(compound));
            _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        }
        _scaleOrigPoints.clear();
        _scaleHandleIdx = -1;
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
        if (_previewActive) {
            _previewActive = false;
            double dx = _previewEnd.x - _previewStart.x;
            double dy = _previewEnd.y - _previewStart.y;
            if (std::abs(dx) > 1.0 || std::abs(dy) > 1.0) {
                CtDrawingStroke newStroke;
                newStroke.color = _currentColor;
                newStroke.lineWidth = _currentLineWidth;
                newStroke.opacity = _currentOpacity;
                newStroke.lineStyle = _currentLineStyle;
                newStroke.filled = _currentFilled;
                newStroke.points.push_back(_previewStart);
                newStroke.points.push_back(_previewEnd);
                if (_currentTool == CtDrawingTool::Line) {
                    newStroke.type = _currentLineType;
                    newStroke.arrowHead = _currentArrowHead;
                    newStroke.arrowStyle = _currentArrowStyle;
                } else {
                    newStroke.type = _currentShapeType;
                }
                auto cmd = std::make_unique<DrawStrokeCommand>(
                    bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                    std::move(newStroke));
                bridge->executeCommand(std::move(cmd));
                _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
            }
        } else if (!canvas.strokes.empty()) {
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

void CtDrawingOverlay::_showTextDialog(double canvasX, double canvasY, size_t canvasIdx,
                                        const CtDrawingStroke* existingStroke, int existingIdx)
{
    Gtk::Dialog dlg(_("Insert Text"), *_pMainWin, true);
    dlg.add_button(_("Cancel"), Gtk::RESPONSE_CANCEL);
    dlg.add_button(_("OK"), Gtk::RESPONSE_OK);
    dlg.set_default_response(Gtk::RESPONSE_OK);

    auto* box = dlg.get_content_area();
    box->set_spacing(8);
    box->set_margin_start(12);
    box->set_margin_end(12);
    box->set_margin_top(8);

    auto* entry = Gtk::manage(new Gtk::Entry());
    entry->set_activates_default(true);
    if (existingStroke) entry->set_text(existingStroke->textContent);
    box->pack_start(*Gtk::manage(new Gtk::Label(_("Text:"))), false, false);
    box->pack_start(*entry, false, false);

    auto* fontBtn = Gtk::manage(new Gtk::FontButton());
    std::string fontStr = existingStroke ? existingStroke->fontFamily : _currentColor.empty() ? "Sans" : "Sans";
    double fontSize = existingStroke ? existingStroke->fontSize : 14.0;
    fontBtn->set_font_name(fontStr + " " + std::to_string(static_cast<int>(fontSize)));
    box->pack_start(*Gtk::manage(new Gtk::Label(_("Font:"))), false, false);
    box->pack_start(*fontBtn, false, false);

    box->show_all();

    if (dlg.run() != Gtk::RESPONSE_OK) return;

    std::string text = entry->get_text();
    if (text.empty()) return;

    Pango::FontDescription fd(fontBtn->get_font_name());
    std::string family = fd.get_family();
    double fsize = fd.get_size() / Pango::SCALE;

    auto* bridge = _pMainWin->get_command_bridge();
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!bridge || !treeIter) return;

    if (existingStroke && existingIdx >= 0) {
        auto eraseCmd = std::make_unique<EraseStrokeCommand>(
            bridge->getDocumentModel(), treeIter.get_node_id(),
            canvasIdx, *existingStroke, static_cast<size_t>(existingIdx));
        bridge->executeCommand(std::move(eraseCmd));
    }

    CtDrawingStroke stroke;
    stroke.type = CtDrawingElementType::Text;
    stroke.color = _currentColor;
    stroke.lineWidth = _currentLineWidth;
    stroke.opacity = _currentOpacity;
    stroke.lineStyle = _currentLineStyle;
    stroke.textContent = text;
    stroke.fontFamily = family;
    stroke.fontSize = fsize;
    stroke.points.push_back({
        existingStroke ? existingStroke->points[0].x : canvasX,
        existingStroke ? existingStroke->points[0].y : canvasY
    });

    auto cmd = std::make_unique<DrawStrokeCommand>(
        bridge->getDocumentModel(), treeIter.get_node_id(),
        canvasIdx, std::move(stroke));
    bridge->executeCommand(std::move(cmd));
    _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
}

void CtDrawingOverlay::_strokeBoundingBox(const CtDrawingStroke& stroke,
                                          double& minX, double& minY,
                                          double& maxX, double& maxY)
{
    if (stroke.points.empty()) {
        minX = minY = maxX = maxY = 0.0;
        return;
    }

    if (stroke.type == CtDrawingElementType::Text && !stroke.textContent.empty()) {
        auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 1, 1);
        auto tmpCr = Cairo::Context::create(surface);
        auto layout = Pango::Layout::create(tmpCr);
        layout->set_text(stroke.textContent);
        Pango::FontDescription fd;
        fd.set_family(stroke.fontFamily);
        fd.set_size(static_cast<int>(stroke.fontSize * Pango::SCALE));
        layout->set_font_description(fd);
        int tw, th;
        layout->get_pixel_size(tw, th);
        minX = stroke.points[0].x;
        minY = stroke.points[0].y;
        maxX = minX + tw;
        maxY = minY + th;
    } else {
        minX = maxX = stroke.points[0].x;
        minY = maxY = stroke.points[0].y;
        for (size_t i = 1; i < stroke.points.size(); ++i) {
            minX = std::min(minX, stroke.points[i].x);
            minY = std::min(minY, stroke.points[i].y);
            maxX = std::max(maxX, stroke.points[i].x);
            maxY = std::max(maxY, stroke.points[i].y);
        }
    }

    double hw = stroke.lineWidth / 2.0;
    minX -= hw;
    minY -= hw;
    maxX += hw;
    maxY += hw;

    if (std::abs(stroke.rotation) > 1e-6) {
        double scx, scy;
        _strokeCenter(stroke, scx, scy);
        double cosA = std::cos(stroke.rotation);
        double sinA = std::sin(stroke.rotation);
        double corners[4][2] = {
            {minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}
        };
        double rMinX = 1e18, rMinY = 1e18, rMaxX = -1e18, rMaxY = -1e18;
        for (auto& c : corners) {
            double dx = c[0] - scx, dy = c[1] - scy;
            double rx = scx + dx * cosA - dy * sinA;
            double ry = scy + dx * sinA + dy * cosA;
            rMinX = std::min(rMinX, rx);
            rMinY = std::min(rMinY, ry);
            rMaxX = std::max(rMaxX, rx);
            rMaxY = std::max(rMaxY, ry);
        }
        minX = rMinX; minY = rMinY;
        maxX = rMaxX; maxY = rMaxY;
    }
}

void CtDrawingOverlay::_drawSelectionHighlight(const Cairo::RefPtr<Cairo::Context>& cr,
                                                const CtDrawingStroke& stroke,
                                                double cx, double cy, double zoom)
{
    double minX, minY, maxX, maxY;
    _strokeBoundingBox(stroke, minX, minY, maxX, maxY);

    double sx = cx + minX * zoom;
    double sy = cy + minY * zoom;
    double sw = (maxX - minX) * zoom;
    double sh = (maxY - minY) * zoom;

    cr->set_source_rgba(0.2, 0.5, 1.0, 0.6);
    cr->set_line_width(1.5);
    std::vector<double> dashes{4.0, 3.0};
    cr->set_dash(dashes, 0.0);
    cr->rectangle(sx, sy, sw, sh);
    cr->stroke();
    cr->unset_dash();
}

void CtDrawingOverlay::_applyPropertyToSelection(std::function<void(CtDrawingStroke&)> mutate)
{
    if (_selectedStrokeIndices.empty() || _selectedCanvasIdx < 0) return;
    auto* bridge = _pMainWin->get_command_bridge();
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!bridge || !bridge->isActive() || !treeIter) return;
    auto docModel = bridge->getDocumentModel();
    auto nodeModel = docModel->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return;
    auto& canvases = nodeModel->getDrawingCanvasesMut();
    size_t ci = static_cast<size_t>(_selectedCanvasIdx);
    if (ci >= canvases.size()) return;

    auto compound = std::make_unique<CompoundCommand>("Change stroke properties");
    compound->setNodeId(treeIter.get_node_id());
    bool anyChanged = false;

    for (int idx : _selectedStrokeIndices) {
        if (static_cast<size_t>(idx) >= canvases[ci].strokes.size()) continue;
        CtDrawingStroke oldStroke = canvases[ci].strokes[idx];
        CtDrawingStroke newStroke = oldStroke;
        mutate(newStroke);
        if (newStroke.color != oldStroke.color ||
            newStroke.lineWidth != oldStroke.lineWidth ||
            newStroke.opacity != oldStroke.opacity ||
            newStroke.lineStyle != oldStroke.lineStyle ||
            newStroke.filled != oldStroke.filled ||
            newStroke.arrowHead != oldStroke.arrowHead ||
            newStroke.arrowStyle != oldStroke.arrowStyle)
        {
            compound->addCommand(std::make_unique<StrokePropertiesCommand>(
                docModel, treeIter.get_node_id(), ci,
                static_cast<size_t>(idx), std::move(oldStroke), std::move(newStroke)));
            anyChanged = true;
        }
    }

    if (anyChanged) {
        bridge->executeCommand(std::move(compound));
        _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
        _drawingArea.queue_draw();
    }
}

void CtDrawingOverlay::_clearStrokeSelection()
{
    _selectedStrokeIndices.clear();
    _selectStrokeIdx = -1;
    _selectDragPointIdx = -1;
    _selectOrigPoints.clear();
    _moveSelOrigPoints.clear();
}

void CtDrawingOverlay::_copySelectedStrokes()
{
    if (_selectedStrokeIndices.empty() || _selectedCanvasIdx < 0) return;
    auto* bridge = _pMainWin->get_command_bridge();
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!bridge || !treeIter) return;
    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return;
    const auto& canvases = nodeModel->getDrawingCanvases();
    size_t ci = static_cast<size_t>(_selectedCanvasIdx);
    if (ci >= canvases.size()) return;

    _strokeClipboard.clear();
    for (int idx : _selectedStrokeIndices) {
        if (static_cast<size_t>(idx) < canvases[ci].strokes.size()) {
            _strokeClipboard.push_back(canvases[ci].strokes[idx]);
        }
    }
}

void CtDrawingOverlay::_cutSelectedStrokes()
{
    _copySelectedStrokes();
    _deleteSelectedStrokes();
}

void CtDrawingOverlay::_pasteStrokes(double canvasX, double canvasY)
{
    if (_strokeClipboard.empty() || _selectedCanvasIdx < 0) return;
    auto* bridge = _pMainWin->get_command_bridge();
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!bridge || !treeIter) return;
    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return;
    const auto& canvases = nodeModel->getDrawingCanvases();
    size_t ci = static_cast<size_t>(_selectedCanvasIdx);
    if (ci >= canvases.size()) return;

    double groupMinX = 1e18, groupMinY = 1e18;
    double groupMaxX = -1e18, groupMaxY = -1e18;
    for (const auto& s : _strokeClipboard) {
        double bMinX, bMinY, bMaxX, bMaxY;
        _strokeBoundingBox(s, bMinX, bMinY, bMaxX, bMaxY);
        groupMinX = std::min(groupMinX, bMinX);
        groupMinY = std::min(groupMinY, bMinY);
        groupMaxX = std::max(groupMaxX, bMaxX);
        groupMaxY = std::max(groupMaxY, bMaxY);
    }
    double centerX = (groupMinX + groupMaxX) / 2.0;
    double centerY = (groupMinY + groupMaxY) / 2.0;

    if (canvasX < 0.0 || canvasY < 0.0) {
        canvasX = canvases[ci].width / 2.0;
        canvasY = canvases[ci].height / 2.0;
    }
    double offsetX = canvasX - centerX + 10.0;
    double offsetY = canvasY - centerY + 10.0;

    auto compound = std::make_unique<CompoundCommand>("Paste strokes");
    compound->setNodeId(treeIter.get_node_id());

    int baseIdx = static_cast<int>(canvases[ci].strokes.size());
    _selectedStrokeIndices.clear();

    for (size_t i = 0; i < _strokeClipboard.size(); ++i) {
        CtDrawingStroke pasted = _strokeClipboard[i];
        for (auto& p : pasted.points) {
            p.x += offsetX;
            p.y += offsetY;
        }
        compound->addCommand(std::make_unique<DrawStrokeCommand>(
            bridge->getDocumentModel(), treeIter.get_node_id(), ci,
            std::move(pasted)));
        _selectedStrokeIndices.insert(baseIdx + static_cast<int>(i));
    }

    bridge->executeCommand(std::move(compound));
    _selectStrokeIdx = -1;
    _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
    _drawingArea.queue_draw();
}

void CtDrawingOverlay::_deleteSelectedStrokes()
{
    if (_selectedStrokeIndices.empty() || _selectedCanvasIdx < 0) return;
    auto* bridge = _pMainWin->get_command_bridge();
    auto treeIter = _pMainWin->curr_tree_iter();
    if (!bridge || !treeIter) return;
    auto nodeModel = bridge->getDocumentModel()->getNodeById(treeIter.get_node_id());
    if (!nodeModel) return;
    const auto& canvases = nodeModel->getDrawingCanvases();
    size_t ci = static_cast<size_t>(_selectedCanvasIdx);
    if (ci >= canvases.size()) return;

    auto compound = std::make_unique<CompoundCommand>("Delete strokes");
    compound->setNodeId(treeIter.get_node_id());

    // iterate in reverse to keep indices valid during execute
    for (auto it = _selectedStrokeIndices.rbegin(); it != _selectedStrokeIndices.rend(); ++it) {
        int idx = *it;
        if (static_cast<size_t>(idx) < canvases[ci].strokes.size()) {
            compound->addCommand(std::make_unique<EraseStrokeCommand>(
                bridge->getDocumentModel(), treeIter.get_node_id(), ci,
                canvases[ci].strokes[idx], static_cast<size_t>(idx)));
        }
    }

    bridge->executeCommand(std::move(compound));
    _clearStrokeSelection();
    _pMainWin->update_window_save_needed(CtSaveNeededUpdType::None, true);
    _drawingArea.queue_draw();
}

void CtDrawingOverlay::_showStrokeContextMenu(GdkEventButton* event)
{
    auto* menu = Gtk::manage(new Gtk::Menu());

    auto* cutItem = Gtk::manage(new Gtk::MenuItem(_("Cut Strokes")));
    cutItem->signal_activate().connect([this]() { _cutSelectedStrokes(); });
    menu->append(*cutItem);

    auto* copyItem = Gtk::manage(new Gtk::MenuItem(_("Copy Strokes")));
    copyItem->signal_activate().connect([this]() { _copySelectedStrokes(); });
    menu->append(*copyItem);

    auto* pasteItem = Gtk::manage(new Gtk::MenuItem(_("Paste Strokes")));
    pasteItem->set_sensitive(!_strokeClipboard.empty());
    pasteItem->signal_activate().connect([this]() {
        _pasteStrokes(_lastClickCanvasX, _lastClickCanvasY);
    });
    menu->append(*pasteItem);

    menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

    auto* deleteItem = Gtk::manage(new Gtk::MenuItem(_("Delete Strokes")));
    deleteItem->signal_activate().connect([this]() { _deleteSelectedStrokes(); });
    menu->append(*deleteItem);

    menu->show_all();
    menu->popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
}

void CtDrawingOverlay::_showContextMenu(GdkEventButton* event)
{
    if (_currentTool == CtDrawingTool::Select && !_selectedStrokeIndices.empty()) {
        _showStrokeContextMenu(event);
        return;
    }

    auto* menu = Gtk::manage(new Gtk::Menu());
    bool hasSelection = _selectedCanvasIdx >= 0;

    if (hasSelection) {
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
            data.tsCreation = canvas.tsCreation;
            data.tsLastSave = canvas.tsLastSave;

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
