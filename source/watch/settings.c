#include "settings.h"

#include <stdio.h>
#include <string.h>
#include "face.h"

static const char *const step_modes[] = {"Off", "Daytime", "Always"};

static enum settings_page owner(const watch_t *w)
{
    for (unsigned i = w->settings_depth; i; i--) {
        enum settings_page page = w->settings_path[i - 1].page;
        if (page <= PREF_SYNC || page >= PREF_TIMERS) return page;
    }
    return PREF_ROOT;
}

static void show_owner(watch_t *w)
{
    static const uint8_t screens[] = {MENU, SETTINGS, STEPS, ALARM, SET_TIME, FACE_CHOOSER, SYNC_SCREEN};
    enum settings_page page = owner(w);
    if (!w->settings_depth) w->screen = FACE;
    else if (page == PREF_TIMERS) w->screen = MENU;
    else if (page == PREF_NIGHT) w->screen = NIGHT_SETTINGS;
    else if (page == PREF_TOOL) w->screen = w->settings_path[w->settings_depth - 1].arg;
    else w->screen = screens[page];
}

void settings_push(watch_t *w, enum settings_page page, unsigned focus, unsigned arg)
{
    if (w->settings_depth < WATCH_SETTINGS_DEPTH) {
        w->settings_path[w->settings_depth++] = (settings_frame_t){page, focus, arg};
        show_owner(w);
    }
}

void settings_finish_value(watch_t *w)
{
    w->settings_depth = w->settings_owner;
    show_owner(w);
}

void settings_read_manual(watch_t *w)
{
    if (!w->clock_valid || !watch_decode_date(w->utc, w->settings.offset_min, &w->edit)) {
        w->edit = (watch_date_t){2026, 1, 1, 0, 0, 0};
    }
    if (w->edit.year < 2000) w->edit = (watch_date_t){2000, 1, 1, 0, 0, 0};
    if (w->edit.year > 2099) w->edit = (watch_date_t){2099, 12, 31, 23, 59, 0};
}

void watch_settings_enter(watch_t *w)
{
    w->settings_depth = 0;
    settings_push(w, PREF_ROOT, 0, 0);
}

enum watch_action settings_store_action(const watch_t *w)
{
    // The fixed-size NVS record contains no padding.
    return memcmp(&w->settings, &w->pending, sizeof w->settings) ? ACTION_STORE_PREFS : ACTION_NONE;
}

void watch_settings_list(const watch_t *w, settings_list_t *list)
{
    const settings_frame_t *frame = &w->settings_path[w->settings_depth - 1];
    *list = (settings_list_t){.focus = frame->focus, .checked = -1};
    switch (frame->page) {
    case PREF_ROOT: list->title = "Menu"; list->count = 6; break;
    case PREF_TIMERS: list->title = "Timers"; list->count = 2; break;
    case PREF_CLOCK: list->title = "Clock"; list->count = 4; break;
    case PREF_STEPS: {
        list->title = "Steps";
        list->count = 3;
        bool active = watch_counting(w);
        const char *status = w->settings.step_mode == STEPS_OFF ? "Off" : !active ? "Paused" :
                             w->steps_available ? "Counting" : "Unavailable";
        char count[14];
        watch_count_text(count, w->steps);
        snprintf(list->hint, sizeof list->hint, "%s · %s", count, status);
        break;
    }
    case PREF_ALARM: list->title = "Daily alarm"; list->count = 2; break;
    case PREF_NIGHT:
        list->title = "Night mode";
        list->count = 3;
        if (w->settings.night_on && w->settings.night_start == w->settings.night_end)
            snprintf(list->hint, sizeof list->hint, "Same times · no window");
        break;
    case PREF_MANUAL: list->title = "Set time & date"; list->count = 5; break;
    default: settings_value_list(w, list); break;
    }
}

static void row_text(settings_row_t *row, const char *name, const char *value)
{
    snprintf(row->label, sizeof row->label, "%s", name);
    snprintf(row->value, sizeof row->value, "%s", value);
}

static void clock_value(settings_row_t *row, const char *label, unsigned minutes)
{
    row_text(row, label, "");
    snprintf(row->value, sizeof row->value, "%02u:%02u", minutes / 60, minutes % 60);
}

