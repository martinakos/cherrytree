# Updated Plan: Floating Drawing Canvases (for document_model_on_v1.7.0)

## Context

The original `DRAWING_CANVASES_PLAN.md` was written before the `document_model_on_v1.7.0` branch introduced a structured document model (`CtNodeContent`), delta-based undo commands, an observer-driven view update pattern, and GTK3/GTK4 dual paths. This updated plan preserves the **floating overlay** design (canvases at absolute document-space coordinates, independent of text reflow) while integrating with the new architecture.

---

## What Changes from the Original Plan

### Data Model — Store on `CtNodeModel`, Not `CtNodeContent`
Drawing canvases are *not* anchored widgets — they float over text independently. So they live **on `CtNodeModel`** as a parallel data structure to `_content`, not inside `CtNodeContent`. The original plan's `_drawingCanvases` vector approach is correct, but:
- Add accessors following the existing pattern (`getDrawingCanvases()` / `getDrawingCanvasesMut()`)
- Add to `CtNodeProps` bulk capture/restore for undo support (or use separate snapshot — see commands below)
- Serialization goes through a dedicated path in each storage backend, separate from the `CtNodeContent` serialization

### Commands — Follow the New `CtCommand` Pattern
All 6 command classes from the original plan are needed, but each must:
- Hold `std::shared_ptr<CtDocumentModel>` (not raw pointers)
- Override `getNodeId()`, `getOldCursorPos()`, `getNewCursorPos()`, `getOldScrollPos()`, `getNewScrollPos()`
- Call `_docModel->notifyNodeDrawingChanged(_nodeId)` instead of `_drawingArea.queue_draw()`
- **Never** directly touch the UI — the observer handles view updates

### Observer — New `onNodeDrawingChanged` Method
Add to `CtDocumentObserver`:
```cpp
virtual void onNodeDrawingChanged(gint64 nodeId) {}
```
Add to `CtDocumentModel`:
```cpp
void notifyNodeDrawingChanged(gint64 nodeId);
```
This avoids triggering a full buffer rebuild (`onNodeChanged`) when only drawing data changes. The `BridgeObserver` handles this by calling `queue_draw()` on the drawing overlay.

### Widget Hierarchy — Dual GTK3/GTK4 Paths
Insert `Gtk::Overlay` between `_vboxText` and `_scrolledwindowText` in `ct_main_win.cc` (lines 223-247):

```cpp
// GTK4 path (line 225-227):
_overlay.set_child(_scrolledwindowText);
_overlay.add_overlay(_drawingArea);
_vboxText.append(_init_window_header());
_vboxText.append(_overlay);  // was: _vboxText.append(_scrolledwindowText)

// GTK3 path (line 238-240):
_overlay.add(_scrolledwindowText);
_overlay.add_overlay(_drawingArea);
_vboxText.pack_start(_init_window_header(), false, false);
_vboxText.pack_start(_overlay);  // was: _vboxText.pack_start(_scrolledwindowText)
```

Event pass-through differs between GTK versions:
- GTK3: `_overlay.set_overlay_pass_through(_drawingArea, !_drawingMode)`
- GTK4: Use event controllers; `_drawingArea` captures/ignores pointer events via `Gtk::GestureClick` / `Gtk::GestureDrag` sensitivity

### Command Bridge — Drawing Mode as Orthogonal State
Drawing mode does not fit the existing `BridgeOp` state machine (which tracks text editing operations). Instead, `_drawingMode` is an orthogonal flag on `CtMainWin` — drawing operations go through `CtCommandBridge::executeCommand()` / `addCommandToStack()` directly without needing a new `BridgeOp` state.

### Storage — Mark Nodes Dirty
When a drawing command executes, call `pending_edit_db_node_buff(nodeId)` on `CtStorageControl` to mark the node for saving. This piggybacks on the existing dirty-tracking mechanism.

---

## Implementation Steps

### Step 1: Data Structures (`ct_drawing.h`)
Keep `CtDrawingPoint`, `CtDrawingStroke`, `CtDrawingCanvas` structs from original plan. No changes needed.

### Step 2: Model Integration (`ct_document_model.h`)
Add to `CtNodeModel` (after `_content` at line ~167):
```cpp
std::vector<CtDrawingCanvas> _drawingCanvases;
```
With `getDrawingCanvases()` / `getDrawingCanvasesMut()` accessors.

Add to `CtDocumentObserver` (after `onNodePropertiesChanged`):
```cpp
virtual void onNodeDrawingChanged(gint64 nodeId) {}
```
Add `notifyNodeDrawingChanged(gint64)` to `CtDocumentModel`.

### Step 3: Commands (`ct_drawing_commands.h`)
6 commands following `CtCommand` interface. Example pattern:
```cpp
class DrawStrokeCommand : public CtCommand {
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
    // ...cursor/scroll position members...
    
    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        node->getDrawingCanvasesMut()[_canvasIdx].strokes.push_back(_stroke);
        _docModel->notifyNodeDrawingChanged(_nodeId);
    }
    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        node->getDrawingCanvasesMut()[_canvasIdx].strokes.pop_back();
        _docModel->notifyNodeDrawingChanged(_nodeId);
    }
};
```
Same pattern for EraseStroke, AddCanvas, DeleteCanvas, MoveCanvas, ResizeCanvas.

