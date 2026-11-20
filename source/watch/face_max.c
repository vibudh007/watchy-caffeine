#include "face_max.h"

#include <string.h>

#include "frame.h"

typedef struct {
    uint16_t offset;
    uint8_t width, height;
} digit_glyph_t;

#include "face_max_data.inc"

static void outline(uint8_t *frame, int x, int y, int width, int height)
{
    watch_fill(frame, x, y, width, 1);
    watch_fill(frame, x, y + height - 1, width, 1);
    watch_fill(frame, x, y + 1, 1, height - 2);
    watch_fill(frame, x + width - 1, y + 1, 1, height - 2);
}

static void white_box(uint8_t *frame, int x, int y, int width, int height)
{
    // Callers pass small, in-bounds instruments. Keep neighboring black art intact.
    for (int row = y; row < y + height; row++) {
        uint8_t *scan = frame + row * WATCH_STRIDE;
        for (int column = x; column < x + width; column++) {
            scan[column / 8] |= (uint8_t)(0x80u >> (column % 8));
        }
    }
}

static void digit(uint8_t *frame, int x, int y, const digit_glyph_t *glyph,
                  const uint8_t *pixels, bool white)
{
    x -= glyph->width / 2;
    int shift = x % 8;
    int pitch = (glyph->width + 7) / 8;
    const uint8_t *source = pixels + glyph->offset;
    for (unsigned row = 0; row < glyph->height; row++) {
        uint8_t *target = frame + (y + row) * WATCH_STRIDE + x / 8;
        uint8_t carry = 0;
        for (int byte = 0; byte < pitch; byte++) {
            unsigned ink = *source++;
            uint8_t mask = (ink >> shift) | carry;
            if (white) {
                target[byte] |= mask;
            } else {
                target[byte] &= (uint8_t)~mask;
            }
            carry = (uint8_t)(ink << (8 - shift));
        }
        if (carry) {
            if (white) {
                target[pitch] |= carry;
            } else {
                target[pitch] &= (uint8_t)~carry;
            }
        }
    }
}

static void clock_digits(uint8_t *frame, const char *time, int y, const uint8_t centers[4],
                         const digit_glyph_t *glyphs, const uint8_t *pixels, bool white)
{
    static const uint8_t positions[] = {0, 1, 3, 4};
    for (unsigned i = 0; i < 4; i++) {
        unsigned number = (unsigned)(time[positions[i]] - '0');
        if (number <= 9) {
            digit(frame, centers[i], y, &glyphs[number], pixels, white);
        }
    }
}

static char decimal_digit(unsigned value)
{
    return (char)('0' + value);
}

static void year_number(char text[5], unsigned year)
{
    text[0] = decimal_digit(year / 1000 % 10);
    text[1] = decimal_digit(year / 100 % 10);
    text[2] = decimal_digit(year / 10 % 10);
    text[3] = decimal_digit(year % 10);
    text[4] = '\0';
}

static void battery_label(char text[5], int battery)
{
    unsigned level = (unsigned)battery;
    if (level > 100) {
        memcpy(text, "-", 2);
    } else if (level == 100) {
        memcpy(text, "100%", 5);
    } else if (level >= 10) {
        text[0] = decimal_digit(level / 10);
        text[1] = decimal_digit(level % 10);
        text[2] = '%';
        text[3] = '\0';
    } else {
        text[0] = decimal_digit(level);
        text[1] = '%';
        text[2] = '\0';
    }
}

static void gauge(uint8_t *frame, int x, int y, int width, int gap, int height,
                  int battery, bool white)
{
    int filled = battery >= 0 ? (battery + 10) / 20 : 0;
    for (int i = 0; i < 5; i++, x += width + gap) {
        if (white) {
            white_box(frame, x, y, width, height);
            if (i >= filled) {
                watch_fill(frame, x + 1, y + 1, width - 2, height - 2);
            }
        } else if (i < filled) {
            watch_fill(frame, x, y, width, height);
        } else {
            outline(frame, x, y, width, height);
        }
    }
}

static const char *period_label(const face_data_t *data)
{
    return data->format_12 ? data->period : "24H";
}

