# Architecture Changes: Document Model & Command System

CherryTree's undo/redo and editing internals were redesigned. The original system (`CtStateMachine`) captured full XML snapshots of each node on word boundaries, with no command objects, no model layer, and no observer pattern. The GTK TextBuffer was the only source of truth.

The new architecture introduces a proper document model, a command pattern with lightweight delta commands, and an observer-driven bridge between model and view.

---

## Document Model

A GTK-free in-memory representation of the document tree.

- **CtDocumentModel** — tree of `CtNodeModel` objects, with an observer interface (`CtDocumentObserver`) for change notification
- **CtNodeModel** — node properties (name, syntax, tags, bold, foreground, readonly) plus a `CtNodeContent`
- **CtNodeContent** — flat offset-based content as a vector of `CtContentElement` (either `CtTextSpan` or `CtWidgetDesc`). Provides `insertText`, `deleteRange`, `applyFormat`, `insertWidget`, etc.
- **CtTextSpan** — a text run with an attributes map (bold, italic, colors, etc.)
- **CtWidgetDesc** — lightweight widget descriptor for images, anchors, LaTeX, codeboxes, and tables. Tables carry either `tableData` (plain) or `richTableData` (rich cells with formatting and embedded widgets)
- **CtCellContent** — rich table cell content: text spans + embedded widget descriptors

The model is the canonical source of truth. XML is generated on demand for file storage and clipboard, not held in memory for undo.

---

## Command System

All editable operations go through a command pattern with undo/redo stacks.

- **CtCommand** — base class with `execute()`, `undo()`, `redo()`
- **CompoundCommand** — groups multiple commands into one undo step; tracks node ID, cursor positions, scroll position
- **CtCommandManager** — manages undo/redo stacks

### Delta Commands (text)

Instead of storing full-buffer XML snapshots, delta commands record only what changed:

- **InsertTextCommand** — stores inserted text + attributes at an offset. Consecutive keystrokes are coalesced into a single command via `appendText()`
- **DeleteRangeCommand** — stores deleted content (text spans + widgets) at a range
- **ApplyFormatCommand / RemoveFormatCommand** — stores attribute changes with old values for rollback

These update `CtNodeContent` directly, achieving ~40x memory reduction compared to full XML snapshots.

### Session-Based Capture

- **CtTextEditSession** — batches rapid keystrokes into a single undo step using word-boundary detection. Connects to GTK buffer signals (insert, erase, tag-apply, tag-remove) and produces delta commands when the session commits

### Fallback

- **TextEditCommand** — XML before/after snapshot, used for paste and edge cases where delta capture isn't practical

### Widget Commands

- **InsertWidgetDeltaCommand** — insert a widget (image, codebox, table, etc.) at a character offset
- **ModifyWidgetDeltaCommand** — modify widget properties (resize, edit)
- **EditTableCellCommand** — old/new cell text for plain tables
- **EditRichCellCommand** — old/new `CtCellContent` for rich table cells
- **EditCodeboxContentCommand** — old/new codebox text

---

## Bridge

**CtCommandBridge** connects the GTK view layer to the document model and command system.

- Owns `CtCommandManager`, `CtDocumentModel`, and text edit sessions
- Intercepts GTK buffer signals and routes them to the appropriate command or session
- Uses a **BridgeOp** enum (`None`, `CapturingPaste`, `CapturingCut`, `CapturingFormat`, `TrackingWidget`, `ExecutingUndo`, `ExecutingRedo`) as a single state tracker, replacing the original scattered boolean flags
- **BridgeObserver** (inner class implementing `CtDocumentObserver`) syncs model changes back to GTK widgets during undo/redo

### Widget Edit Tracking

When the user edits inside an embedded widget (codebox, table cell, rich cell), the bridge tracks:

- `_widgetEditNodeId` — which node owns the widget
- `_widgetEditCharOffset` — widget's position in the buffer
- `_widgetEditRow` / `_widgetEditCol` — for table cells
- `_widgetEditIsRichCell` — true when editing a `CtTableRich` cell (enables formatting commands and a dedicated `CtTextEditSession` for the cell buffer)

---

## Rich Tables

A new table type (`CtTableRich`) supports per-cell rich text formatting and embedded widgets.

- **CtRichCell** — extends `CtTextCell` with rich formatting. Populated from `CtCellContent`, can snapshot back to `CtCellContent`. Supports embedded widgets (images, anchors, LaTeX) within cells
- **CtTableRich** — grid of `CtRichCell` objects. Each cell shares the main window's tag table so formatting commands work identically to the main editor
- Three table types coexist: `CtTableLight` (plain, TreeView-based), `CtTableHeavy` (plain, Grid-based), `CtTableRich` (rich text, Grid-based). Users can convert between them via the table properties dialog

---

## Key Design Decisions

- **Model as source of truth** — `CtNodeContent` holds the canonical state, not the GTK TextBuffer. The buffer is a view that gets rebuilt during undo/redo
- **Delta over snapshot** — most edits produce small delta commands instead of full-buffer copies. Only paste/fallback paths use XML snapshots
- **Single state enum** — `BridgeOp` replaced five independent boolean flags, eliminating a class of state-conflict bugs
- **Signal-based sessions** — `CtTextEditSession` captures formatting changes (tag apply/remove) through GTK signals rather than XML diffing, making format undo reliable
- **Unified command virtuals** — `CtCommand` base provides virtual `getNodeId()`, `getOldCursorPos()`, `getNewCursorPos()` so undo/redo navigation works without downcasting

---

## Architectural Comparison

