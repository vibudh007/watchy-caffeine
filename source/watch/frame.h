#ifndef WATCH_FRAME_H
#define WATCH_FRAME_H

#include "watch.h"

#define WATCH_WIDTH 200
#define WATCH_HEIGHT 200
#define WATCH_STRIDE (WATCH_WIDTH / 8)
#define WATCH_FRAME_SIZE (WATCH_STRIDE * WATCH_HEIGHT)

void watch_fill(uint8_t *frame, int x, int y, int width, int height);
void watch_text(uint8_t *frame, int x, int y, unsigned scale, const char *text);
void watch_count_text(char dst[14], uint32_t count);
void watch_render(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE]);
bool watch_join_frame(uint8_t frame[WATCH_FRAME_SIZE], const char *ssid, const char *password);

#endif
