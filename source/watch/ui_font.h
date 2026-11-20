#ifndef WATCH_UI_FONT_H
#define WATCH_UI_FONT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_TINY,
    UI_SMALL,
    UI_BODY,
    UI_MENU,
    UI_TITLE,
    UI_LARGE,
    UI_DATE,
    UI_NUMBER,
    UI_MONO_TINY,
    UI_MONO_SMALL,
    UI_MONO_MEDIUM,
    UI_MONO_LARGE,
    UI_MONO_HERO,
    UI_FONT_COUNT,
} ui_font_t;

int ui_text_width(ui_font_t font, const char *text);
void ui_text(uint8_t *frame, ui_font_t font, int x, int y, const char *text);
void ui_text_color(uint8_t *frame, ui_font_t font, int x, int y, const char *text, bool white);
void ui_bitmap_ink(uint8_t *frame, const uint8_t *bitmap, int64_t x, int64_t y,
                   unsigned width, unsigned height, bool white);
void ui_clock_digit(uint8_t *frame, char digit, int center_x, int y, bool large);

#endif
