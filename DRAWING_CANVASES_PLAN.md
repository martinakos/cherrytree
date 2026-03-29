# Implementation Plan: Floating Drawing Canvases Over Node Pages

## Context

Add multiple floating, transparent drawing canvases per node that hover over the text content. Each canvas is an independent rectangular region where the user can draw freeform colored lines. Canvases can be selected, resized, dragged to a new position, or deleted — similar to a floating widget but transparent and overlaid on the text. The drawing canvases are independent from text content and unaffected by text reflow.

---

## Step 1: Data Model

**New file:** [ct_drawing.h](src/ct/ct_drawing.h)

```cpp
struct CtDrawingPoint {
    double x;  // relative to canvas top-left
    double y;
};

struct CtDrawingStroke {
    std::vector<CtDrawingPoint> points;
    std::string color;     // "#rrggbb"
    double lineWidth{2.0};
    double opacity{1.0};
};

struct CtDrawingCanvas {
    double x;              // document-space position (top-left)
    double y;
    double width{200.0};   // canvas dimensions
    double height{150.0};
    std::vector<CtDrawingStroke> strokes;
};
```

Stroke coordinates are **relative to the canvas top-left**, so moving a canvas moves all its strokes automatically.

**Add to `CtNodeModel`** in [ct_document_model.h](src/ct/ct_document_model.h) (~line 130):

```cpp
std::vector<CtDrawingCanvas> _drawingCanvases;
```

With accessors `getDrawingCanvases()` / `getDrawingCanvasesMut()`.

---

## Step 2: Overlay Widget — Single DrawingArea Rendering All Canvases

**New file:** [ct_drawing.cc](src/ct/ct_drawing.cc)

Use a single `Gtk::Overlay` + `Gtk::DrawingArea` over the scrolled window. The DrawingArea renders **all** canvases for the current node and handles hit-testing to determine which canvas receives input.

**Widget hierarchy change** in [ct_main_win.h](src/ct/ct_main_win.h) (~line 342) and [ct_main_win.cc](src/ct/ct_main_win.cc) (~lines 136-162):

```
_vboxText → _overlay → _scrolledwindowText → _ctTextview
                     → _drawingArea (transparent, renders all canvases)
```

GTK3 (~line 152-153):
```cpp
_overlay.add(_scrolledwindowText);
_overlay.add_overlay(_drawingArea);
_overlay.set_overlay_pass_through(_drawingArea, true);  // default pass-through
_vboxText.pack_start(_overlay);
```

New members in `CtMainWin`:
```cpp
Gtk::Overlay    _overlay;
Gtk::DrawingArea _drawingArea;
bool            _drawingMode{false};
int             _selectedCanvasIdx{-1};   // -1 = none selected
```

### Cairo Rendering

`_drawingArea.signal_draw()` handler:
1. Get scroll offsets from `_scrolledwindowText` adjustments
2. For each `CtDrawingCanvas` in the current node:
   - Translate to canvas position (canvas.x - hScroll, canvas.y - vScroll)
   - **If in drawing mode**: Draw a visible border (dashed line, e.g. 1px gray)
   - **If selected**: Draw border highlighted (e.g. blue) + resize handles (small squares at corners/edges)
   - Clip to canvas bounds
   - Render all strokes within the canvas
3. Background is fully transparent — text shows through

### Input Handling

**When `_drawingMode == false`:**
- `set_overlay_pass_through(_drawingArea, true)` — all events go to text view
- Canvases are invisible borders, fully transparent, no interaction

**When `_drawingMode == true`:**
- `set_overlay_pass_through(_drawingArea, false)` — DrawingArea captures events
- Hit-test pointer position against all canvases to determine action:

| Click target | Action |
|---|---|
| Inside a canvas interior | Start drawing a stroke (append points on motion) |
| On a canvas border/edge | Start resize drag (track which edge/corner) |
| On a canvas title bar area (top edge, small band) | Start move drag |
| On a selected canvas + Delete key | Delete the canvas |
| Outside all canvases | Deselect; or if dragging from empty space, create a new canvas (drag to define rectangle) |

**Creating a new canvas:**
- User drags on empty space to define a rectangle
- On release, a new `CtDrawingCanvas` is added with the dragged bounds
- Minimum size enforced (e.g. 50x50)

**Selecting a canvas:**
- Click on a canvas border or inside it to select
- Selected canvas shows highlighted border + resize handles
- Only one canvas selected at a time

