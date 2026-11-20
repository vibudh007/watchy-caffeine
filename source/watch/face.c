#include "face.h"

#include <stdlib.h>
#include <string.h>

#include "face_data.h"
#include "face_art.h"
#include "face_exp.h"
#include "face_max.h"

static const char *const weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

typedef struct {
    int x, y;
} point_t;

static void stroke(uint8_t *frame, point_t from, point_t to, int width)
{
    int dx = abs(to.x - from.x), dy = -abs(to.y - from.y);
    int x_step = from.x < to.x ? 1 : -1, y_step = from.y < to.y ? 1 : -1;
    int error = dx + dy;
    int w = dx < -dy ? width : 1, h = dx < -dy ? 1 : width;
    for (;;) {
        watch_fill(frame, from.x - w / 2, from.y - h / 2, w, h);
        if (from.x == to.x && from.y == to.y) {
            return;
        }
        int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            from.x += x_step;
        }
        if (doubled <= dx) {
            error += dx;
            from.y += y_step;
        }
    }
}

static void disk(uint8_t *frame, point_t center, int radius)
{
    for (int y = -radius; y <= radius; y++) {
        int x = radius;
        while (x * x + y * y > radius * radius) {
            x--;
        }
        watch_fill(frame, center.x - x, center.y + y, 2 * x + 1, 1);
    }
}

void face_text_right(uint8_t *frame, ui_font_t font, int right, int y, const char *label)
{
    ui_text(frame, font, right - ui_text_width(font, label), y, label);
}

void face_ink_text(uint8_t *frame, ui_font_t font, int x, int y, const char *text, bool white)
{
    ui_text_color(frame, font, x, y, text, white);
}

void face_text_center(uint8_t *frame, ui_font_t font, int center, int y, const char *text)
{
    ui_text(frame, font, center - ui_text_width(font, text) / 2, y, text);
}

static void pixel(uint8_t *frame, int x, int y, bool white)
{
    uint8_t *byte = frame + y * WATCH_STRIDE + x / 8;
    uint8_t mask = (uint8_t)(0x80u >> (x % 8));
    *byte = white ? *byte | mask : *byte & (uint8_t)~mask;
}

void face_alarm(uint8_t *frame, int right, int y, const face_data_t *data, bool white)
{
    static const uint8_t bell[] = {0x18, 0x3c, 0x24, 0x24, 0x24, 0x42, 0x7e, 0x00, 0x18};
    int x = right - ui_text_width(UI_TINY, data->alarm);
    face_ink_text(frame, UI_TINY, x, y, data->alarm, white);
    if (x < 11 || x > WATCH_WIDTH || y < 0 || y + (int)sizeof bell > WATCH_HEIGHT) {
        return;
    }
    for (unsigned row = 0; row < sizeof bell; row++) {
        for (unsigned col = 0; col < 8; col++) {
            if ((bell[row] & (0x80u >> col)) || (!data->alarm_on && row == 8 - col)) {
                pixel(frame, x - 11 + (int)col, y + (int)row, white);
            }
        }
    }
}

static void menu_hint(uint8_t *frame, int y, ui_font_t font, const char *label)
{
    stroke(frame, (point_t){13, y + 5}, (point_t){17, y}, 2);
    stroke(frame, (point_t){13, y + 5}, (point_t){17, y + 10}, 2);
    ui_text(frame, font, 24, y, label);
}

static void date_line(uint8_t *frame, const watch_date_t *date, const char *weekday,
                      int y, ui_font_t font, bool right)
{
    char label[11] = "DDD 00 MMM";
    memcpy(label, weekday, 3);
    label[4] = (char)('0' + date->day / 10);
    label[5] = (char)('0' + date->day % 10);
    memcpy(label + 7, months[date->month - 1], 3);
    if (right) {
        face_text_right(frame, font, 186, y, label);
    } else {
        ui_text(frame, font, 13, y, label);
    }
}

static void digits(uint8_t *frame, int value, int tens, int units, int y, bool large)
{
    ui_clock_digit(frame, (char)('0' + value / 10), tens, y, large);
    ui_clock_digit(frame, (char)('0' + value % 10), units, y, large);
}

static void battery(uint8_t *frame, int x, int y, unsigned level)
{
    watch_fill(frame, x + 1, y, 19, 2);
    watch_fill(frame, x, y + 1, 2, 10);
    watch_fill(frame, x + 19, y + 1, 2, 10);
    watch_fill(frame, x + 1, y + 10, 19, 2);
    watch_fill(frame, x + 22, y + 3, 2, 6);
    watch_fill(frame, x + 4, y + 4, (level * 14 + 50) / 100, 4);
}

