# UI font generator

`rasterize` crops a monochrome glyph and records placement and packed pixels. `tables` builds glyph indices and bitmap arrays. `main` validates the local input path, applies the approved face and size profiles, and writes `source/watch/ui_font_data.inc`.

Run `python3 -B -s tools/make_watchy_ui_font.py assets/fonts`. [Font notices](../source/watch/ui_font-license.txt).
