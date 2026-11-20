#include "face_art.h"

#include <string.h>

typedef struct {
    uint16_t offset;
    uint8_t width, height;
} art_digit_t;

#include "face_art_data.inc"

_Static_assert(sizeof woodcut_tide_base == WATCH_FRAME_SIZE &&
               sizeof concrete_study_base == WATCH_FRAME_SIZE &&
               sizeof blockprint_iris_base == WATCH_FRAME_SIZE &&
               sizeof ink_observatory_base == WATCH_FRAME_SIZE &&
               sizeof film_leader_base == WATCH_FRAME_SIZE, "Artwork must match the panel");

static void ink_rect(uint8_t *frame, int x, int y, int width, int height, bool white)
{
    if (!white) {
        watch_fill(frame, x, y, width, height);
        return;
    }
    // The white artwork rectangles are entirely on-screen.
    int first = x / 8;
    int last = (x + width - 1) / 8;
    uint8_t start = 0xffu >> (x % 8);
    uint8_t end = 0xffu << (7 - (x + width - 1) % 8);
    for (int row = y; row < y + height; row++) {
        uint8_t *line = frame + row * WATCH_STRIDE;
        if (first == last) {
            line[first] |= start & end;
        } else {
            line[first] |= start;
            memset(line + first + 1, 0xff, last - first - 1);
            line[last] |= end;
        }
    }
}

static void right_text(uint8_t *frame, ui_font_t font, int right, int y, const char *text, bool white)
{
    ui_text_color(frame, font, right - ui_text_width(font, text), y, text, white);
}

static void center_text(uint8_t *frame, ui_font_t font, int center, int y, const char *text, bool white)
{
    ui_text_color(frame, font, center - ui_text_width(font, text) / 2, y, text, white);
}

static void draw_clock(uint8_t *frame, const char *time, const int centers[4], int y,
                       const art_digit_t *digits, const uint8_t *bits, unsigned white_mask)
{
    for (unsigned i = 0; i < 4; i++) {
        unsigned digit = (unsigned)(time[i + (i >= 2)] - '0');
        if (digit < 10) {
            const art_digit_t *glyph = digits + digit;
            ui_bitmap_ink(frame, bits + glyph->offset, centers[i] - glyph->width / 2, y,
                          glyph->width, glyph->height, (white_mask >> i) & 1u);
        }
    }
}

static void round_colon(uint8_t *frame, int top, int separation, bool white)
{
    static const uint8_t dot[] = {0x78, 0xfc, 0xfc, 0xfc, 0xfc, 0x78};
    ui_bitmap_ink(frame, dot, 97, top, 6, 6, white);
    ui_bitmap_ink(frame, dot, 97, top + separation, 6, 6, white);
}

static void alarm_tag(uint8_t *frame, int right, int y, const face_data_t *data, bool white, bool plaque)
{
    int label_width = ui_text_width(UI_MONO_TINY, data->alarm);
    int left = right - label_width - 20;
    if (plaque) {
        int width = right - left + 1;
        ink_rect(frame, left, y, width, 17, white);
        ink_rect(frame, left + 1, y + 1, width - 2, 15, !white);
    }
    ui_text_color(frame, UI_MONO_TINY, left + 2, y + 4, data->alarm, white);
    ui_bitmap_ink(frame, alarm_ink, right - 16, y + 2, 15, 13, white);
    if (!data->alarm_on) {
        for (int x = 0; x < 13; x++) {
            ink_rect(frame, right - 15 + x, y + 14 - x, 1, 1, white);
        }
    }
}

static void charge_label(char label[5], int level)
{
    if (level < 0 || level > 100) {
        memcpy(label, "--%", sizeof "--%");
    } else if (level == 100) {
        memcpy(label, "100%", sizeof "100%");
    } else if (level < 10) {
        label[0] = '0' + level;
        memcpy(label + 1, "%", sizeof "%");
    } else {
        label[0] = '0' + level / 10;
        label[1] = '0' + level % 10;
        memcpy(label + 2, "%", sizeof "%");
    }
}

enum battery_style { BATTERY_BLOCKS, BATTERY_PETALS, BATTERY_WAVE };