static void right_ink(uint8_t *frame, ui_font_t font, int right, int y,
                      const char *text, bool white)
{
    face_ink_text(frame, font, right - ui_text_width(font, text), y, text, white);
}

static void alarm_text(uint8_t *frame, int right, int y, const face_data_t *data, bool white)
{
    char label[16] = "ALARM OFF";
    if (data->alarm_on) {
        memcpy(label + 6, data->alarm, sizeof data->alarm);
    }
    right_ink(frame, UI_MONO_TINY, right, y, label, white);
}

static void step_count(uint8_t *frame, int x, int y, int width, ui_font_t large,
                       const face_data_t *data, bool white, bool align_right)
{
    const char *text = data->steps;
    char decimal[sizeof data->steps];
    ui_font_t font = large;
    if (ui_text_width(font, text) > width) {
        font = UI_MONO_TINY;
    }
    if (ui_text_width(font, text) > width) {
        // Keep every digit when the full uint32_t count leaves no grouping room.
        size_t used = 0;
        for (const char *p = text; *p; p++) {
            if (*p != ',') {
                decimal[used++] = *p;
            }
        }
        decimal[used] = '\0';
        text = decimal;
    }
    if (align_right) {
        right_ink(frame, font, x + width, y, text, white);
    } else {
        face_ink_text(frame, font, x, y, text, white);
    }
}

static void signal_box(uint8_t *frame, const face_data_t *data)
{
    static const uint8_t centers[] = {32, 71, 127, 166};
    char year[5], battery[5];
    memcpy(frame, signal_box_art, WATCH_FRAME_SIZE);
    year_number(year, data->year);
    battery_label(battery, data->battery);
    face_ink_text(frame, UI_MONO_TINY, 17, 19, data->date, true);
    right_ink(frame, UI_MONO_TINY, 181, 19, year, true);
    if (data->degraded) {
        face_ink_text(frame, UI_TINY, 98, 19, "Backup", true);
    }
    face_text_right(frame, UI_MONO_TINY, 183, 44, period_label(data));
    clock_digits(frame, data->time, 68, centers, digits52, pixels52, true);
    ui_text(frame, UI_MENU, 16, 154, battery);
    step_count(frame, 112, 154, 72, UI_MENU, data, false, false);
    alarm_text(frame, 184, 179, data, false);
}

static void patchbay(uint8_t *frame, const face_data_t *data)
{
    static const uint8_t centers[] = {37, 74, 126, 163};
    char battery[5], mode[11] = "LOCAL ";
    unsigned start = 6;
    memcpy(frame, patchbay_art, WATCH_FRAME_SIZE);
    if (data->degraded) {
        memcpy(mode, "BACKUP ", 7);
        start = 7;
    }
    const char *period = period_label(data);
    for (unsigned i = 0; period[i]; i++) {
        mode[start + i] = period[i];
    }
    battery_label(battery, data->battery);
    face_ink_text(frame, UI_MONO_TINY, 20, 21, mode, true);
    right_ink(frame, UI_MONO_TINY, 179, 21, data->date, true);
    clock_digits(frame, data->time, 50, centers, digits59, pixels59, false);
    gauge(frame, 18, 143, 9, 3, 8, data->battery, true);
    face_ink_text(frame, UI_SMALL, 18, 155, battery, true);
    step_count(frame, 110, 145, 73, UI_TITLE, data, true, false);
    alarm_text(frame, 176, 180, data, true);
}

static const uint8_t game_digits[10][7] = {
    {14, 27, 27, 27, 27, 27, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 27, 3, 6, 12, 24, 31},   {30, 3, 3, 14, 3, 3, 30},
    {3, 7, 15, 27, 31, 3, 3},     {31, 24, 24, 30, 3, 3, 30},
    {14, 24, 24, 30, 27, 27, 14}, {31, 3, 3, 6, 6, 12, 12},
    {14, 27, 27, 14, 27, 27, 14}, {14, 27, 27, 15, 3, 3, 14},
};

