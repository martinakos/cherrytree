# Tables

Insert a table with `insert_object` (kind `table` or `rich_table`, property `rows`). Change it with `edit_table`, giving `object_index`, `expect_kind` and a list of `operations`. The operations apply in order, as one undo step; each sees the rows and columns left by the ones before. Rows and columns count from 0, and row 0 is the header.

- `table` cells hold plain text. `rich_table` cells take inline Markdown (`**bold**`, `_italic_`, `` `code` ``, links).
- Per-cell styles (colors, borders, alignment) follow their cells when rows or columns are inserted, deleted or moved.

## Operations

| op | fields |
|---|---|
| `set_cell` | `row`, `col`, `text` |
| `insert_row` | `row` (the new row's index), `cells` (optional) |
| `delete_row` | `row` — asks the user |
| `move_row` | `row`, `to` |
| `insert_col` | `col`, `cells` (optional, one per row) |
| `delete_col` | `col` — asks the user |
| `move_col` | `col`, `to` |
| `set_col_width` | `col`, `width` (0 = default) |
| `style_cell` | `row`, `col`, and any of `background`, `halign` (left/center/right), `valign` (top/middle/bottom), `wrap`, `border_width`, `border_color` |
| `style_table` | any of `border_width`, `border_color`, `background`, `halign`, `valign`, `wrap`, `row_height` |

Colors are `#rrggbb`; an empty `background` removes it.

## Example: add a total row and highlight it

```json
{"id": 40, "object_index": 1, "expect_kind": "table", "operations": [
  {"op": "insert_row", "row": 4, "cells": ["Total", "1,250"]},
  {"op": "style_cell", "row": 4, "col": 0, "background": "#fff2cc"},
  {"op": "style_cell", "row": 4, "col": 1, "background": "#fff2cc", "halign": "right"}
]}
```

A table keeps at least one row and one column; delete a whole table with `delete_object`.