static void steps(uint8_t *frame, uint32_t count)
{
    char label[14];
    watch_count_text(label, count);
    int width = ui_text_width(UI_MENU, label);
    if (width <= 89) {
        face_text_right(frame, UI_MENU, 148, 169, label);
        face_text_right(frame, UI_SMALL, 186, 172, "steps");
    } else {
        face_text_right(frame, UI_SMALL, 186, 166, label);
        face_text_right(frame, UI_TINY, 186, 181, "steps");
    }
}

static void ledger(const watch_t *watch, uint8_t *frame, const watch_date_t *date,
                   const char *weekday, int hour, const char *period)
{
    date_line(frame, date, weekday, 17, UI_MENU, false);
    if (watch->battery >= 0 && watch->battery <= 100) {
        battery(frame, 164, 18, watch->battery);
    }
    watch_fill(frame, 13, 43, 174, 1);
    digits(frame, hour, 31, 73, 70, false);
    digits(frame, date->minute, 127, 169, 70, false);
    disk(frame, (point_t){100, 88}, 3);
    disk(frame, (point_t){100, 112}, 3);
    if (watch->degraded) {
        ui_text(frame, UI_BODY, 13, 135, "Backup clock");
    }
    if (period) {
        face_text_right(frame, UI_BODY, 187, 135, period);
    }
    watch_fill(frame, 13, 154, 174, 1);
    if (watch->settings.step_mode != STEPS_OFF) {
        steps(frame, watch->steps);
    }
    menu_hint(frame, 172, UI_BODY, "Menu");
}

static void split(const watch_t *watch, uint8_t *frame, const watch_date_t *date,
                  const char *weekday, int hour, const char *period)
{
    watch_fill(frame, 135, 11, 2, 178);
    digits(frame, hour, 38, 97, 17, true);
    digits(frame, date->minute, 38, 97, 112, true);
    watch_fill(frame, 11, 99, 111, 2);
    disk(frame, (point_t){119, 91}, 2);
    disk(frame, (point_t){119, 107}, 2);
    ui_text(frame, UI_SMALL, 149, 16, weekday);
    char day[] = {(char)('0' + date->day / 10), (char)('0' + date->day % 10), 0};
    ui_text(frame, UI_DATE, 148, 38, day);
    ui_text(frame, UI_SMALL, 149, 69, months[date->month - 1]);
    if (period) {
        ui_text(frame, UI_TINY, 149, 84, period);
    }
    watch_fill(frame, 149, 99, 40, 1);
    if (watch->battery >= 0 && watch->battery <= 100) {
        unsigned level = watch->battery;
        battery(frame, 149, 115, level);
        char percent[5] = "100%";
        if (level < 100) {
            percent[0] = (char)('0' + level / 10);
            percent[1] = (char)('0' + level % 10);
            percent[2] = '%';
            percent[3] = '\0';
        }
        ui_text(frame, UI_SMALL, 149, 137, percent + (level < 10));
    }
    if (watch->degraded) {
        ui_text(frame, UI_TINY, 149, 151, "Backup");
        ui_text(frame, UI_TINY, 149, 162, "clock");
    }
    menu_hint(frame, 186, UI_BODY, "Menu");
}

// Round 127 × sin(angle / 2 degrees) for the first quadrant. A fixed-point
// lookup keeps minute rendering independent of the FPU and its math library.
static const uint8_t sine_quadrant[] = {
    0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 63, 64, 65, 66,
    67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 76, 77, 78, 79, 80, 81,
    82, 82, 83, 84, 85, 86, 87, 87, 88, 89, 90, 91, 91, 92, 93, 94,
    94, 95, 96, 97, 97, 98, 99, 99, 100, 101, 101, 102, 103, 103, 104, 105,
    105, 106, 107, 107, 108, 108, 109, 109, 110, 111, 111, 112, 112, 113, 113, 114,
    114, 115, 115, 116, 116, 116, 117, 117, 118, 118, 119, 119, 119, 120, 120, 120,
    121, 121, 121, 122, 122, 122, 123, 123, 123, 123, 124, 124, 124, 124, 125, 125,
    125, 125, 125, 126, 126, 126, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127,
    127, 127, 127, 127, 127,
};

static int sine(unsigned angle)
{
    unsigned phase = angle < 360 ? angle : angle - 360;
    int value = sine_quadrant[phase <= 180 ? phase : 360 - phase];
    return angle < 360 ? value : -value;
}

static int project(int direction, int radius)
{
    int value = direction * radius;
    return (value + (value < 0 ? -63 : 63)) / 127;
}

static point_t dial_position(unsigned angle, int radius)
{
    int x = project(sine(angle), radius);
    int y = project(sine(angle < 540 ? angle + 180 : angle - 540), radius);
    return (point_t){100 + x, 82 - y};
}

