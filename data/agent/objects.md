# Embedded objects

A rich_text node can hold code boxes, tables, images, LaTeX formulas, anchors, attached files and horizontal lines. `read_node` lists them in `objects`, and the text shows each as a `⟦#index kind …⟧` block.

## Pointing at an object

Every object tool takes `object_index` **and** `expect_kind`, both from `read_node`. An index changes when an object is inserted or removed before it; if the object at that index is of another kind, the tool refuses and you must read the node again. Every tool that changes objects returns the node's new `objects` list.

Kinds: `codebox`, `table`, `rich_table`, `image`, `latex`, `anchor`, `file`, `horizontal_line`.

## insert_object

`position` is `end` (default), `start`, `after` or `before`; the last two need `anchor_text`, which must occur once.

| Kind | properties | content |
|---|---|---|
| `codebox` | `language` (GtkSourceView id: `sh`, `python`, `cpp`, `js`, `plain-text`…), `width`, `width_in_pixels` (else % of the page), `height` (a number of pixels, or `"fit"`, the default), `fit_width`, `line_numbers`, `highlight_brackets` | the code |
| `table` | `rows`: array of arrays of cell text; row 0 is the header | |
| `rich_table` | as `table`; cells take inline Markdown (`**bold**`, `_italic_`, `` `code` ``) | |
| `image` | `path` (local file), `width` / `height` (display size), `link` | |
| `latex` | | the LaTeX source |
| `anchor` | `name` | |
| `file` | `path` (local file, attached into the document) | |

All kinds take `justification` (`left`, `center`, `right`).

Images and files come from local paths only. URLs are refused: download the file yourself first. Files over 20 MB, private keys and files in `~/.ssh`, `~/.gnupg` and the CherryTree configuration are refused; a file outside the home folder asks the user.

## Code box size

Always fit code boxes to their content unless the user wants otherwise: `height: "fit"` (the default for new code boxes) makes the box exactly as tall as its code. A number fixes the height. `fit_height` / `fit_width` take `fit`, `fixed` or `follow_preferences`.

## update_object

- codebox: any insert property; `content` replaces the code.
- image: `path` (a new picture), `width` and/or `height` (display size; with one side given the other keeps the proportions), `link`.
- latex: `content`. anchor: `name`.
- Tables use `edit_table` (topic `tables`).

## delete_object

The user confirms in CherryTree. Replacing a node's whole content with `set_node_content` also counts as deleting the objects it drops.
