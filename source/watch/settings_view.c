#include "settings_view.h"

#include "settings.h"
#include "ui_font.h"

enum { PAGE_ROWS = 6, CONTENT_LEFT = 22, CONTENT_RIGHT = 187, SCROLL_TOP = 50, SCROLL_HEIGHT = 107 };

static void text_right(uint8_t *frame, ui_font_t font, int right, int y, const char *text)
{
    ui_text(frame, font, right - ui_text_width(font, text), y, text);
}

static void chrome(uint8_t *frame, const settings_list_t *list)
{
    ui_text(frame, UI_TINY, 11, 5, "< Back");
    text_right(frame, UI_TINY, 189, 5, "↑ Up");
    ui_font_t title_font = ui_text_width(UI_TITLE, list->title) <= 178 ? UI_TITLE : UI_MENU;
    ui_text(frame, title_font, 11, 23, list->title);
    watch_fill(frame, 11, 44, 178, 1);
    watch_fill(frame, 11, 176, 178, 1);
    ui_text(frame, UI_SMALL, 11, 184, "Select");
    text_right(frame, UI_TINY, 189, 184, "↓ Down");
}

static void check_mark(uint8_t *frame, int y)
{
    static const uint8_t rising[] = {4, 5, 6, 5, 4, 3, 2, 1, 0};
    for (unsigned i = 0; i < sizeof rising; i++) {
        watch_fill(frame, 174 + (int)i, y + rising[i], 2, 2);
    }
}

static void toggle_thumb(uint8_t *frame, int y, bool enabled)
{
    watch_fill(frame, 159, y, 26, 1);
    watch_fill(frame, 159, y + 11, 26, 1);
    watch_fill(frame, 156, y + 3, 1, 6);
    watch_fill(frame, 187, y + 3, 1, 6);
    watch_fill(frame, 157, y + 1, 2, 2);
    watch_fill(frame, 185, y + 1, 2, 2);
    watch_fill(frame, 157, y + 9, 2, 2);
    watch_fill(frame, 185, y + 9, 2, 2);
    watch_fill(frame, enabled ? 177 : 160, y + 3, 6, 6);
}

static void draw_row(uint8_t *frame, int y, const settings_row_t *row, bool compact,
                     bool focused, bool checked)
{
    ui_font_t label_font = compact ? UI_BODY : UI_MENU;
    ui_font_t value_font = UI_SMALL;
    int value_right = CONTENT_RIGHT;
    const char *value = row->value;
    if (row->toggle != SETTINGS_NO_TOGGLE) {
        bool enabled = row->toggle == SETTINGS_ON;
        toggle_thumb(frame, y, enabled);
        value_right = 148;
        if (!value[0]) {
            value = enabled ? "On" : "Off";
        }
    } else if (checked) {
        check_mark(frame, y + 3);
        value_right = 166;
    }
    int room = value_right - CONTENT_LEFT - (value[0] ? 5 : 0);
    int label_width = ui_text_width(label_font, row->label);
    if (label_width + ui_text_width(value_font, value) > room) {
        value_font = UI_TINY;
        int value_width = ui_text_width(value_font, value);
        if (label_width + value_width > room) {
            label_font = UI_SMALL;
            if (ui_text_width(label_font, row->label) + value_width > room) {
                label_font = UI_TINY;
            }
        }
    }
    ui_text(frame, label_font, CONTENT_LEFT, y, row->label);
    if (value[0]) {
        text_right(frame, value_font, value_right, y + 2, value);
    }
    if (focused) {
        watch_fill(frame, 12, y - 1, 3, 14);
        watch_fill(frame, 11, y, 5, 12);
    }
}

static unsigned page_start(const settings_list_t *list)
{
    if (list->count <= PAGE_ROWS) {
        return 0;
    }
    unsigned first = list->focus - list->focus % PAGE_ROWS;
    unsigned last = list->count - PAGE_ROWS;
    return first < last ? first : last;
}

static void scroll_track(uint8_t *frame, unsigned first, unsigned count)
{
    unsigned offset = first * SCROLL_HEIGHT / count;
    unsigned height = PAGE_ROWS * SCROLL_HEIGHT / count;
    watch_fill(frame, 194, SCROLL_TOP, 1, SCROLL_HEIGHT);
    watch_fill(frame, 193, SCROLL_TOP + (int)offset, 3, height ? (int)height : 1);
}

void watch_render_settings(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE])
{
    settings_list_t list;
    watch_settings_list(watch, &list);
    chrome(frame, &list);

    unsigned first = page_start(&list);
    bool compact = list.count > 5;
    for (unsigned row = 0; row < PAGE_ROWS && first + row < list.count; row++) {
        unsigned index = first + row;
        settings_row_t item;
        watch_settings_row(watch, index, &item);
        draw_row(frame, 53 + (int)row * (compact ? 18 : 23), &item, compact,
                 index == list.focus, (int)index == list.checked);
    }
    if (list.count > PAGE_ROWS) {
        scroll_track(frame, first, list.count);
    }
    if (list.hint[0]) {
        ui_text(frame, UI_TINY, 13, 163, list.hint);
    }
}