static void hand(uint8_t *frame, unsigned angle, int radius, int width)
{
    point_t tip = dial_position(angle, radius);
    stroke(frame, dial_position(angle, -8), tip, width);
    disk(frame, tip, (width - 1) / 2);
}

static void dial(const watch_t *watch, uint8_t *frame, const watch_date_t *date,
                 const char *weekday, const char *period)
{
    static const char *const cardinals[] = {"12", "3", "6", "9"};
    for (unsigned angle = 0; angle < 720; angle += 12) {
        if (angle % 60) {
            point_t p = dial_position(angle, 70);
            watch_fill(frame, p.x, p.y, 1, 1);
        } else if (angle % 180) {
            stroke(frame, dial_position(angle, 62), dial_position(angle, 68), 2);
        } else {
            point_t p = dial_position(angle, 64);
            const char *label = cardinals[angle / 180];
            ui_text(frame, UI_MENU, p.x - ui_text_width(UI_MENU, label) / 2, p.y - 6, label);
        }
    }
    hand(frame, (date->hour % 12) * 60 + date->minute, 34, 7);
    hand(frame, date->minute * 12, 54, 4);
    disk(frame, (point_t){100, 82}, 5);
    watch_fill(frame, 13, 158, 174, 1);
    date_line(frame, date, weekday, 171, UI_BODY, true);
    if (period) {
        ui_text(frame, UI_TINY, 13, 186, period);
    }
    if (watch->degraded) {
        face_text_right(frame, UI_TINY, 186, 186, "Backup clock");
    }
    menu_hint(frame, 172, UI_BODY, "Menu");
}

static void charge(uint8_t *frame)
{
    watch_fill(frame, 16, 37, 43, 3);
    watch_fill(frame, 14, 39, 3, 21);
    watch_fill(frame, 58, 39, 3, 21);
    watch_fill(frame, 16, 59, 43, 3);
    watch_fill(frame, 62, 44, 5, 11);
    watch_fill(frame, 20, 43, 5, 13);
    ui_text(frame, UI_LARGE, 13, 78, "Charge");
    ui_text(frame, UI_LARGE, 13, 112, "watch");
    ui_text(frame, UI_MENU, 13, 166, "Connect USB");
}

static void clock_unset(uint8_t *frame)
{
    ui_text(frame, UI_LARGE, 13, 51, "Set the");
    ui_text(frame, UI_LARGE, 13, 84, "time");
    ui_text(frame, UI_BODY, 13, 126, "Clock needs setup");
    watch_fill(frame, 13, 154, 174, 1);
    menu_hint(frame, 172, UI_MENU, "Menu to begin");
}

static void clock_text(char *text, unsigned hour, unsigned minute)
{
    text[0] = (char)('0' + hour / 10);
    text[1] = (char)('0' + hour % 10);
    text[2] = ':';
    text[3] = (char)('0' + minute / 10);
    text[4] = (char)('0' + minute % 10);
    text[5] = '\0';
}

static void render_valid(const watch_t *watch, enum watch_face style, uint8_t *frame,
                         const watch_date_t *date, unsigned weekday)
{
    bool format_12 = watch->settings.format_12;
    const char *period = format_12 ? (date->hour < 12 ? "AM" : "PM") : NULL;
    unsigned hour = (unsigned)date->hour;
    if (format_12) {
        hour = (hour + 11) % 12 + 1;
    }
    switch (style) {
    case FACE_LEDGER:
        ledger(watch, frame, date, weekdays[weekday], hour, period);
        return;
    case FACE_SPLIT:
        split(watch, frame, date, weekdays[weekday], hour, period);
        return;
    case FACE_DIAL:
        dial(watch, frame, date, weekdays[weekday], period);
        return;
    default:
        break;
    }

    face_data_t data = {
        .hour = date->hour, .minute = date->minute, .day = date->day, .month = date->month,
        .year = date->year, .weekday = weekday, .display_hour = hour, .format_12 = format_12,
        .alarm_on = watch->settings.alarm_on, .battery = watch->battery, .degraded = watch->degraded,
        .steps_on = watch->settings.step_mode != STEPS_OFF, .steps_available = watch->steps_available,
        .date = "DDD 00 MMM", .steps = "-",
    };
    memcpy(data.date, weekdays[weekday], 3);
    data.date[4] = (char)('0' + date->day / 10);
    data.date[5] = (char)('0' + date->day % 10);
    memcpy(data.date + 7, months[date->month - 1], 3);
    clock_text(data.time, hour, date->minute);
    unsigned alarm_hour = watch->settings.alarm_min / 60;
    clock_text(data.alarm, format_12 ? (alarm_hour + 11) % 12 + 1 : alarm_hour, watch->settings.alarm_min % 60);
    if (format_12) {
        memcpy(data.period, period, sizeof data.period);
        memcpy(data.alarm + 5, alarm_hour < 12 ? " AM" : " PM", 4);
    }
    if (data.steps_on && data.steps_available) {
        watch_count_text(data.steps, watch->steps);
    }
    if (style < FACE_WOODCUT_TIDE) {
        face_max(frame, style, &data);
    } else if (style < FACE_MINUTE_ATLAS) {
        face_art(frame, style, &data);
    } else {
        face_exp(frame, style, &data);
    }
}

