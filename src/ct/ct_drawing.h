/*
 * ct_drawing.h
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

#include <vector>
#include <string>
#include <gtkmm.h>

class CtMainWin;

struct CtDrawingPoint {
    double x;
    double y;
};

struct CtDrawingStroke {
    std::vector<CtDrawingPoint> points;
    std::string color{"#000000"};
    double lineWidth{2.0};
    double opacity{1.0};
};

struct CtDrawingCanvas {
    double x{0.0};
    double y{0.0};
    double width{300.0};
    double height{250.0};
    double cornerRadius{8.0};
    std::vector<CtDrawingStroke> strokes;
};

enum class CtDrawingHitZone {
    None,
    Header,
    Interior,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Left,
    Right,
    Top,
    Bottom
};

enum class CtDrawingDragType {
    None,
    Move,
    Draw,
    Resize,
    CreateCanvas
};

class CtDrawingOverlay {
public:
    CtDrawingOverlay(CtMainWin* pMainWin);
    ~CtDrawingOverlay() = default;

    Gtk::Overlay& getOverlay() { return _overlay; }
    Gtk::DrawingArea& getDrawingArea() { return _drawingArea; }

    void setDrawingMode(bool on);
    bool isDrawingMode() const { return _drawingMode; }

    void setDeleteStrokeMode(bool on) { _deleteStrokeMode = on; }
    bool isDeleteStrokeMode() const { return _deleteStrokeMode; }

    void resetSelection();
    void refresh();

    void beginCreateCanvas();
    bool isCreateCanvasMode() const { return _createCanvasMode; }

    double getCurrentLineWidth() const { return _currentLineWidth; }
    void setCurrentLineWidth(double w) { _currentLineWidth = w; }

    std::string getCurrentColor() const { return _currentColor; }
    void setCurrentColor(const std::string& c) { _currentColor = c; }

    double getCurrentOpacity() const { return _currentOpacity; }
    void setCurrentOpacity(double o) { _currentOpacity = o; }

    int getSelectedCanvasIdx() const { return _selectedCanvasIdx; }

private:
    bool _onDraw(const Cairo::RefPtr<Cairo::Context>& cr);
    bool _onButtonPress(GdkEventButton* event);
    bool _onButtonRelease(GdkEventButton* event);
    bool _onMotionNotify(GdkEventMotion* event);
    bool _onScroll(GdkEventScroll* event);

    void _drawCanvas(const Cairo::RefPtr<Cairo::Context>& cr,
                     const CtDrawingCanvas& canvas,
                     int idx, double hScroll, double vScroll, double zoom);
    void _drawRoundedRect(const Cairo::RefPtr<Cairo::Context>& cr,
                          double x, double y, double w, double h, double r);

    CtDrawingHitZone _hitTest(double mx, double my, const CtDrawingCanvas& canvas,
                              double hScroll, double vScroll, double zoom);
    int _hitTestCanvas(double mx, double my, double hScroll, double vScroll, double zoom);
    int _hitTestStroke(double mx, double my, const CtDrawingCanvas& canvas,
                       double hScroll, double vScroll, double zoom);

    double _distPointToSegment(double px, double py,
                               double ax, double ay, double bx, double by);

    void _showContextMenu(GdkEventButton* event);

    CtMainWin* _pMainWin;
    Gtk::Overlay _overlay;
    Gtk::DrawingArea _drawingArea;

    bool _drawingMode{false};
    int _selectedCanvasIdx{-1};
    bool _deleteStrokeMode{false};
    bool _createCanvasMode{false};

    double _currentLineWidth{2.0};
    std::string _currentColor{"#000000"};
    double _currentOpacity{1.0};

    CtDrawingDragType _dragType{CtDrawingDragType::None};
    CtDrawingHitZone _resizeZone{CtDrawingHitZone::None};
    double _dragStartX{0.0};
    double _dragStartY{0.0};
    double _dragCanvasOrigX{0.0};
    double _dragCanvasOrigY{0.0};
    double _dragCanvasOrigW{0.0};
    double _dragCanvasOrigH{0.0};

    static constexpr double EDGE_HIT_THRESHOLD = 8.0;
    static constexpr double HEADER_HEIGHT = 28.0;
    static constexpr double MIN_CANVAS_WIDTH = 80.0;
    static constexpr double MIN_CANVAS_HEIGHT = 60.0;
    static constexpr double STROKE_HIT_THRESHOLD = 5.0;
};
