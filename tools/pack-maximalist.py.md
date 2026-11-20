# Instrument template packing

`StaticFace` inherits the layouts from `render-maximalist.py` and omits live values. `templates` rasterizes the five static faces. `glyphs` crops and packs Barlow clock digits, `hex_rows` formats bytes, and `main` writes `source/watch/face_max_data.inc`.

Run `python3 -B -s tools/pack-maximalist.py`. [Instrument artwork](render-maximalist.py.md).
