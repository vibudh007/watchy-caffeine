idf_component_register(SRCS "calendar.c" "watch.c" "frame.c" "face.c" "face_max.c" "face_art.c" "face_exp.c"
    "settings.c" "settings_values.c" "settings_view.c" "ui_font.c" "sync.c" "join.c" "vendor/qrcodegen.c"
    INCLUDE_DIRS "." PRIV_INCLUDE_DIRS "vendor")

# The QR library includes __FILE__ in assertions. Use a repository-relative path in release images.
target_compile_options(${COMPONENT_LIB} PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=source/watch")
