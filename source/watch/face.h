#ifndef WATCH_FACE_H
#define WATCH_FACE_H

#include "frame.h"

void watch_render_face(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE]);
const char *watch_face_name(enum watch_face style);
void watch_render_chooser(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE]);

#endif