static void battery_icon(uint8_t *frame, int x, int y, int level, enum battery_style style)
{
    watch_fill(frame, x + 1, y, 18, 1);
    watch_fill(frame, x + 1, y + 8, 18, 1);
    watch_fill(frame, x, y + 1, 1, 7);
    watch_fill(frame, x + 19, y + 1, 1, 7);
    watch_fill(frame, x + 20, y + 2, 2, 5);
    if (style == BATTERY_WAVE) {
        static const uint8_t crest[] = {4, 4, 3, 2, 3, 3, 4, 3, 3, 2, 3, 3, 4, 3, 2};
        unsigned filled = level >= 0 && level <= 100 ? (level * sizeof crest + 50) / 100 : 0;
        for (unsigned i = 0; i < filled; i++) {
            watch_fill(frame, x + 2 + i, y + crest[i], 1, 2);
        }
        return;
    }
    for (int i = 0; i < 3; i++) {
        if (level >= 17 + i * 33) {
            if (style == BATTERY_PETALS) {
                watch_fill(frame, x + 4 + i * 5, y + 2, 1, 5);
                watch_fill(frame, x + 3 + i * 5, y + 4, 3, 1);
            } else {
                watch_fill(frame, x + 3 + i * 5, y + 2, 3, 5);
            }
        }
    }
}

static void battery_ring(uint8_t *frame, int center_x, int center_y, int level)
{
    if (level < 0 || level > 100) {
        return;
    }
    enum { DIAMETER = 23, STRIDE = (DIAMETER + 7) / 8 };
    uint8_t pixels[DIAMETER * STRIDE] = {0};
    for (unsigned y = 0; y < DIAMETER; y++) {
        for (unsigned x = 0; x < DIAMETER; x++) {
            if (ring_charge[y * DIAMETER + x] <= level) {
                pixels[y * STRIDE + x / 8] |= 0x80u >> (x % 8);
            }
        }
    }
    ui_bitmap_ink(frame, pixels, center_x - 11, center_y - 11, DIAMETER, DIAMETER, true);
    if (level <= 15) {
        ink_rect(frame, center_x - 1, center_y - 5, 3, 7, true);
        ink_rect(frame, center_x - 1, center_y + 3, 3, 2, true);
    }
}

static void step_status(uint8_t *frame, int left, int right, int y, const face_data_t *data,
                        bool white, bool align_right)
{
    char label[20];
    size_t length = strlen(data->steps);
    memcpy(label, data->steps, length);
    memcpy(label + length, " STEPS", sizeof " STEPS");
    if (ui_text_width(UI_MONO_TINY, label) > right - left) {
        // Drop grouping commas before dropping the units in the tightest styles.
        char *out = label;
        for (const char *in = data->steps; *in; in++) {
            if (*in != ',') {
                *out++ = *in;
            }
        }
        memcpy(out, " STEPS", sizeof " STEPS");
        if (ui_text_width(UI_MONO_TINY, label) > right - left) {
            *out = '\0';
        }
    }
    if (align_right) {
        right_text(frame, UI_MONO_TINY, right, y, label, white);
    } else {
        ui_text_color(frame, UI_MONO_TINY, left, y, label, white);
    }
}

static void woodcut_tide(uint8_t *frame, const face_data_t *data, const char *charge)
{
    memcpy(frame, woodcut_tide_base, WATCH_FRAME_SIZE);
    ui_text(frame, UI_MONO_TINY, 14, 23, data->date);
    if (data->degraded) {
        ui_text(frame, UI_TINY, 110, 23, "BK");
    }
    draw_clock(frame, data->time, (const int[]){32, 71, 129, 168}, 69, barlow_61, barlow_61_bits, 0);
    round_colon(frame, 80, 16, false);
    step_status(frame, 14, 96, 120, data, false, false);
    center_text(frame, UI_TINY, 110, 120, data->period, false);
    battery_icon(frame, 136, 121, data->battery, BATTERY_WAVE);
    right_text(frame, UI_MONO_TINY, 187, 120, charge, false);
    alarm_tag(frame, 193, 175, data, true, true);
}

