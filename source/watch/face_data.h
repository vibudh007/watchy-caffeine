#ifndef WATCH_FACE_DATA_H
#define WATCH_FACE_DATA_H

#include "ui_font.h"
#include "watch.h"

typedef struct {
    unsigned hour, minute, day, month, year, weekday, display_hour;
    bool format_12, alarm_on, steps_on, steps_available, degraded;
    int battery;
    char time[6], period[3], date[11], alarm[10], steps[14];
} face_data_t;

void face_ink_text(uint8_t *frame, ui_font_t font, int x, int y, const char *text, bool white);
void face_text_right(uint8_t *frame, ui_font_t font, int right, int y, const char *text);
void face_text_center(uint8_t *frame, ui_font_t font, int center, int y, const char *text);
void face_alarm(uint8_t *frame, int right, int y, const face_data_t *data, bool white);

#endif
