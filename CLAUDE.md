# CherryTree Project Guide for Claude

## Project Overview

CherryTree is a hierarchical note-taking application with rich text editing and syntax highlighting. GTK3-based desktop application in C++17.

- **License**: GPL v3
- **Primary Framework**: GTKmm 3.0

## Architecture

**Design Pattern**: MVC-like with signal-driven architecture (sigc++ signals/slots)
- **Model**: `ct_treestore` (tree structure), `ct_storage_*` (SQLite/XML/MultiFile backends), `ct_document_model` / `ct_node_content` (structured content)
- **View**: `ct_main_win` (main window), `ct_text_view` (editor)
- **Controller**: `ct_actions_*.cc` files (edit, file, tree, find, format, export), `ct_app`

**Command System** (undo/redo):
- `ct_command_bridge` — orchestrates edit sessions, captures deltas
- `ct_command` / `ct_text_commands` — command objects (delta-based and XML-snapshot)

All source in `src/ct/`, files named `ct_<feature>.[h|cc]`.

## Build & Test

```bash
# Build (ninja)
ninja -C build

# Tests (require X display)
cd build && ./run_tests_with_x_2
```

CMake options: see `CMakeLists.txt` or `BUILDING.md`.

## Naming Conventions

- Files: `ct_<feature>.[h|cc]`
- Classes: PascalCase with `Ct` prefix (e.g., `CtMainWin`)
- Members: camelCase with `_` prefix for private (e.g., `_pCtMainWin`)

