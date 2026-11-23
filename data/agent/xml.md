# CherryTree XML

`read_node` with `format: "xml"` returns a rich_text node's content as CherryTree XML, and `set_node_content` with `format: "xml"` replaces it. Use this when Markdown and `format_text` are not enough, for example many colors, fonts and justification across a page. For ordinary edits prefer Markdown and `format_text`.

## The format

The content is a sequence of elements, without a root element:

```xml
<rich_text scale="h1">Title
</rich_text><rich_text>Plain text, </rich_text><rich_text weight="heavy" foreground="#c00000">bold red</rich_text><rich_text>.
</rich_text><codebox char_offset="32" frame_width="500" frame_height="100" width_in_pixels="1" syntax_highlighting="sh" highlight_brackets="1" show_line_numbers="0" auto_resize_h="1">ls -l</codebox>
```

`rich_text` attributes: `weight="heavy"`, `style="italic"`, `underline="single"`, `strikethrough="true"`, `family="monospace"` (or a font family), `scale` (`h1`…`h6`, `small`, `sup`, `sub`), `foreground` / `background` (`#rrggbb`), `justification` (`center`, `right`, `fill`), `indent` (a number), `link` (`webs <url>`, `node <id> [anchor]`).

Text is XML-escaped (`&amp;`, `&lt;`), and newlines are part of the text.

Objects are `codebox`, `table` (with `row` / `cell` children, header row **last**) and `encoded_png` (images, anchors, LaTeX, files). Their `char_offset` is recomputed, so it need not be exact.

## Images and attached files

Their data is left out of the XML: such an element has no content and an attribute `keep="<object index>"`. Leave `keep` as it is, and the write puts the data back. You may move these elements or change their other attributes. An element dropped from the new XML is a deleted object, which the user must confirm.

## Errors

XML that is not well formed is refused with the parser's message, and so is a `keep` that does not name an image or file of the node.
