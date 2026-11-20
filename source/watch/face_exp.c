#include "face_exp.h"

#include <string.h>

#include "frame.h"

typedef struct {
    uint16_t offset;
    uint8_t width, height;
} exp_mask_t;

#include "face_exp_assets.inc"

// All spans and masks in this module have fixed in-frame destination bounds.
static void paper_span(uint8_t *frame, int x, int y, int width)
{
    int right = x + width - 1;
    uint8_t *row = frame + y * WATCH_STRIDE;
    uint8_t first = (uint8_t)(0xffu >> (x % 8));
    uint8_t last = (uint8_t)(0xffu << (7 - right % 8));
    int start = x / 8, end = right / 8;
    if (start == end) {
        row[start] |= first & last;
    } else {
        row[start] |= first;
        memset(row + start + 1, 0xff, (size_t)(end - start - 1));
        row[end] |= last;
    }
}

static void stamp(uint8_t *frame, int x, int y, const exp_mask_t *mask, const uint8_t *pixels)
{
    ui_bitmap_ink(frame, pixels + mask->offset, x, y, mask->width, mask->height, false);
}

static void digits2(char text[3], unsigned value)
{
    text[0] = (char)('0' + value / 10);
    text[1] = (char)('0' + value % 10);
    text[2] = 0;
}

static void percentage(char text[5], int charge)
{
    if (charge < 0) {
        memcpy(text, "--", 3);
    } else if (charge == 100) {
        memcpy(text, "100%", 5);
    } else {
        unsigned level = (unsigned)charge;
        digits2(text, level);
        text[2] = '%';
        text[3] = 0;
        if (level < 10) {
            memmove(text, text + 1, 3);
        }
    }
}

static void panel_outline(uint8_t *frame, int x, int y, int width, int height)
{
    watch_fill(frame, x + 2, y, width - 4, 1);
    watch_fill(frame, x + 2, y + height - 1, width - 4, 1);
    watch_fill(frame, x, y + 2, 1, height - 4);
    watch_fill(frame, x + width - 1, y + 2, 1, height - 4);
    watch_fill(frame, x + 1, y + 1, 1, 1);
    watch_fill(frame, x + width - 2, y + 1, 1, 1);
    watch_fill(frame, x + 1, y + height - 2, 1, 1);
    watch_fill(frame, x + width - 2, y + height - 2, 1, 1);
}

static void ink_disk(uint8_t *frame, int x, int y, int radius)
{
    int edge = radius;
    for (int row = 0; row <= radius; row++) {
        while (edge * edge + row * row > radius * radius) {
            edge--;
        }
        watch_fill(frame, x - edge, y + row, edge * 2 + 1, 1);
        if (row) {
            watch_fill(frame, x - edge, y - row, edge * 2 + 1, 1);
        }
    }
}

static void steps_icon(uint8_t *frame, int x, int y)
{
    watch_fill(frame, x + 2, y + 3, 2, 4);
    watch_fill(frame, x + 1, y + 7, 2, 2);
    watch_fill(frame, x + 7, y + 1, 2, 4);
    watch_fill(frame, x + 6, y + 5, 2, 2);
}

static void menu_arrow(uint8_t *frame, int y, bool compact)
{
    for (int row = 0; row < 9; row++) {
        int x = 13 + (row < 4 ? 4 - row : row - 4);
        watch_fill(frame, x, y + row, 2, 1);
    }
    if (!compact) {
        ui_text(frame, UI_SMALL, 24, y, "Menu");
    }
}

static void footer(uint8_t *frame, int y, const face_data_t *data)
{
    int width = ui_text_width(UI_MONO_TINY, data->steps);
    bool compact = width > 47;
    menu_arrow(frame, y, compact);
    int left = compact ? 27 : 67;
    steps_icon(frame, left, y);
    ui_text(frame, UI_MONO_TINY, left + 12, y, data->steps);
    face_alarm(frame, 189, y, data, false);
}

static void rounded_meter(uint8_t *frame, int center, int y, int charge)
{
    char text[5];
    percentage(text, charge);
    for (int i = 0; i < 5; i++) {
        int x = center - 9 + i * 4;
        watch_fill(frame, x + 1, y, 1, 1);
        watch_fill(frame, x + 1, y + 7, 1, 1);
        watch_fill(frame, x, y + 1, 1, 6);
        watch_fill(frame, x + 2, y + 1, 1, 6);
        if (charge >= (i + 1) * 20) {
            watch_fill(frame, x + 1, y + 1, 1, 6);
        }
    }
    face_text_center(frame, UI_MONO_TINY, center, y + 10, text);
}

static void lobe(uint8_t *frame, int x, int y, unsigned mass)
{
    for (int i = 0; i < 13; i++) {
        int width = atlas_lobes[mass][i];
        if (width) {
            watch_fill(frame, x - width / 2, y + i - 6, width, 1);
        }
    }
}

