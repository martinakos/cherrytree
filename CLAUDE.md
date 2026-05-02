# CherryTree Project Guide for Claude

## Project Overview

CherryTree is a hierarchical note-taking application with rich text editing and syntax highlighting. GTK3-based C++17 desktop application using GTKmm 3.0 and sigc++ signals.

All source lives in `src/ct/`, named `ct_<feature>.[h|cc]`.

## Architecture

**MVC-like, signal-driven** (`sigc++` signals/slots):

- **Model**: `ct_document_model` / `ct_node_content` (in-memory, GTK-free document tree); `ct_treestore` (GTK tree structure); `ct_storage_*` (SQLite/XML/MultiFile backends)
- **View**: `ct_main_win`, `ct_text_view`
- **Controller**: `ct_actions_*.cc` — one file per concern (edit, file, tree, find, format, export, view, others)
- **Bridge**: `ct_command_bridge` — connects GTK buffer signals to the document model and command system

For a full explanation of the document model and command system design, see `ARCHITECTURE_CHANGES.md`.

## Where to Start for Common Tasks

| Task | Start here |
|------|-----------|
| Add/change a node action | `ct_actions_tree.cc` |
| Add/change a text edit action | `ct_actions_edit.cc` |
| Add/change a formatting action | `ct_actions_format.cc` |
| Add an undoable operation | `ct_command.h` — new `CtCommand` subclass; register in `ct_command_bridge.cc` |
| Change how content is stored | `ct_node_content.h` / `ct_node_content.cc` |
| Change buffer ↔ model sync | `ct_buffer_converter.cc` |
| Change file storage | `ct_storage_*.cc` |

## Key Design Constraints

- **`CtNodeContent` is the source of truth**, not the GTK TextBuffer. The buffer is a view rebuilt during undo/redo.
- **Two sync directions are mutually exclusive**: during editing, GTK buffer signals update the model; during undo/redo, commands update the model and `BridgeObserver` rebuilds the buffer. `BridgeOp` enum enforces this — always check/set it correctly when adding bridge-level code.
- **`CtDocumentModel` and `CtNodeContent` have no GTK dependency** — keep it that way. They can be instantiated and tested without a display.
- **New undoable operations need a `CtCommand` subclass** with `execute()`, `undo()`, `redo()`, and `getNodeId()`. Group related steps in a `CompoundCommand`.
- **GTK is single-threaded** — never touch widgets from a non-main thread.

## Naming Conventions

- Files: `ct_<feature>.[h|cc]`
- Classes: PascalCase with `Ct` prefix (`CtMainWin`, `CtNodeContent`)
- Private members: camelCase with `_` prefix (`_pCtMainWin`, `_nodeId`)
