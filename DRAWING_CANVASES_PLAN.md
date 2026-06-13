# Floating Drawing Canvases

## Summary

Drawing canvases are transparent, floating rectangular regions with rounded corners that overlay a node's text content. Each node can have multiple independent canvases anchored to the page — they scroll and zoom with the document. Users draw freeform colored strokes inside a canvas using the mouse. Drawing tools (line width, color, delete stroke, delete canvas) are accessed through a right-click context menu on the canvas.

Two toolbar buttons control the feature: one creates a new canvas (and activates drawing mode), the other toggles drawing mode on/off. When drawing mode is on, canvases show a visible rounded-corner border with a small header bar for dragging, and the user can draw, resize, and move canvases. When drawing mode is off, canvases stay static and their content renders transparently over the node — all user interaction (clicks, typing, selection) passes through to the underlying text content.

---

## Step 1: Data Structures

**New file:** `src/ct/ct_drawing.h`

```cpp
struct CtDrawingPoint {
    double x;  // relative to canvas top-left
    double y;
};

struct CtDrawingStroke {
    std::vector<CtDrawingPoint> points;
    std::string color;     // "#rrggbb"
    double lineWidth{2.0};
    double opacity{1.0};   // 0.0 (transparent) to 1.0 (opaque)
};

struct CtDrawingCanvas {
    double x;              // document-space position (top-left)
    double y;
    double width{300.0};
    double height{250.0};
    double cornerRadius{8.0};
    std::vector<CtDrawingStroke> strokes;
};
```

Stroke coordinates are **relative to the canvas top-left**, so moving a canvas moves all its strokes automatically.

---

## Step 2: Model Integration

Drawing canvases float over text independently. They live **on `CtNodeModel`** as a parallel data structure to `_content`, not inside `CtNodeContent`.

**Add to `CtNodeModel`** in `src/ct/ct_document_model.h`:

```cpp
std::vector<CtDrawingCanvas> _drawingCanvases;
```

With accessors `getDrawingCanvases()` / `getDrawingCanvasesMut()`.

**Add to `CtDocumentObserver`:**
```cpp
virtual void onNodeDrawingChanged(gint64 nodeId) {}
```

**Add to `CtDocumentModel`:**
```cpp
void notifyNodeDrawingChanged(gint64 nodeId);
```

This avoids triggering a full buffer rebuild (`onNodeChanged`) when only drawing data changes. The `BridgeObserver` handles this by calling `queue_draw()` on the drawing overlay.

---

## Step 3: Overlay Widget

**New file:** `src/ct/ct_drawing.cc`

Use a single `Gtk::Overlay` + `Gtk::DrawingArea` over the scrolled window. The DrawingArea renders all canvases for the current node and handles hit-testing to determine which canvas receives input.

**Widget hierarchy change** in `src/ct/ct_main_win.h` and `src/ct/ct_main_win.cc`:

```
_vboxText -> _overlay -> _scrolledwindowText -> _ctTextview
                      -> _drawingArea (transparent, renders all canvases)
```

GTK3:
```cpp
_overlay.add(_scrolledwindowText);
_overlay.add_overlay(_drawingArea);
_overlay.set_overlay_pass_through(_drawingArea, true);  // default pass-through
_vboxText.pack_start(_overlay);
```

GTK4:
```cpp
_overlay.set_child(_scrolledwindowText);
_overlay.add_overlay(_drawingArea);
_vboxText.append(_overlay);
```

Event pass-through differs between GTK versions:
- GTK3: `_overlay.set_overlay_pass_through(_drawingArea, !_drawingMode)`
- GTK4: Use event controllers; `_drawingArea` captures/ignores pointer events via `Gtk::GestureClick` / `Gtk::GestureDrag` sensitivity

New members in `CtMainWin`:
```cpp
Gtk::Overlay     _overlay;
Gtk::DrawingArea _drawingArea;
bool             _drawingMode{false};
int              _selectedCanvasIdx{-1};   // -1 = none selected

// Current drawing tool state (set via context menu)
double           _currentLineWidth{2.0};
std::string      _currentColor{"#000000"};
double           _currentOpacity{1.0};
bool             _deleteStrokeMode{false};
```

### Canvas Appearance