static void solder(uint8_t *frame, int x, int y, unsigned fraction)
{
    lobe(frame, x, y, 10 - fraction);
    if (fraction) {
        lobe(frame, x, y + 19, fraction);
        for (int row = 0; row < 20; row++) {
            int width = atlas_necks[fraction - 1][row];
            watch_fill(frame, x - width / 2, y + row, width, 1);
        }
    }
}

static int atlas_column(unsigned hour)
{
    return 32 + 14 * (int)(hour % 12);
}

static void atlas_alias(uint8_t *frame, const face_data_t *data, unsigned fraction)
{
    bool outgoing = data->minute > 50;
    if (data->minute >= 10 && !outgoing) {
        return;
    }
    unsigned other = (data->hour + (outgoing ? 1 : 23)) % 24;
    lobe(frame, atlas_column(other), outgoing ? 54 : 168, outgoing ? fraction : 10 - fraction);
    if (other / 12 != data->hour / 12) {
        char hint[8];
        memcpy(hint, outgoing ? "NEXT " : "PREV ", 5);
        if (data->format_12) {
            memcpy(hint + 5, other < 12 ? "AM" : "PM", 3);
        } else {
            digits2(hint + 5, other);
        }
        face_text_right(frame, UI_MONO_TINY, 189, 21, hint);
    }
}

static void minute_atlas(uint8_t *frame, const face_data_t *data)
{
    ui_text(frame, UI_MONO_TINY, 12, 14, data->date);
    rounded_meter(frame, 102, 8, data->battery);
    const char *window = data->hour < 12 ? "00-11" : "12-23";
    face_text_right(frame, UI_MONO_TINY, 189, 10, data->format_12 ? data->period : window);
    if (data->degraded) {
        ui_text(frame, UI_MONO_TINY, 12, 23, "BK");
    }
    watch_fill(frame, 12, 30, 178, 2);
    stamp(frame, 26, 52, exp_atlas_masks, exp_atlas_pixels);

    for (unsigned i = 0; i < 12; i++) {
        char number[3];
        unsigned hour = data->format_12 ? (i ? i : 12) : i + (data->hour / 12) * 12;
        digits2(number, hour);
        const char *label = data->format_12 && hour < 10 ? number + 1 : number;
        face_text_center(frame, UI_MONO_TINY, atlas_column(i), 36, label);
    }
    for (unsigned i = 0; i < 7; i++) {
        char number[3];
        digits2(number, i * 10);
        face_text_right(frame, UI_MONO_TINY, 20, 49 + 19 * (int)i, number);
    }
    unsigned fraction = data->minute % 10;
    solder(frame, atlas_column(data->hour), 54 + 19 * (int)(data->minute / 10), fraction);
    atlas_alias(frame, data, fraction);
    watch_fill(frame, 12, 176, 178, 2);
    footer(frame, 183, data);
}

static void quarter_dot(uint8_t *frame, unsigned quarter, unsigned offset)
{
    int x = quarter_minute[offset][0], y = quarter_minute[offset][1];
    for (unsigned i = 0; i < quarter; i++) {
        int previous_x = x;
        x = -y;
        y = previous_x;
    }
    bool right = quarter < 2, top = (quarter == 0 || quarter == 3);
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int px = x + dx, py = y + dy;
            if (dx * dx + dy * dy <= 9 && (right ? px >= 0 : px <= 0) &&
                (top ? py <= 0 : py >= 0)) {
                paper_span(frame, 144 + px, 125 + py, 1);
            }
        }
    }
}

