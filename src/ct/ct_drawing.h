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
#include <optional>
#include <gtkmm.h>

class CtMainWin;

enum class CtDrawingElementType {
    Freehand,
    Line,
    Rectangle,
    Ellipse,
    Text,
    Polyline,
    Triangle,
    Diamond,
    RoundedRectangle
};

enum class CtDrawingLineStyle {
    Solid,
    Dashed,
    Dotted,
    DashDot
};

enum class CtDrawingArrowHead { None, Start, End, Both };
enum class CtDrawingArrowStyle { Solid, Open };

enum class CtDrawingTool {
    Pencil,
    Line,
    Shape,
    Text,
    Rubber,
    Move,
    Rotate
};

struct CtDrawingPoint {
    double x;
    double y;
};

struct CtDrawingStroke {
    std::vector<CtDrawingPoint> points;
    std::string color{"#000000"};
    double lineWidth{2.0};
    double opacity{1.0};
    CtDrawingElementType type{CtDrawingElementType::Freehand};
    CtDrawingLineStyle lineStyle{CtDrawingLineStyle::Solid};
    bool filled{false};
    std::string textContent;
    std::string fontFamily{"Sans"};
    double fontSize{14.0};
    double rotation{0.0};
    CtDrawingArrowHead arrowHead{CtDrawingArrowHead::None};
    CtDrawingArrowStyle arrowStyle{CtDrawingArrowStyle::Solid};
};

struct CtDrawingCanvas {
    double x{0.0};
    double y{0.0};
    double width{300.0};
    double height{250.0};
    double cornerRadius{8.0};
    std::string name;
    std::string bgColor{"#ffffff"};
    double bgOpacity{0.15};
    bool showBorderWhenInactive{false};
    gint64 tsCreation{0};
    gint64 tsLastSave{0};
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
    CreateCanvas,
    MoveStroke,
    RotateStroke
};

class CtDrawingOverlay {
public:
    CtDrawingOverlay(CtMainWin* pMainWin);
    ~CtDrawingOverlay() = default;

    Gtk::Overlay& getOverlay() { return _overlay; }
    Gtk::DrawingArea& getDrawingArea() { return _drawingArea; }

    void addToolbarToOverlay();

    void setDrawingMode(bool on);
    bool isDrawingMode() const { return _drawingMode; }

    void setDeleteStrokeMode(bool on) { _deleteStrokeMode = on; }
    bool isDeleteStrokeMode() const { return _deleteStrokeMode; }

    void resetSelection();
    void refresh();

    void beginCreateCanvas();
    bool isCreateCanvasMode() const { return _createCanvasMode; }

    static void setClipboard(const CtDrawingCanvas& c) { _clipboard = c; }
    static const CtDrawingCanvas& getClipboard() { return _clipboard.value(); }
    static bool hasClipboard() { return _clipboard.has_value(); }
    static void clearClipboard() { _clipboard.reset(); }

    double getCurrentLineWidth() const { return _currentLineWidth; }
    void setCurrentLineWidth(double w) { _currentLineWidth = w; }

    std::string getCurrentColor() const { return _currentColor; }
    void setCurrentColor(const std::string& c) { _currentColor = c; }

    double getCurrentOpacity() const { return _currentOpacity; }
    void setCurrentOpacity(double o) { _currentOpacity = o; }

    CtDrawingTool getCurrentTool() const { return _currentTool; }
    void setCurrentTool(CtDrawingTool tool);

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

    void _buildToolbar();
    void _showToolbar();
    void _hideToolbar();
    void _updateToolbarPosition();
    void _updateToolButtonStates();
    static gboolean _onGetChildPosition(GtkOverlay* overlay, GtkWidget* widget,
                                         GdkRectangle* alloc, gpointer data);
    void _showTextDialog(double canvasX, double canvasY, size_t canvasIdx,
                         const CtDrawingStroke* existingStroke = nullptr, int existingIdx = -1);

    void _drawStroke(const Cairo::RefPtr<Cairo::Context>& cr,
                     const CtDrawingStroke& stroke,
                     double cx, double cy, double zoom);
    void _drawArrowHead(const Cairo::RefPtr<Cairo::Context>& cr,
                        double tipX, double tipY,
                        double fromX, double fromY,
                        double lineWidth, double zoom,
                        CtDrawingArrowStyle style);

    void _strokeCenter(const CtDrawingStroke& stroke, double& centerX, double& centerY);

    static std::optional<CtDrawingCanvas> _clipboard;

    CtMainWin* _pMainWin;
    Gtk::Overlay _overlay;
    Gtk::DrawingArea _drawingArea;

    bool _drawingMode{false};
    int _selectedCanvasIdx{-1};
    bool _deleteStrokeMode{false};
    bool _createCanvasMode{false};

    CtDrawingTool _currentTool{CtDrawingTool::Pencil};
    CtDrawingElementType _currentShapeType{CtDrawingElementType::Rectangle};
    CtDrawingElementType _currentLineType{CtDrawingElementType::Line};
    CtDrawingLineStyle _currentLineStyle{CtDrawingLineStyle::Solid};
    CtDrawingArrowHead _currentArrowHead{CtDrawingArrowHead::None};
    CtDrawingArrowStyle _currentArrowStyle{CtDrawingArrowStyle::Solid};
    double _currentLineWidth{2.0};
    std::string _currentColor{"#000000"};
    double _currentOpacity{1.0};
    bool _currentFilled{false};

    Gtk::Box* _pToolbarBox{nullptr};
    Gtk::EventBox* _pToolbarEventBox{nullptr};
    bool _toolbarVisible{false};
    int _toolbarPosX{0};
    int _toolbarPosY{0};
    std::vector<Gtk::ToggleButton*> _toolButtons;

    bool _previewActive{false};
    CtDrawingPoint _previewStart{0.0, 0.0};
    CtDrawingPoint _previewEnd{0.0, 0.0};

    bool _polylineActive{false};
    std::vector<CtDrawingPoint> _polylinePoints;

    Gtk::Image* _pLineToolIcon{nullptr};
    Gtk::Image* _pShapeToolIcon{nullptr};
    Gtk::Image* _pMoveToolIcon{nullptr};

    CtDrawingDragType _dragType{CtDrawingDragType::None};
    CtDrawingHitZone _resizeZone{CtDrawingHitZone::None};
    double _dragStartX{0.0};
    double _dragStartY{0.0};
    double _dragCanvasOrigX{0.0};
    double _dragCanvasOrigY{0.0};
    double _dragCanvasOrigW{0.0};
    double _dragCanvasOrigH{0.0};

    int _moveStrokeIdx{-1};
    std::vector<CtDrawingPoint> _moveStrokeOrigPoints;

    int _rotateStrokeIdx{-1};
    double _rotateOrigRotation{0.0};
    double _rotateStartAngle{0.0};

    bool _updatingToolButtons{false};

    static constexpr double EDGE_HIT_THRESHOLD = 8.0;
    static constexpr double HEADER_HEIGHT = 28.0;
    static constexpr double MIN_CANVAS_WIDTH = 80.0;
    static constexpr double MIN_CANVAS_HEIGHT = 60.0;
    static constexpr double STROKE_HIT_THRESHOLD = 5.0;
    static constexpr double TOOLBAR_HEIGHT = 42.0;
    static constexpr double TOOLBAR_GAP = 6.0;
};