### Step 4: Overlay Widget (`ct_drawing.cc`)
- `CtDrawingOverlay` class managing the `Gtk::DrawingArea` over `Gtk::Overlay`
- Implements `CtDocumentObserver` (or the bridge observer calls it) to handle `onNodeDrawingChanged` → `queue_draw()`
- Cairo rendering: translate by scroll offsets, iterate current node's canvases, render strokes
- Input handling: hit-test for drawing/selecting/moving/resizing (same logic as original plan)
- GTK3: `signal_draw`, `signal_button_press_event`, etc.
- GTK4: `set_draw_func`, `Gtk::GestureClick`, `Gtk::GestureDrag`, `Gtk::EventControllerMotion`

### Step 5: Main Window Integration (`ct_main_win.h` / `.cc`)
New members:
```cpp
Gtk::Overlay _overlay;
Gtk::DrawingArea _drawingArea;
bool _drawingMode{false};
int _selectedCanvasIdx{-1};
```
Insert overlay in hierarchy (Step described above). 
On node switch (`_on_treeview_cursor_changed` in `ct_main_win_events.cc`): reset `_selectedCanvasIdx = -1`, `_drawingArea.queue_draw()`.

### Step 6: Actions (`ct_actions_draw.cc`, `ct_actions.h`)
Declare methods on `CtActions`:
- `toggle_drawing_mode()` — flip `_drawingMode`, toggle event pass-through, change cursor
- Tool selection, color, line width actions

Register `CtMenuAction` in `ct_menu_actions.cc` with category, icon, shortcut (Ctrl+Shift+D).

### Step 7: Storage

**XML** (`ct_storage_xml.cc`): Add `<drawing_canvases>` as child of `<node>`, serialized alongside anchored widgets. Parse in node-loading path, write in node-saving path.

**SQLite** (`ct_storage_sqlite.cc`): Two new tables:
```sql
CREATE TABLE drawing_canvas (
    node_id INTEGER, canvas_index INTEGER,
    x REAL, y REAL, width REAL, height REAL,
    PRIMARY KEY (node_id, canvas_index)
);
CREATE TABLE drawing_stroke (
    node_id INTEGER, canvas_index INTEGER, stroke_index INTEGER,
    color TEXT, width REAL, opacity REAL, points BLOB,
    PRIMARY KEY (node_id, canvas_index, stroke_index)
);
```
Follow the pattern of existing `TABLE_*_CREATE` / `TABLE_*_INSERT` / `TABLE_*_DELETE` constants.

**MultiFile** (`ct_storage_multifile.cc`): `drawing.xml` per node directory (same XML format).

### Step 8: Dirty Tracking
Drawing commands call `_pCtMainWin->get_ct_storage()->pending_edit_db_node_buff(nodeId)` to mark the node for saving. The `_fileSaveNeeded` flag is set via the existing `onNodeDrawingChanged` handler.

---

## Files to Create

| File | Purpose |
|------|---------|
| `src/ct/ct_drawing.h` | Data structures, `CtDrawingOverlay` class declaration |
| `src/ct/ct_drawing.cc` | Cairo rendering, input handling, overlay management |
| `src/ct/ct_drawing_commands.h` | 6 command classes following `CtCommand` interface |
| `src/ct/ct_actions_draw.cc` | Drawing mode toggle, tool actions |

## Files to Modify

| File | Change |
|------|--------|
| `src/ct/ct_document_model.h` | Add `_drawingCanvases` to `CtNodeModel`, `onNodeDrawingChanged` to observer, `notifyNodeDrawingChanged` to `CtDocumentModel` |
| `src/ct/ct_document_model.cc` | Implement `notifyNodeDrawingChanged` |
| `src/ct/ct_main_win.h` | Add `_overlay`, `_drawingArea`, `_drawingMode`, `_selectedCanvasIdx` |
| `src/ct/ct_main_win.cc` | Insert overlay in widget hierarchy (lines 223-247, both GTK paths) |
| `src/ct/ct_main_win_events.cc` | Reset drawing selection on node switch |
| `src/ct/ct_command_bridge.cc` | Bridge observer handles `onNodeDrawingChanged` → `queue_draw()` + dirty tracking |
| `src/ct/ct_storage_xml.cc` | Serialize/deserialize `<drawing_canvases>` |
| `src/ct/ct_storage_sqlite.cc` | New tables + read/write methods |
| `src/ct/ct_storage_multifile.cc` | `drawing.xml` per node |
| `src/ct/ct_actions.h` | Declare drawing action methods |
| `src/ct/ct_menu.cc` / `ct_menu_actions.cc` | Register drawing mode action + shortcut |
| `CMakeLists.txt` | Add new source files |

---

## Verification

1. `ninja -C build` compiles without errors
2. Toggle drawing mode: overlay captures events, cursor changes, canvas borders appear
3. Create canvas by dragging on empty space
4. Draw strokes inside canvas with selected color/width
5. Select/move/resize/delete canvas
6. Multiple canvases per node, each independent
7. Scroll: canvases scroll with document (coordinate translation via scroll offsets)
8. Node switch: canvases persist, selection resets
9. Undo/redo: all 6 operations undoable
10. Save/close/reopen: canvases preserved in all 3 formats (XML, SQLite, MultiFile)
11. Drawing mode off: events pass through to text view
12. Backward compatibility: older CherryTree ignores drawing data