static void quarter_dial(uint8_t *frame, const face_data_t *data)
{
    char percent[5];
    percentage(percent, data->battery);
    int step_width = ui_text_width(UI_MONO_SMALL, data->steps);
    int step_left = 185 - step_width;
    bool compact = step_left < 118;
    ui_text(frame, compact ? UI_MONO_TINY : UI_MONO_SMALL, 13, 13, data->date);
    face_text_right(frame, UI_MONO_TINY, 87, 29, data->period);
    watch_fill(frame, 12, 33, 63, 2);
    if (compact) {
        step_width = ui_text_width(UI_MONO_TINY, data->steps);
        step_left = 185 - step_width;
        ui_text(frame, UI_MONO_TINY, step_left, 13, data->steps);
    } else {
        ui_text(frame, UI_MONO_SMALL, step_left, 13, data->steps);
    }
    steps_icon(frame, step_left - 12, 13);
    for (int i = 0; i < 10; i++) {
        for (int row = 0; row < 8; row++) {
            int width = data->battery >= (i + 1) * 10 ? 2 : 1;
            watch_fill(frame, 112 + i * 5 + (7 - row) / 3, 29 + row, width, 1);
        }
    }
    face_text_right(frame, UI_MONO_TINY, 185, 29, percent);
    if (data->degraded) {
        ui_text(frame, UI_MONO_TINY, 13, 168, "BK");
    }
    for (unsigned i = 0; i < 2; i++) {
        unsigned digit = i ? data->display_hour % 10 : data->display_hour / 10;
        const exp_mask_t *mask = exp_digits_masks + digit;
        stamp(frame, 13 + 37 * (int)i + (37 - mask->width) / 2, 42, mask, exp_digits_pixels);
    }

    unsigned quarter = data->minute / 15;
    bool top = quarter == 0 || quarter == 3;
    for (int row = 0; row < 48; row++) {
        int width = quarter_width[row];
        int left = quarter < 2 ? 144 : 144 - width;
        watch_fill(frame, left, top ? 125 - row : 125 + row, width + 1, 1);
    }
    stamp(frame, 88, 70, exp_quarter_masks, exp_quarter_pixels);
    static const char *const quarters[] = {"00", "15", "30", "45"};
    for (unsigned i = 0; i < 4; i++) {
        int right = (i < 2 ? 163 : 125) - ui_text_width(UI_MONO_TINY, quarters[i]) / 2;
        face_ink_text(frame, UI_MONO_TINY, right, i == 0 || i == 3 ? 99 : 138,
                      quarters[i], i == quarter);
    }
    quarter_dot(frame, quarter, data->minute % 15);
    menu_arrow(frame, 183, false);
    face_alarm(frame, 189, 183, data, false);
}

static void letterpress(uint8_t *frame, const face_data_t *data)
{
    char percent[5];
    percentage(percent, data->battery);
    ui_text(frame, UI_MONO_TINY, 13, 14, data->date);
    face_text_right(frame, UI_MONO_TINY, 186, 14, data->format_12 ? data->period : "24H");
    if (data->degraded) {
        ui_text(frame, UI_MONO_TINY, 13, 23, "BK");
    }
    panel_outline(frame, 105, 14, 23, 10);
    watch_fill(frame, 128, 16, 2, 6);
    if (data->battery > 0) {
        watch_fill(frame, 106, 15, (21 * data->battery + 50) / 100, 8);
    }
    ui_text(frame, UI_MONO_TINY, 133, 14, percent);
    watch_fill(frame, 13, 33, 174, 2);
    stamp(frame, 13, 42, exp_hours_masks + data->display_hour, exp_hours_pixels);

    unsigned minute = data->minute;
    unsigned units = minute % 10;
    if (minute < 20 && minute >= 10) {
        stamp(frame, 13, 107, exp_minutes_masks + minute, exp_minutes_pixels);
    } else if (!units) {
        unsigned word = minute ? 18 + minute / 10 : 0;
        stamp(frame, 13, 107, exp_minutes_masks + word, exp_minutes_pixels);
    } else {
        unsigned word = minute < 10 ? 24 : 18 + minute / 10;
        stamp(frame, 13, 99, exp_minutes_masks + word, exp_minutes_pixels);
        stamp(frame, 13, 133, exp_minutes_masks + units, exp_minutes_pixels);
    }
    watch_fill(frame, 13, 169, 174, 2);
    footer(frame, 181, data);
}

static void dial_scale(uint8_t *frame, int y, int absolute, int spacing, int period,
                       int major_division, int middle_division, int label_base)
{
    enum { LEFT = 19, RIGHT = 181, NEEDLE = 100 };
    int radius = (RIGHT - NEEDLE) / spacing;
    int unit = (absolute + period - radius) % period;
    for (int offset = -radius; offset <= radius; offset++, unit = (unit + 1) % period) {
        int x = NEEDLE + offset * spacing;
        bool major = unit % major_division == 0;
        int length = major ? 12 : unit % middle_division == 0 || spacing != 1 ? 7 : 4;
        watch_fill(frame, x, y + 19, major ? 2 : 1, length);
        if (major && x > LEFT + 8 && x < RIGHT - 8) {
            char label[3];
            unsigned number = label_base == 0 && period == 60 ? (unsigned)unit :
                              (unsigned)(unit / major_division + label_base);
            digits2(label, number);
            face_text_center(frame, UI_MONO_SMALL, x, y + 35, label);
        }
    }
    watch_fill(frame, LEFT, y + 19, RIGHT - LEFT + 1, 2);
    for (int row = 0; row < 14; row++) {
        int half = (13 - row) / 2;
        watch_fill(frame, NEEDLE - half, y + 3 + row, 2 * half + 1, 1);
    }
}

