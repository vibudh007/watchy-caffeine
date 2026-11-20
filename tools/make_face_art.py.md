# Art face assets

`rounded_points`, `cubic`, and `leaf` draw the fixed contours. The face-specific renderers produce the five one-bit art backgrounds. `digit_font` creates packed numerals, `ring_thresholds` builds the battery ring masks, and `bell_bitmap` packs the alarm icon. `c_bytes` serializes C arrays and `main` generates or checks the art include.

Run `python3 -B -s tools/make_face_art.py` from the repo. Use `--check` to validate the generated result. [Build and asset instructions](../README.md#build-from-source).