| Aspect | Original (`CtStateMachine`) | Current (Command + Observer + Model) |
|--------|----------------------------|--------------------------------------|
| Undo/redo system | Per-node state array | Global command stacks |
| Source of truth | GTK TextBuffer | `CtNodeContent` (structured model) |
| State capture | Full snapshot on word boundaries | Delta commands, coalesced per word |
| Text undo storage | `xmlpp::Document` per snapshot | `InsertTextCommand` / `DeleteRangeCommand` (~150B per word, coalesced) |
| Widget undo storage | `CtAnchoredWidgetState_*` typed C++ objects | `CtWidgetDesc` with `contentData`/`tableData` |
| Widget reconstruction | `to_widget()` — direct C++ constructors | `buildBufferFromContent()` from `CtWidgetDesc` — direct C++ constructors |
| Text reconstruction | Parse XML via `CtStorageXmlHelper` | Read `CtTextSpan` array, apply formatting tags |
| Architecture pattern | State machine (imperative snapshot/restore) | Command + Observer + Model (declarative) |
| Model-View separation | None (buffer IS the model) | Clean (`CtDocumentModel` is GTK-free) |
| Undo granularity | Word boundary (alphanumeric transitions) | Word boundary (session-based, similar heuristic) |
| Undo scope | Per-node | Global (commands track nodeId) |
| Compound undo | Not applicable (single snapshots) | Notification suppression (1 rebuild, not N) |
| Memory per undo entry | ~100KB+ (full XML text + widget state copies) | ~150B per coalesced word (delta commands only) |
| GTK dependency | Pervasive | Model layer is GTK-free |
| Plain text/code undo | GtkSourceBuffer built-in | Same command system as rich text |
| Testability | Requires GTK for any test | Document model testable in isolation |
| Codebase size | ~466 lines in one file | ~7200 lines across 12 new files |

### Original Architecture — Advantages

- **Simplicity.** The entire undo/redo system was 466 lines in one file. No command hierarchy, no observer interface, no model layer, no bridge. Easy to read and reason about.
- **Type-safe widget state.** Each widget type had a dedicated `CtAnchoredWidgetState_*` class with typed fields and a proper `equal()` method. No string-keyed property maps. The compiler caught type errors.
- **State deduplication.** `update_state()` compared the new snapshot against the last one (XML string equality + widget state `equal()`) and skipped storing if nothing changed.
- **Direct widget reconstruction.** `to_widget()` called the exact widget constructor with typed arguments. No intermediate format, no property map lookups.
- **Per-node isolation.** One node's undo history couldn't affect another. Simpler mental model.
- **Scroll position tracking.** Each `CtNodeState` stored `v_adj_val` (vertical scroll position). Undo restored content, cursor, and viewport.
- **Mature and battle-tested.** Years of real-world use with no major architectural issues.

### Original Architecture — Disadvantages

- **Memory.** Each undo state stored full XML serialization + widget state copies. For a 100KB node, each state ≈ 100KB+. With 20 undo levels, that's 2MB per node.
- **No model layer.** The GTK TextBuffer was the source of truth. No way to inspect or test content without a live GTK environment.
- **Scattered triggers.** `text_variation()` was called from multiple places. Adding a new trigger meant finding all the right locations.
- **Coarse granularity.** Undo was word-level at best. No way to undo just a bold toggle without also undoing adjacent text edits.
- **Rich text only.** Plain text and code nodes relied on GtkSourceBuffer's built-in undo with different behavior.
- **No compound undo.** A paste that inserted text + formatting + a widget was one opaque snapshot, not a structured sequence.

### Current Architecture — Advantages

- **Memory efficiency.** ~40x reduction. Delta commands store only what changed (~150 bytes per word after coalescing vs ~100KB per snapshot).
- **Independent model layer.** `CtDocumentModel` and `CtNodeContent` have no GTK dependency. Testable without a display server.
- **Structured undo.** Each operation is a distinct command object. You can inspect the undo stack and understand exactly what each entry does.
- **Compound operations with notification suppression.** `CompoundCommand` groups multiple deltas with a single buffer rebuild, eliminating widget corruption.
- **Unified undo for all operations.** Text, formatting, paste, cut, and widget edits go through the same command system.
- **Global undo scope.** Commands track `nodeId`, so multi-step undo/redo across nodes is supported.
- **Extensibility.** Adding a new undoable operation means creating a new `CtCommand` subclass with a well-defined pattern.

### Current Architecture — Disadvantages

- **Complexity.** ~7200 lines across 12 files vs 466 lines in one file. The conceptual surface area (Command, CompoundCommand, CommandManager, DocumentModel, NodeModel, NodeContent, TextSpan, WidgetDesc, BridgeObserver, CommandBridge, TextEditSession) is significantly larger.
- **Bridge complexity.** `CtCommandBridge` manages state transitions via `BridgeOp` enum and tracking fields for each operation type. Coordinating global undo across nodes, session boundaries, widget tracking, and GTK event timing requires careful attention.
- **Generic widget descriptor.** `CtWidgetDesc` uses `map<string, string> properties` instead of per-type classes with typed fields. This trades compile-time checking for a simpler, more extensible design — property keys are set in few places and any mistake surfaces immediately in tests.
- **Two sync directions.** During editing, buffer signals update the model. During undo/redo, commands update the model and the observer rebuilds the buffer. These flows are mutually exclusive via `BridgeOp`, but maintaining two separate paths adds to the conceptual surface area.
- **Hybrid command types.** Text-only pastes use delta commands, but pastes containing widgets fall back to XML-snapshot `TextEditCommand`, meaning two undo code paths coexist.