Each canvas is drawn as a **rounded-corner rectangle** (using `cairo_arc` for corners with the canvas's `cornerRadius`).

**Header bar**: A small strip (~20px tall) at the top of the canvas. When drawing mode is on, it's rendered with a slightly different shade (e.g., semi-transparent gray) to indicate the drag handle. The user clicks and drags on this header to move the entire canvas (with all its strokes) to a new position in the node.

**Resize handles**: The canvas can be resized by dragging any edge or corner:
- The **four corners** (diagonal resize)
- The **four sides** — left, right, top, bottom (single-axis resize)

**Move vs. resize at the top**: The top edge doubles as the header bar. The distinction is based on cursor position:
- Cursor on the **top side edge** (within ~8px of the border) or **top corners**: resize
- Cursor **inside the header area** (away from the edges): move/drag

### Cairo Rendering

`_drawingArea.signal_draw()` (GTK3) / `set_draw_func()` (GTK4) handler:
1. Get scroll offsets from `_scrolledwindowText` adjustments
2. For each `CtDrawingCanvas` in the current node:
   - Translate to canvas position (canvas.x - hScroll, canvas.y - vScroll)
   - Draw the rounded-corner rectangle path (always, both modes)
   - **If drawing mode is on**: Fill with semi-transparent white, stroke the border (e.g., 1px gray dashed), draw the header bar
   - **If drawing mode is off**: No fill, no border — just render the strokes transparently over the content
   - Clip to canvas bounds
   - Render all strokes within the canvas (each stroke is a polyline: `cairo_move_to` to first point, `cairo_line_to` for each subsequent point, with the stroke's `lineWidth` and `color`)
3. When drawing mode is on and a canvas is selected: highlight the border (e.g., blue) and show resize handle indicators at the bottom corners and sides

### Input Handling

**When `_drawingMode == false`:**
- All events pass through to text view
- Canvases render their strokes but are fully non-interactive

**When `_drawingMode == true`:**
- DrawingArea captures events
- Hit-test pointer position against all canvases to determine action:

| Click target | Action |
|---|---|
| Inside canvas header bar | Start move drag — reposition canvas on motion |
| Inside canvas interior (not header) | If `_deleteStrokeMode`: hit-test nearby strokes, delete closest. Otherwise: start drawing a new stroke (append points on motion) |
| On any corner (within ~8px) | Start diagonal resize |
| On any edge — left, right, top, or bottom (within ~8px) | Start single-axis resize |
| Outside all canvases | Deselect any selected canvas; pass event through |
| Right-click inside a canvas | Show the drawing context menu |

**Drawing strokes:**
- When the left mouse button is pressed inside a canvas (not on header, not on resize handles, and `_deleteStrokeMode` is off):
  1. Begin a new `CtDrawingStroke` with the current `_currentLineWidth` and `_currentColor`
  2. On mouse motion while pressed: append each position as a `CtDrawingPoint` (canvas-relative coordinates)
  3. On mouse release: finalize the stroke and push a `DrawStrokeCommand` to the undo stack
- Each stroke is a **sequence of points connected by lines** (a polyline/segment chain) rendered with Cairo's `cairo_line_to`

**Delete stroke mode:**
- Activated from the context menu ("Delete Stroke")
- When active, the cursor changes (e.g., crosshair or eraser icon)
- Clicking inside a canvas hit-tests all strokes: for each stroke, check minimum distance from click point to any segment (line between consecutive points)
- If a stroke is within hit threshold (~5px): delete the **entire stroke** (all its points/segments) via an `EraseStrokeCommand`
- The mode stays active until toggled off via context menu or by switching drawing mode off

**Resizing:**
- Drag bottom corners or sides to resize
- Strokes stay at their canvas-relative coordinates (canvas grows/shrinks around them)
- Strokes outside the new bounds get clipped visually
- Minimum size enforced (e.g., 80x60)
- On release: push a `ResizeCanvasCommand`

**Moving:**
- Drag the header bar to reposition
- Updates `canvas.x` and `canvas.y` — strokes move with it automatically since they're canvas-relative
- On release: push a `MoveCanvasCommand`

**Anchoring with the page:**
- Canvas coordinates are in document space (relative to the text view's content, not the viewport)
- When the user scrolls, the rendering translates by the scroll offsets — canvases move with the content
- When the user zooms, canvas coordinates and stroke dimensions scale with the zoom factor

---

## Step 4: Context Menu

Right-clicking inside a canvas (when drawing mode is on) shows a context menu with these options:

1. **Line Width** — submenu with width options:
   - Thin (1px)
   - Normal (2px) *(default)*
   - Thick (4px)
   - Very Thick (8px)
   - The currently selected width is indicated with a checkmark/radio

2. **Color** — submenu with color options:
   - Black *(default)*
   - Red
   - Blue
   - Green
   - Yellow
   - White
   - Custom... (opens a GTK color chooser dialog)
   - The currently selected color is indicated with a checkmark/radio

3. **Opacity** — submenu with opacity levels:
   - 100% *(default)*
   - 75%
   - 50%
   - 25%
   - The currently selected opacity is indicated with a checkmark/radio

4. **Delete Stroke** — toggles delete-stroke mode on/off (checkmark when active). When on, clicking on a stroke segment deletes the whole stroke.

5. **Delete Drawing Canvas** — deletes the canvas and all its strokes (with confirmation dialog). Pushes a `DeleteCanvasCommand`.

The context menu is built in `ct_actions_draw.cc` and connected via `signal_populate_popup` on the drawing area or manually shown on right-click.

---

## Step 5: Undo/Redo Commands

**New file:** `src/ct/ct_drawing_commands.h`

Six command classes following the `CtCommand` interface. Each holds `std::shared_ptr<CtDocumentModel>`, overrides `getNodeId()`, `getOldCursorPos()`, `getNewCursorPos()`, `getOldScrollPos()`, `getNewScrollPos()`, and calls `_docModel->notifyNodeDrawingChanged(_nodeId)` — never directly touching the UI.

```cpp
// Drawing a stroke within a canvas
class DrawStrokeCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
    // execute: push stroke to canvas, undo: pop it
};

// Erasing a stroke (delete stroke tool)
class EraseStrokeCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
    size_t _strokeIdx;
    // execute: remove stroke, undo: re-insert at original index
};

// Adding a new canvas (from toolbar button)
class AddCanvasCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    CtDrawingCanvas _canvas;
    size_t _canvasIdx;
    // execute: insert canvas, undo: remove it
};

// Deleting a canvas (from context menu)
class DeleteCanvasCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    CtDrawingCanvas _canvas;  // full snapshot for undo
    size_t _canvasIdx;
    // execute: remove canvas, undo: re-insert with all strokes
};

// Moving a canvas (dragging header bar)
class MoveCanvasCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    double _oldX, _oldY, _newX, _newY;
    // execute: set new pos, undo: restore old pos
};

// Resizing a canvas (dragging corners/sides)
class ResizeCanvasCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    double _oldX, _oldY, _oldW, _oldH;
    double _newX, _newY, _newW, _newH;
    // execute: set new bounds, undo: restore old bounds
};
```

All commands go through `CtCommandBridge::executeCommand()` / `addCommandToStack()` directly. Drawing mode is an orthogonal flag on `CtMainWin` — it does not use the `BridgeOp` state machine for text editing.

---

## Step 6: Toolbar Buttons

**Modify** `src/ct/ct_actions_draw.cc` (new file)

Two toolbar buttons are added to the CherryTree main toolbar:

1. **New Drawing Canvas** — creates a new canvas at a default position (centered in the current viewport) with default size (300x250). Activates drawing mode if not already on. Pushes an `AddCanvasCommand`.

2. **Toggle Drawing Mode** — switches drawing mode on/off. Toggle button (stays pressed when active).
   - **On**: Canvas borders, headers, and resize handles become visible. User can draw strokes, move, resize canvases. The drawing area captures input over canvases.
   - **Off**: Canvas borders and headers hidden. Strokes render transparently over content. All input passes through to the text view. `_deleteStrokeMode` is reset to false.

When creating a new canvas, drawing mode is automatically turned on.

Declare methods on `CtActions`:
- `new_drawing_canvas()` — create canvas + activate drawing mode
- `toggle_drawing_mode()` — flip `_drawingMode`, toggle event pass-through, change cursor

Register `CtMenuAction` entries in `src/ct/ct_menu_actions.cc` with keyboard shortcuts:
- New Drawing Canvas: `Ctrl+Shift+D`
- Toggle Drawing Mode: `Ctrl+D`

---

## Step 7: Node Switching

**Modify** `src/ct/ct_main_win_events.cc` (`_on_treeview_cursor_changed`):

- Canvas data lives in `CtNodeModel` — persists automatically
- On node switch: reset `_selectedCanvasIdx = -1`, reset `_deleteStrokeMode = false`, call `_drawingArea.queue_draw()`
- Drawing mode state (`_drawingMode`) persists across node switches (it's a UI mode, not per-node)

---

## Step 8: Storage Serialization

### XML (`src/ct/ct_storage_xml.cc`)

```xml
<node ...>
  <rich_text>...</rich_text>
  <drawing_canvases>
    <canvas x="100" y="200" width="300" height="250" corner_radius="8">
      <stroke color="#ff0000" width="2.0" opacity="1.0">10,20;15,30;20,25</stroke>
      <stroke color="#0000ff" width="4.0" opacity="0.5">50,50;60,70;80,90</stroke>
    </canvas>
  </drawing_canvases>
</node>
```

Points as semicolon-separated `x,y` pairs (canvas-relative). Backward compatible — older CherryTree ignores the element.

### SQLite (`src/ct/ct_storage_sqlite.cc`)

Two new tables, following the pattern of existing `TABLE_*_CREATE` / `TABLE_*_INSERT` / `TABLE_*_DELETE` constants:

```sql
CREATE TABLE drawing_canvas (
    node_id INTEGER, canvas_index INTEGER,
    x REAL, y REAL, width REAL, height REAL,
    corner_radius REAL DEFAULT 8.0,
    PRIMARY KEY (node_id, canvas_index)
);

CREATE TABLE drawing_stroke (
    node_id INTEGER, canvas_index INTEGER, stroke_index INTEGER,
    color TEXT, width REAL, opacity REAL DEFAULT 1.0, points BLOB,
    PRIMARY KEY (node_id, canvas_index, stroke_index)
);
```

### MultiFile (`src/ct/ct_storage_multifile.cc`)

A `drawing.xml` per node directory, same XML format as above.

### Dirty Tracking

Drawing commands call `pending_edit_db_node_buff(nodeId)` on `CtStorageControl` to mark the node for saving, piggybacking on the existing dirty-tracking mechanism.

---

## Files to Create

| File | Purpose |
|------|---------|
| `src/ct/ct_drawing.h` | Data structures, `CtDrawingOverlay` class declaration |
| `src/ct/ct_drawing.cc` | Cairo rendering, input handling, overlay management, context menu |
| `src/ct/ct_drawing_commands.h` | 6 command classes following `CtCommand` interface |
| `src/ct/ct_actions_draw.cc` | Drawing mode toggle, new canvas action, toolbar buttons |

## Files to Modify

| File | Change |
|------|--------|
| `src/ct/ct_document_model.h` | Add `_drawingCanvases` to `CtNodeModel`, `onNodeDrawingChanged` to observer, `notifyNodeDrawingChanged` to `CtDocumentModel` |
| `src/ct/ct_document_model.cc` | Implement `notifyNodeDrawingChanged` |
| `src/ct/ct_main_win.h` | Add `_overlay`, `_drawingArea`, `_drawingMode`, `_selectedCanvasIdx`, `_currentLineWidth`, `_currentColor`, `_deleteStrokeMode` |
| `src/ct/ct_main_win.cc` | Insert overlay in widget hierarchy (both GTK3 and GTK4 paths) |
| `src/ct/ct_main_win_events.cc` | Reset drawing selection and delete-stroke mode on node switch |
| `src/ct/ct_command_bridge.cc` | Bridge observer handles `onNodeDrawingChanged` -> `queue_draw()` + dirty tracking |
| `src/ct/ct_storage_xml.cc` | Serialize/deserialize `<drawing_canvases>` |
| `src/ct/ct_storage_sqlite.cc` | New tables + read/write methods |
| `src/ct/ct_storage_multifile.cc` | `drawing.xml` per node |
| `src/ct/ct_actions.h` | Declare `new_drawing_canvas()`, `toggle_drawing_mode()` |
| `src/ct/ct_menu.cc` / `ct_menu_actions.cc` | Register toolbar buttons + keyboard shortcuts |
| `CMakeLists.txt` | Add new source files |

---

## Verification

1. `ninja -C build` compiles without errors
2. Toolbar: "New Drawing Canvas" button creates a canvas and activates drawing mode
3. Toolbar: "Toggle Drawing Mode" button switches mode on/off
4. Drawing mode on: canvases show rounded-corner borders with header bar
5. Draw strokes inside canvas by pressing left mouse button and dragging
6. Strokes rendered as polylines with correct width and color
7. Right-click context menu: change line width, change color (both reflected in subsequent strokes)
8. Right-click context menu: "Delete Stroke" mode — clicking on a stroke deletes the entire stroke
9. Right-click context menu: "Delete Drawing Canvas" removes the canvas
10. Header bar drag: moves canvas with all strokes to new position
11. Any corner/edge drag: resizes canvas; header interior drag: moves canvas
12. Undo/redo: all 6 operations (draw stroke, erase stroke, add canvas, delete canvas, move, resize)
13. Scroll: canvases scroll with document (anchored to page coordinates)
14. Zoom: canvases scale with zoom factor
15. Drawing mode off: strokes render transparently, all input passes through to text view
16. Node switch: canvases persist, selection resets, drawing mode persists
17. Save/close/reopen: canvases preserved in all 3 formats (XML, SQLite, MultiFile)
18. Backward compatibility: older CherryTree ignores drawing data
