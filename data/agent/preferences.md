# Preferences

`get_preferences` returns CherryTree preferences that help match the user's style, each with a short description. It only reads them; no tool changes preferences.

Sections: `codebox` (default size, fit, line numbers, language), `fonts`, `colors` (style schemes, page background, monospace and link colors), `lists` (bullet, to-do and table of contents characters), `tables` (default rows, columns, column width), `editor` (tab width, line wrapping, horizontal rule, timestamp format).

Use them, for example, to:

- give new code boxes the user's usual width and line numbers setting;
- pick text colors that read well on the page background;
- write timestamps in the user's format.

Proxy credentials, recent documents and the commands CherryTree runs are never returned.