**Resizing:**
- Drag corner/edge handles to resize
- Strokes stay at their canvas-relative coordinates (canvas grows/shrinks around them)
- Strokes outside the new bounds get clipped visually

**Moving:**
- Drag the canvas border (or a small drag zone at the top) to reposition
- Updates `canvas.x` and `canvas.y` — strokes move with it automatically since they're canvas-relative

**Deleting:**
- Select a canvas, press Delete key, or right-click context menu → Delete

---

## Step 3: Node Switching

**Modify** [ct_main_win_events.cc](src/ct/ct_main_win_events.cc) `_on_treeview_cursor_changed()`:

- Canvas data lives in `CtNodeModel` — persists automatically
- On node switch: reset `_selectedCanvasIdx = -1`, call `_drawingArea.queue_draw()`
- Drawing mode state (`_drawingMode`) persists across node switches (it's a UI mode, not per-node)

---

## Step 4: Undo/Redo Commands

Add to [ct_drawing.h](src/ct/ct_drawing.h):

```cpp
// Drawing a stroke within a canvas
class DrawStrokeCommand : public CtCommand {
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
    // execute: push stroke to canvas, undo: pop it
};

// Erasing a stroke
class EraseStrokeCommand : public CtCommand {
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
    size_t _strokeIdx;
    // execute: remove stroke, undo: re-insert
};

// Adding a new canvas
class AddCanvasCommand : public CtCommand {
    gint64 _nodeId;
    CtDrawingCanvas _canvas;
    size_t _canvasIdx;
    // execute: insert canvas, undo: remove it
};

// Deleting a canvas (with all its strokes)
class DeleteCanvasCommand : public CtCommand {
    gint64 _nodeId;
    CtDrawingCanvas _canvas;  // full snapshot for undo
    size_t _canvasIdx;
    // execute: remove canvas, undo: re-insert with all strokes
};

// Moving a canvas
class MoveCanvasCommand : public CtCommand {
    gint64 _nodeId;
    size_t _canvasIdx;
    double _oldX, _oldY, _newX, _newY;
    // execute: set new pos, undo: restore old pos
};

// Resizing a canvas
class ResizeCanvasCommand : public CtCommand {
    gint64 _nodeId;
    size_t _canvasIdx;
    double _oldX, _oldY, _oldW, _oldH;
    double _newX, _newY, _newW, _newH;
    // execute: set new bounds, undo: restore old bounds
};
```

All commands trigger `_drawingArea.queue_draw()` after execution. Wire through `CtCommandBridge`.

---

## Step 5: Drawing Mode Toggle & Toolbar

**Add action** in a new [ct_actions_draw.cc](src/ct/ct_actions_draw.cc):

- `toggle_drawing_mode`: Flips `_drawingMode`
  - **On**: Shows canvas borders, changes cursor to crosshair, enables event capture on overlay, shows drawing toolbar
  - **Off**: Hides borders, deselects canvas, restores normal cursor, disables overlay event capture, hides drawing toolbar

**Drawing toolbar** (new toolbar or panel):
- **Draw tool** (default): Freeform stroke drawing inside selected canvas
- **Select tool**: Click to select/move/resize canvases
- **New canvas**: Drag to create a new canvas rectangle
- **Color picker**: Stroke color
- **Line width**: Slider or dropdown (1px, 2px, 4px, 8px)
- **Eraser**: Click a stroke to delete it
- **Delete canvas**: Delete the selected canvas and all its strokes

**Menu entry** in [ct_menu.cc](src/ct/ct_menu.cc) + keyboard shortcut (e.g. Ctrl+Shift+D).

---

## Step 6: Storage Serialization

### XML ([ct_storage_xml.cc](src/ct/ct_storage_xml.cc))

```xml
<node ...>
  <rich_text>...</rich_text>
  <drawing_canvases>
    <canvas x="100" y="200" width="300" height="250">
      <stroke color="#ff0000" width="2.0" opacity="1.0">10,20;15,30;20,25</stroke>
      <stroke color="#0000ff" width="4.0" opacity="0.8">50,50;60,70;80,90</stroke>
    </canvas>
    <canvas x="400" y="600" width="200" height="150">
      <stroke ...>...</stroke>
    </canvas>
  </drawing_canvases>
</node>
```

Points as semicolon-separated `x,y` pairs (canvas-relative). Backward compatible.

### SQLite ([ct_storage_sqlite.cc](src/ct/ct_storage_sqlite.cc))

Two new tables:
```sql
CREATE TABLE IF NOT EXISTS drawing_canvas (
    node_id INTEGER,
    canvas_index INTEGER,
    x REAL, y REAL,
    width REAL, height REAL,
    PRIMARY KEY (node_id, canvas_index)
);

CREATE TABLE IF NOT EXISTS drawing_stroke (
    node_id INTEGER,
    canvas_index INTEGER,
    stroke_index INTEGER,
    color TEXT,
    width REAL,
    opacity REAL,
    points BLOB,  -- packed array of double pairs
    PRIMARY KEY (node_id, canvas_index, stroke_index)
);
```

### MultiFile ([ct_storage_multifile.cc](src/ct/ct_storage_multifile.cc))

A `drawing.xml` per node directory, same XML format as above.

---

## Step 7: Eraser Tool

- **Stroke eraser**: In drawing mode with eraser active, click inside a canvas near a stroke → delete that stroke (`EraseStrokeCommand`). Hit-test by computing minimum distance from click point to each stroke's polyline segments.
- **Clear canvas**: Right-click context menu on selected canvas → "Clear all strokes". `CompoundCommand` of `EraseStrokeCommand`s.

---

## Files to Create

| File | Purpose |
|------|---------|
| [ct_drawing.h](src/ct/ct_drawing.h) | Data structures (`CtDrawingCanvas`, `CtDrawingStroke`), command classes, overlay class declaration |
| [ct_drawing.cc](src/ct/ct_drawing.cc) | Cairo rendering, input handling (hit-test, draw, move, resize), overlay management |
| [ct_actions_draw.cc](src/ct/ct_actions_draw.cc) | Drawing mode toggle, toolbar actions |

## Files to Modify

| File | Change |
|------|--------|
| [ct_main_win.h](src/ct/ct_main_win.h) | Add `_overlay`, `_drawingArea`, `_drawingMode`, `_selectedCanvasIdx` members |
| [ct_main_win.cc](src/ct/ct_main_win.cc) | Insert overlay in widget hierarchy (lines 136-162) |
| [ct_main_win_events.cc](src/ct/ct_main_win_events.cc) | Reset drawing selection on node switch, queue redraw |
| [ct_document_model.h](src/ct/ct_document_model.h) | Add `_drawingCanvases` to `CtNodeModel` |
| [ct_command_bridge.h](src/ct/ct_command_bridge.h) | Add drawing command commit methods |
| [ct_command_bridge.cc](src/ct/ct_command_bridge.cc) | Implement drawing command wiring |
| [ct_storage_xml.cc](src/ct/ct_storage_xml.cc) | Serialize/deserialize `<drawing_canvases>` |
| [ct_storage_sqlite.cc](src/ct/ct_storage_sqlite.cc) | New `drawing_canvas` + `drawing_stroke` tables |
| [ct_storage_multifile.cc](src/ct/ct_storage_multifile.cc) | `drawing.xml` per node |
| [ct_menu.cc](src/ct/ct_menu.cc) | Add drawing mode menu/toolbar entries |
| [CMakeLists.txt](CMakeLists.txt) | Add new source files |

---

## Verification

1. **Build**: `ninja -C build` compiles without errors
2. **Toggle drawing mode**: Menu/shortcut shows canvas borders, changes cursor
3. **Create canvas**: Drag on empty space to define a new floating canvas rectangle
4. **Draw strokes**: Freeform lines inside a canvas with selected color/width
5. **Select canvas**: Click to select — highlighted border + resize handles appear
6. **Move canvas**: Drag to reposition — strokes move with it
7. **Resize canvas**: Drag handles — canvas grows/shrinks
8. **Delete canvas**: Select + Delete key removes canvas and all its strokes
9. **Multiple canvases**: Create several per node, each independent
10. **Scroll**: Canvases scroll with document (document-coordinate positioning)
11. **Node switch**: Switch away and back — canvases persist
12. **Undo/redo**: All operations (draw, erase, add/delete/move/resize canvas) are undoable
13. **Save/load**: Save, close, reopen — canvases and strokes preserved
14. **Text editing**: Text reflow does not affect canvas positions (independent)
15. **Drawing mode off**: Canvases become fully transparent and non-interactive (events pass through to text)
16. **Backward compatibility**: Older CherryTree ignores drawing data, no crash