static void concrete_study(uint8_t *frame, const face_data_t *data, const char *charge)
{
    memcpy(frame, concrete_study_base, WATCH_FRAME_SIZE);
    if (data->degraded) {
        ui_text(frame, UI_TINY, 98, 18, "BK");
    }
    draw_clock(frame, data->time, (const int[]){35, 77, 134, 177}, 75, barlow_68, barlow_68_bits, 12);
    watch_fill(frame, 103, 89, 5, 5);
    watch_fill(frame, 103, 111, 5, 5);
    center_text(frame, UI_TINY, 97, 128, data->period, false);
    char weekday[4] = {data->date[0], data->date[1], data->date[2], '\0'};
    ui_text(frame, UI_SMALL, 112, 150, weekday);
    ui_text(frame, UI_MONO_TINY, 112, 164, data->date + 4);
    int width = ui_text_width(UI_MONO_SMALL, data->steps);
    if (width <= 51) {
        ui_text_color(frame, UI_MONO_SMALL, 9, 155, data->steps, true);
    } else {
        size_t length = strlen(data->steps);
        size_t split = (length + 1) / 2;
        size_t comma = split;
        while (comma && data->steps[comma - 1] != ',') {
            comma--;
        }
        if (comma >= 4) {
            split = comma;
        }
        char upper[8] = {0};
        memcpy(upper, data->steps, split);
        ui_text_color(frame, UI_MONO_TINY, 9, 151, upper, true);
        ui_text_color(frame, UI_MONO_TINY, 9, 163, data->steps + split, true);
    }
    ui_text_color(frame, UI_MONO_TINY, 9, 179, "STEPS", true);
    battery_icon(frame, 77, 154, data->battery, BATTERY_BLOCKS);
    center_text(frame, UI_MONO_TINY, 89, 172, charge, false);
    alarm_tag(frame, 193, 177, data, false, true);
}

static void blockprint_iris(uint8_t *frame, const face_data_t *data, const char *charge)
{
    memcpy(frame, blockprint_iris_base, WATCH_FRAME_SIZE);
    step_status(frame, 28, 125, 51, data, false, false);
    battery_icon(frame, 129, 51, data->battery, BATTERY_PETALS);
    right_text(frame, UI_MONO_TINY, 177, 51, charge, false);
    if (data->degraded) {
        center_text(frame, UI_TINY, 100, 67, "BK", false);
    }
    draw_clock(frame, data->time, (const int[]){44, 77, 123, 156}, 78, fraunces_50, fraunces_50_bits, 0);
    round_colon(frame, 87, 13, false);
    center_text(frame, UI_TINY, 100, 117, data->period, false);
    center_text(frame, UI_MONO_TINY, 100, 136, data->date, false);
    alarm_tag(frame, 188, 171, data, false, true);
}

static void ink_observatory(uint8_t *frame, const face_data_t *data, const char *charge)
{
    memcpy(frame, ink_observatory_base, WATCH_FRAME_SIZE);
    battery_ring(frame, 44, 60, data->battery);
    ui_text_color(frame, UI_MONO_TINY, 59, 61, charge, true);
    step_status(frame, 89, 164, 61, data, true, true);
    draw_clock(frame, data->time, (const int[]){32, 72, 128, 168}, 76, barlow_61, barlow_61_bits, 15);
    round_colon(frame, 88, 16, true);
    center_text(frame, UI_MONO_TINY, 100, 128, data->date, true);
    center_text(frame, UI_TINY, 100, 144, data->period, true);
    if (data->degraded) {
        ui_text_color(frame, UI_TINY, 119, 144, "BK", true);
    }
    alarm_tag(frame, 192, 175, data, true, true);
}

static void film_leader(uint8_t *frame, const face_data_t *data, const char *charge)
{
    memcpy(frame, film_leader_base, WATCH_FRAME_SIZE);
    if (data->degraded) {
        center_text(frame, UI_TINY, 100, 4, "BK", false);
    }
    draw_clock(frame, data->time, (const int[]){48, 78, 122, 152}, 55,
               geist_mono_47, geist_mono_47_bits, 0);
    round_colon(frame, 64, 12, false);
    center_text(frame, UI_TINY, 100, 98, data->period, false);
    step_status(frame, 35, 114, 115, data, false, false);
    battery_icon(frame, 115, 116, data->battery, BATTERY_BLOCKS);
    right_text(frame, UI_MONO_TINY, 163, 115, charge, false);
    ui_text(frame, UI_MONO_TINY, 43, 155, data->date);
    alarm_tag(frame, 167, 174, data, false, false);
}

void face_art(uint8_t frame[WATCH_FRAME_SIZE], enum watch_face style, const face_data_t *data)
{
    char charge[5];
    charge_label(charge, data->battery);
    switch (style) {
    case FACE_WOODCUT_TIDE:
        woodcut_tide(frame, data, charge);
        break;
    case FACE_CONCRETE_STUDY:
        concrete_study(frame, data, charge);
        break;
    case FACE_BLOCKPRINT_IRIS:
        blockprint_iris(frame, data, charge);
        break;
    case FACE_INK_OBSERVATORY:
        ink_observatory(frame, data, charge);
        break;
    case FACE_FILM_LEADER:
        film_leader(frame, data, charge);
        break;
    default:
        break;
    }
}
