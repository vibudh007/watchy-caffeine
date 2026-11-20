# Experimental face assets

`word_mask` and `hour_digit` rasterize text and numerals. `quarter_rim` and `atlas_grid` draw fixed geometry. `layout`, `byte_rows`, and `bitmap_table` turn monochrome images into bounded C bitmap tables. `geometry_tables` builds fixed-point dial coordinates and minute-marker shapes. `main` writes `source/watch/face_exp_assets.inc` using the bundled fonts.

Run `python3 -B -s tools/make_watchy_experimental_assets.py`. [Asset instructions](../README.md#build-from-source).