static void console_digits(uint8_t *frame, const char *time)
{
    static const uint8_t xs[] = {24, 61, 109, 146};
    static const uint8_t positions[] = {0, 1, 3, 4};
    for (unsigned i = 0; i < 4; i++) {
        unsigned digit = (unsigned)(time[positions[i]] - '0');
        if (digit > 9) {
            continue;
        }
        for (unsigned row = 0; row < 7; row++) {
            uint8_t bits = game_digits[digit][row];
            for (unsigned column = 0; column < 5; column++) {
                if (bits & (16u >> column)) {
                    watch_fill(frame, xs[i] + column * 6, 66 + row * 6, 6, 6);
                }
            }
        }
    }
}

static void pocket_console(uint8_t *frame, const face_data_t *data)
{
    char battery[5];
    memcpy(frame, pocket_console_art, WATCH_FRAME_SIZE);
    battery_label(battery, data->battery);
    ui_text(frame, UI_MONO_TINY, 25, 35, data->date);
    if (data->degraded) {
        ui_text(frame, UI_TINY, 97, 35, "BK");
    }
    face_text_center(frame, UI_MONO_TINY, 126, 35, period_label(data));
    gauge(frame, 146, 35, 4, 2, 8, data->battery, false);
    console_digits(frame, data->time);
    ui_text(frame, UI_SMALL, 25, 141, battery);
    step_count(frame, 108, 141, 68, UI_SMALL, data, false, false);
    alarm_text(frame, 179, 166, data, false);
}

static void filing_card(uint8_t *frame, const face_data_t *data)
{
    static const char *const weekdays[] = {
        "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY",
    };
    static const uint8_t centers[] = {45, 80, 128, 163};
    char battery[5];
    memcpy(frame, filing_card_art, WATCH_FRAME_SIZE);
    battery_label(battery, data->battery);
    const char *calendar = data->date + 4;
    face_ink_text(frame, UI_MONO_TINY, 149 - ui_text_width(UI_MONO_TINY, calendar) / 2,
                  21, calendar, true);
    ui_text(frame, UI_MONO_TINY, 32, 36, data->degraded ? "BACKUP CLOCK" : "LOCAL TIME");
    ui_text(frame, UI_MONO_TINY, 32, 47, period_label(data));
    face_text_right(frame, UI_MONO_TINY, 173, 44, weekdays[data->weekday % 7]);
    clock_digits(frame, data->time, 71, centers, digits55, pixels55, false);
    face_text_right(frame, UI_SMALL, 173, 130, battery);
    step_count(frame, 32, 154, 141, UI_SMALL, data, false, true);
    alarm_text(frame, 188, 189, data, false);
}

static void chart_table(uint8_t *frame, const face_data_t *data)
{
    static const uint8_t centers[] = {38, 76, 125, 162};
    char weekday[4], date[13], battery[5];
    memcpy(frame, chart_table_art, WATCH_FRAME_SIZE);
    memcpy(weekday, data->date, 3);
    weekday[3] = '\0';
    memcpy(date, data->date + 4, 6);
    date[6] = ' ';
    year_number(date + 7, data->year);
    battery_label(battery, data->battery);
    ui_text(frame, UI_TITLE, 17, 20, weekday);
    if (data->degraded) {
        ui_text(frame, UI_TINY, 104, 41, "BK");
    }
    face_text_center(frame, UI_MONO_TINY, 97, 24, period_label(data));
    ui_text(frame, UI_MONO_TINY, 18, 41, date);
    clock_digits(frame, data->time, 68, centers, digits64, pixels64, false);
    ui_text(frame, UI_SMALL, 18, 154, battery);
    gauge(frame, 53, 156, 7, 1, 8, data->battery, false);
    step_count(frame, 109, 153, 72, UI_MENU, data, false, false);
    alarm_text(frame, 182, 179, data, false);
}

void face_max(uint8_t *frame, enum watch_face style, const face_data_t *data)
{
    switch (style) {
    case FACE_SIGNAL_BOX:
        signal_box(frame, data);
        break;
    case FACE_PATCHBAY:
        patchbay(frame, data);
        break;
    case FACE_POCKET_CONSOLE:
        pocket_console(frame, data);
        break;
    case FACE_FILING_CARD:
        filing_card(frame, data);
        break;
    case FACE_CHART_TABLE:
        chart_table(frame, data);
        break;
    default:
        break;
    }
}