void watch_settings_row(const watch_t *w, unsigned i, settings_row_t *row)
{
    static const char *const months[] = {"January", "February", "March", "April", "May", "June",
                                        "July", "August", "September", "October", "November", "December"};
    const watch_settings_t *a = &w->settings;
    *row = (settings_row_t){0};
    switch (w->settings_path[w->settings_depth - 1].page) {
    case PREF_ROOT:
        switch (i) {
        case 0: row_text(row, "Watch face", watch_face_name(a->face)); break;
        case 1: {
            unsigned offset = a->offset_min < 0 ? -a->offset_min : a->offset_min;
            row_text(row, "Clock", "");
            snprintf(row->value, sizeof row->value, "%u h · UTC%c%02u:%02u", a->format_12 ? 12u : 24u,
                     a->offset_min < 0 ? '-' : '+', offset / 60, offset % 60);
            break;
        }
        case 2: row_text(row, "Steps", step_modes[a->step_mode]); break;
        case 3:
            clock_value(row, "Alarm", a->alarm_min);
            snprintf(row->value + 5, sizeof row->value - 5, " · %s", a->alarm_on ? "On" : "Off");
            break;
        case 4: row_text(row, "Night mode", a->night_on ? "On" : "Off"); break;
        default: row_text(row, "Timers", ">"); break;
        }
        break;
    case PREF_TIMERS: row_text(row, i == 0 ? "Countdown" : "Stopwatch", ">"); break;
    case PREF_CLOCK:
        switch (i) {
        case 0: row_text(row, "Clock format", a->format_12 ? "12-hour" : "24-hour"); break;
        case 1: {
            unsigned offset = a->offset_min < 0 ? -a->offset_min : a->offset_min;
            row_text(row, "UTC offset", "");
            snprintf(row->value, sizeof row->value, "%c%02u:%02u", a->offset_min < 0 ? '-' : '+',
                     offset / 60, offset % 60);
            break;
        }
        case 2: row_text(row, "Set time & date", ">"); break;
        case 3: row_text(row, "Sync with phone", ">"); break;
        }
        break;
    case PREF_STEPS:
        if (!i) row_text(row, "Counting mode", step_modes[a->step_mode]);
        else if (i == 1) clock_value(row, "Daytime starts", a->steps_start);
        else clock_value(row, "Daytime ends", a->steps_end);
        break;
    case PREF_ALARM:
        if (!i) {
            row_text(row, "Enabled", a->alarm_on ? "On" : "Off");
            row->toggle = a->alarm_on ? SETTINGS_ON : SETTINGS_OFF;
        } else {
            clock_value(row, "Alarm time", a->alarm_min);
        }
        break;
    case PREF_NIGHT:
        if (!i) {
            row_text(row, "Enabled", a->night_on ? "On" : "Off");
            row->toggle = a->night_on ? SETTINGS_ON : SETTINGS_OFF;
        } else if (i == 1) {
            clock_value(row, "Night starts", a->night_start);
        } else {
            clock_value(row, "Night ends", a->night_end);
        }
        break;
    case PREF_MANUAL: {
        static const char *const labels[] = {"Year", "Month", "Day", "Hour", "Minute"};
        const int values[] = {w->edit.year, w->edit.month, w->edit.day, w->edit.hour, w->edit.minute};
        row_text(row, labels[i], i == 1 ? months[w->edit.month - 1] : "");
        if (i != 1) snprintf(row->value, sizeof row->value, "%02d", values[i]);
        break;
    }
    default: settings_value_row(w, i, row); break;
    }
}

enum watch_action watch_settings_press(watch_t *w, enum watch_key key)
{
    settings_frame_t *frame = &w->settings_path[w->settings_depth - 1];
    enum settings_page page = frame->page;
    if (key == KEY_BACK) {
        w->settings_depth--;
        show_owner(w);
        return ACTION_NONE;
    }
    if (page == PREF_FACE) {
        if (key == KEY_MENU) {
            w->pending = w->settings;
            w->pending.face = w->face_focus;
            return settings_store_action(w);
        }
        w->face_focus = watch_face_neighbor(w->face_focus, key == KEY_UP ? -1 : 1);
        return ACTION_NONE;
    }
    if (page == PREF_SYNC || page == PREF_TOOL) return ACTION_NONE;
    if (key != KEY_MENU) {
        settings_list_t list;
        watch_settings_list(w, &list);
        unsigned delta = key == KEY_UP ? list.count - 1 : 1;
        frame->focus = (frame->focus + delta) % list.count;
        return ACTION_NONE;
    }
    unsigned i = frame->focus;
    switch (page) {
    case PREF_ROOT: {
        static const uint8_t entries[] = {PREF_FACE, PREF_CLOCK, PREF_STEPS, PREF_ALARM, PREF_NIGHT, PREF_TIMERS};
        if (!i) w->face_focus = w->settings.face;
        settings_push(w, entries[i], 0, 0);
        break;
    }
    case PREF_TIMERS: settings_push(w, PREF_TOOL, 0, i == 0 ? TIMER : STOPWATCH); break;
    case PREF_CLOCK:
        if (i < 2) {
            settings_value_open(w, i == 0 ? FIELD_FORMAT : FIELD_OFFSET);
        } else if (i == 2) {
            settings_read_manual(w);
            settings_push(w, PREF_MANUAL, 0, 0);
        } else {
            settings_push(w, PREF_SYNC, 0, 0);
            return ACTION_SYNC;
        }
        break;
    case PREF_STEPS: settings_value_open(w, FIELD_STEP_MODE + i); break;
    case PREF_ALARM:
    case PREF_NIGHT:
        if (i == 0) {
            w->pending = w->settings;
            if (page == PREF_ALARM) w->pending.alarm_on ^= 1;
            else w->pending.night_on ^= 1;
            return ACTION_STORE_PREFS;
        }
        settings_value_open(w, page == PREF_ALARM ? FIELD_ALARM_TIME : FIELD_NIGHT_START + i - 1);
        break;
    case PREF_MANUAL:
        settings_read_manual(w);
        settings_value_open(w, FIELD_YEAR + i);
        break;
    default: return settings_value_select(w);
    }
    return ACTION_NONE;
}
