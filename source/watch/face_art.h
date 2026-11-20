#ifndef WATCH_FACE_ART_H
#define WATCH_FACE_ART_H

#include "face_data.h"
#include "frame.h"

void face_art(uint8_t frame[WATCH_FRAME_SIZE], enum watch_face style, const face_data_t *data);

#endif
