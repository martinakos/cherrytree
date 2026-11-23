# Drawing canvases

A drawing canvas floats over a rich_text page. `read_node` lists a node's canvases; with `include_strokes: true` it also lists each canvas's strokes (and their points with `include_points: true`).

## Coordinates

- A canvas's `x`, `y` are **page** coordinates: pixels from the top-left corner of the text area.
- Stroke points are **relative to the canvas's top-left corner**, from `[0, 0]` to `[width, height]`. Points outside are refused.
- While the canvas is active, its header covers the top 28 pixels: keep important strokes below `y = 30`.

## Tools

- `add_canvas` — `x`, `y`, `width`, `height`, and optional `name`, `bg_color`, `bg_opacity` (0–1, default 0.15), `corner_radius`, `show_border`.
- `update_canvas` — move, resize or restyle; shrinking so strokes fall outside is refused.
- `delete_canvas` — the user confirms.
- `edit_canvas` — a list of `operations`, applied in order as one undo step. Each sees the stroke indexes left by the ones before.
- `render_canvas` — a PNG of the canvas. **Render after each batch** and fix overlaps, text that does not fit, and misplaced arrows.

## Operations

| op | fields |
|---|---|
| `add` | `type`, its geometry, style fields |
| `update` | `index`, the fields to change (geometry included) |
| `delete` | `index` — asks the user |
| `move` | `index`, `dx`, `dy` |
| `rotate` | `index`, `degrees` (absolute) |

## Stroke types and geometry

| type | geometry |
|---|---|
| `line` | `from: [x, y]`, `to: [x, y]`; `arrow_head`: `none`, `start`, `end`, `both` — use this for arrows between shapes |
| `rectangle`, `rounded_rectangle`, `ellipse`, `triangle`, `diamond` | `x`, `y`, `width`, `height` (the bounding box) |
| `arrow`, `double_arrow` | `x`, `y`, `width`, `height` — a block arrow shape pointing right |
| `text` | `x`, `y` (top-left of the text), `text`, `font_size` (default 14), `font_family` |
| `polyline`, `freehand` | `points: [[x, y], …]` |
| `bezier` | `points`: start, control 1, control 2, end |

Style fields: `color` (`#rrggbb`, default black), `fill_color` (sets `filled`), `filled`, `line_width` (default 2), `line_style` (`solid`, `dashed`, `dotted`, `dash_dot`), `opacity` (0–1), `rotation` (degrees), `arrow_style` (`solid`, `open`).

## Examples

A labelled box (about 8 px per character at font size 14):

```json
{"op": "add", "type": "rounded_rectangle", "x": 20, "y": 40, "width": 140, "height": 50, "fill_color": "#dae8fc", "color": "#6c8ebf"},
{"op": "add", "type": "text", "x": 45, "y": 55, "text": "Start", "font_size": 14}
```

An arrow from the right edge of one box to the left edge of the next:

```json
{"op": "add", "type": "line", "from": [160, 65], "to": [220, 65], "arrow_head": "end"}
```

A small flow chart: three boxes 60 px apart in a row (`x` = 20, 220, 420, each 140 × 50, at `y` = 40), a label inside each, and two arrows joining the edges (`[160, 65] → [220, 65]`, `[360, 65] → [420, 65]`). Make the canvas at least 580 × 120, then call `render_canvas`.
