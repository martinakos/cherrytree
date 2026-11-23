# Formatting text

Use `format_text` on a rich_text node. Name the text exactly as `read_node` shows it, without the `note_content` markers.

- `text` must occur once in the node. Otherwise give `occurrence` (1 for the first) or `all: true`.
- `set` applies attributes; `remove` clears them. One call is one undo step.
- The text cannot span an embedded `⟦…⟧` object.

## Attributes

| Name | Value | Notes |
|---|---|---|
| `bold`, `italic`, `underline`, `strikethrough`, `monospace` | `true` / `false` | |
| `small`, `superscript`, `subscript` | `true` / `false` | these share one property with `heading` |
| `heading` | 1–6, 0 removes | |
| `foreground`, `background` | `#rrggbb`, `""` removes | |
| `font_family` | a font family name | |
| `justification` | `left`, `center`, `right`, `fill` | applies to the whole lines the text is on |
| `indent` | 0–10 | |
| `link` | a URL, `node:<id>`, `node:<id>#<anchor>`, `file:<path>`, `""` removes | |

## Examples

Make a warning red and bold:

```json
{"id": 12, "text": "Do not unplug", "set": {"bold": true, "foreground": "#c00000"}}
```

Link every mention of a project to its node:

```json
{"id": 12, "text": "Bristol visit", "all": true, "set": {"link": "node:1095"}}
```

Center a title line:

```json
{"id": 12, "text": "Annual report", "set": {"justification": "center", "heading": 1}}
```

## Tips

- For new text, write Markdown with `insert_text` instead of inserting plain text and formatting it after.
- For many different formats across a page, `format: "xml"` may be simpler (topic `xml`).
- `get_preferences` section `colors` shows the user's colors, to match them.