static unsigned local_weekday(const watch_t *watch)
{
    uint32_t local = (uint32_t)((int64_t)watch->utc + watch->settings.offset_min * 60);
    return (local / 86400 + 4) % 7;
}

void watch_render_face(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE])
{
    if (watch->battery == 0) {
        charge(frame);
        return;
    }
    watch_date_t date;
    if (!watch->clock_valid || !watch_decode_date(watch->utc, watch->settings.offset_min, &date)) {
        clock_unset(frame);
        return;
    }
    render_valid(watch, watch->settings.face, frame, &date, local_weekday(watch));
}

const char *watch_face_name(enum watch_face style)
{
    static const char *const names[FACE_COUNT] = {
        "Ledger", "Split", "Dial", "Signal Box", "Patchbay", "Pocket Console", "Filing Card",
        "Chart Table", "Woodcut Tide", "Concrete Study", "Blockprint Iris", "Ink Observatory",
        "Film Leader", "Minute Atlas", "Quarter Dial", "Letterpress Time", "Shortwave", "Domino",
    };
    return (unsigned)style < FACE_COUNT ? names[style] : names[FACE_LEDGER];
}

static void preview(uint8_t *frame, const watch_t *sample, const watch_date_t *date, unsigned weekday,
                    enum watch_face style, int x, int y, unsigned width, unsigned size, unsigned start)
{
    // Runtime stack stays small. Only the synchronous carousel renderer uses this buffer.
    static uint8_t source[WATCH_FRAME_SIZE];
    memset(source, 0xff, sizeof source);
    render_valid(sample, style, source, date, weekday);
    for (unsigned dy = 0; dy < size; dy++) {
        const uint8_t *row = source + (dy * WATCH_HEIGHT / size) * WATCH_STRIDE;
        for (unsigned dx = 0; dx < width; dx++) {
            unsigned sx = (dx + start) * WATCH_WIDTH / size;
            if (!(row[sx / 8] & (0x80u >> (sx % 8)))) {
                pixel(frame, x + (int)dx, y + (int)dy, false);
            }
        }
    }
}

static void selected_badge(uint8_t *frame)
{
    watch_fill(frame, 156, 153, 16, 16);
    for (int i = 0; i < 9; i++) {
        int y = i < 3 ? 159 + i : 165 - i;
        pixel(frame, 159 + i, y, true);
        pixel(frame, 159 + i, y + 1, true);
    }
}

void watch_render_chooser(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE])
{
    watch_date_t date;
    unsigned weekday;
    if (watch->clock_valid && watch_decode_date(watch->utc, watch->settings.offset_min, &date)) {
        weekday = local_weekday(watch);
    } else {
        date = (watch_date_t){2026, 11, 19, 22, 28, 0};
        weekday = 4;
    }
    enum watch_face style = watch->face_focus;
    preview(frame, watch, &date, weekday, watch_face_neighbor(style, -1), 0, 38, 20, 122, 102);
    preview(frame, watch, &date, weekday, watch_face_neighbor(style, 1), 180, 38, 20, 122, 0);
    preview(frame, watch, &date, weekday, style, 28, 26, 144, 144, 0);
    watch_fill(frame, 20, 37, 1, 124);
    watch_fill(frame, 179, 37, 1, 124);
    watch_fill(frame, 26, 24, 148, 1);
    watch_fill(frame, 26, 171, 148, 1);
    watch_fill(frame, 26, 24, 1, 148);
    watch_fill(frame, 173, 24, 1, 148);
    if (style == watch->settings.face) {
        selected_badge(frame);
    }
    face_text_center(frame, UI_SMALL, 100, 175, watch_face_name(style));
    ui_text(frame, UI_SMALL, 12, 5, "Back");
    face_text_right(frame, UI_SMALL, 188, 5, "↑ Previous");
    ui_text(frame, UI_SMALL, 12, 188, "Menu Select");
    face_text_right(frame, UI_SMALL, 188, 188, "↓ Next");
}
