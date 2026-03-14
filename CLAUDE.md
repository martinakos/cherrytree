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

---

# Claude Assistant Preferences

Make things as simple as possible, so that they reduce the cognitive load of a human reading the code.
Comment the intent, not the mechanics.
Don't write so many documentation files, .md, .txt, etc. just explain this in the terminal
If I ask something reply to the question instead of writing some code about it.
Avoid using grey code block backgrounds (triple backticks without language specifier) for explanatory text, examples, or scenarios. Use plain text with bullet points and indentation instead. Only use code blocks for actual code snippets with proper language tags.
Do not compact, summarise, or drop prior context unless I explicitly ask you to. If context becomes large, stop and wait for instructions.

Internally plan.
Do not expose reasoning.

Respond in this exact format:

SUMMARY:
<short explanation>

CHANGES:
<bullet list>

CODE:
<final code only>