static void shortwave(uint8_t *frame, const face_data_t *data)
{
    panel_outline(frame, 10, 10, 179, 161);
    panel_outline(frame, 16, 15, 7, 7);
    watch_fill(frame, 17, 18, 5, 1);
    face_text_center(frame, UI_MONO_TINY, 100, 15, data->date);
    rounded_meter(frame, 167, 13, data->battery);
    watch_fill(frame, 17, 36, 165, 1);
    ui_text(frame, UI_MONO_TINY, 19, 45, "HR");
    face_text_right(frame, UI_MONO_TINY, 179, 45, data->format_12 ? data->period : "24H");
    if (data->degraded) {
        ui_text(frame, UI_MONO_TINY, 122, 45, "BK");
    }
    int hour = (int)data->display_hour - (data->format_12 ? 1 : 0);
    dial_scale(frame, 46, hour * 60 + (int)data->minute, 1,
               data->format_12 ? 720 : 1440, 60, 5, data->format_12 ? 1 : 0);
    watch_fill(frame, 18, 107, 163, 1);
    ui_text(frame, UI_MONO_TINY, 19, 119, "MIN");
    dial_scale(frame, 115, (int)data->minute, 9, 60, 5, 1, 0);
    footer(frame, 181, data);
}

static void pip_tile(uint8_t *frame, int x, unsigned digit)
{
    static const uint16_t pips[] = {0, 0x010, 0x101, 0x111, 0x145,
                                    0x155, 0x16d, 0x17d, 0x1ef, 0x1ff};
    for (int row = 0; row < 63; row++) {
        int inset = row < 4 ? 4 - row : row > 58 ? row - 58 : 0;
        paper_span(frame, x + inset, 70 + row, 36 - 2 * inset);
    }
    watch_fill(frame, x + 10, 126, 16, 1);
    uint16_t pattern = pips[digit];
    if (pattern) {
        for (unsigned i = 0; i < 9; i++, pattern >>= 1) {
            if (pattern & 1) {
                ink_disk(frame, x + 8 + 10 * (int)(i % 3), 84 + 13 * (int)(i / 3), 3);
            }
        }
    } else {
        ink_disk(frame, x + 18, 97, 4);
        for (int row = -2; row <= 2; row++) {
            paper_span(frame, x + (row ? 17 : 16), 97 + row, row ? 3 : 5);
        }
    }
}

static void domino(uint8_t *frame, const face_data_t *data)
{
    face_text_center(frame, UI_TITLE, 100, 13, data->date);
    char percent[5];
    percentage(percent, data->battery);
    int width = ui_text_width(UI_MONO_TINY, percent);
    int left = (WATCH_WIDTH - 28 - width) / 2;
    panel_outline(frame, left, 35, 21, 11);
    watch_fill(frame, left + 21, 38, 2, 5);
    for (int i = 0; i < data->battery / 30; i++) {
        watch_fill(frame, left + 2 + i * 6, 37, 5, 7);
    }
    ui_text(frame, UI_MONO_TINY, left + 28, 36, percent);
    face_text_center(frame, UI_MONO_TINY, 100, 51, data->period);
    if (data->degraded) {
        face_text_right(frame, UI_MONO_TINY, 189, 51, "BK");
    }
    for (int row = 0; row < 75; row++) {
        int inset = row < 6 ? 6 - row : row > 68 ? row - 68 : 0;
        watch_fill(frame, 7 + inset, 64 + row, 186 - 2 * inset, 1);
    }
    static const int positions[] = {12, 51, 113, 152};
    static const int characters[] = {0, 1, 3, 4};
    for (unsigned i = 0; i < 4; i++) {
        pip_tile(frame, positions[i], (unsigned)(data->time[characters[i]] - '0'));
    }
    for (int i = 0; i < 4; i++) {
        paper_span(frame, 98, 89 + i, 4);
        paper_span(frame, 98, 106 + i, 4);
    }
    panel_outline(frame, 12, 149, 176, 21);
    ui_font_t number_font = UI_MONO_SMALL;
    width = ui_text_width(number_font, data->steps);
    if (width > 106) {
        number_font = UI_MONO_TINY;
        width = ui_text_width(number_font, data->steps);
    }
    left = (200 - 58 - width) / 2;
    steps_icon(frame, left, 154);
    ui_text(frame, UI_SMALL, left + 15, 154, "Steps");
    ui_text(frame, number_font, left + 58, 152, data->steps);
    menu_arrow(frame, 182, false);
    face_alarm(frame, 189, 182, data, false);
}

void face_exp(uint8_t *frame, enum watch_face style, const face_data_t *data)
{
    switch (style) {
    case FACE_MINUTE_ATLAS:
        minute_atlas(frame, data);
        break;
    case FACE_QUARTER_DIAL:
        quarter_dial(frame, data);
        break;
    case FACE_LETTERPRESS_TIME:
        letterpress(frame, data);
        break;
    case FACE_SHORTWAVE:
        shortwave(frame, data);
        break;
    case FACE_DOMINO:
        domino(frame, data);
        break;
    default:
        break;
    }
}
