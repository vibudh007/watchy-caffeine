#include "settings.h"

#include <stdio.h>

enum offset_sign { BEHIND_UTC, AT_UTC, AHEAD_UTC };

static const settings_frame_t *picker(const watch_t *w)
{
    return &w->settings_path[w->settings_depth - 1];
}

static enum offset_sign offset_sign(int minutes)
{
    if (minutes < 0) return BEHIND_UTC;
    return minutes ? AHEAD_UTC : AT_UTC;
}

static unsigned offset_absolute(const watch_t *w)
{
    int minutes = w->settings.offset_min;
    return (unsigned)(minutes < 0 ? -minutes : minutes);
}

static unsigned offset_max_hour(enum offset_sign sign)
{
    return sign == BEHIND_UTC ? 12 : 14;
}

static unsigned first_quarter(unsigned hour)
{
    return hour == 0 ? 1 : 0;
}

static char offset_symbol(enum offset_sign sign)
{
    return sign == BEHIND_UTC ? '-' : '+';
}

static uint16_t selected_time(const watch_t *w)
{
    switch (w->settings_field) {
    case FIELD_STEP_START: return w->settings.steps_start;
    case FIELD_STEP_END: return w->settings.steps_end;
    case FIELD_NIGHT_START: return w->settings.night_start;
    case FIELD_NIGHT_END: return w->settings.night_end;
    default: return w->settings.alarm_min;
    }
}

static unsigned time_interval(const watch_t *w)
{
    return w->settings_field == FIELD_ALARM_TIME ? 5 : 15;
}

static void set_selected_time(watch_settings_t *pending, unsigned field, uint16_t minutes)
{
    switch (field) {
    case FIELD_STEP_START: pending->steps_start = minutes; break;
    case FIELD_STEP_END: pending->steps_end = minutes; break;
    case FIELD_NIGHT_START: pending->night_start = minutes; break;
    case FIELD_NIGHT_END: pending->night_end = minutes; break;
    default: pending->alarm_min = minutes; break;
    }
}

void settings_value_open(watch_t *w, enum settings_field field)
{
    w->settings_owner = w->settings_depth;
    w->settings_field = (uint8_t)field;
    switch (field) {
    case FIELD_FORMAT: settings_push(w, PREF_FORMAT, w->settings.format_12 ? 0 : 1, 0); break;
    case FIELD_OFFSET: settings_push(w, PREF_OFFSET_SIGN, offset_sign(w->settings.offset_min), 0); break;
    case FIELD_STEP_MODE: settings_push(w, PREF_STEP_MODE, w->settings.step_mode, 0); break;
    case FIELD_STEP_START:
    case FIELD_STEP_END:
    case FIELD_NIGHT_START:
    case FIELD_NIGHT_END:
    case FIELD_ALARM_TIME: settings_push(w, PREF_TIME_HOUR, selected_time(w) / 60u, 0); break;
    case FIELD_YEAR: settings_push(w, PREF_YEAR_GROUP, (unsigned)(w->edit.year - 2000) / 10, 0); break;
    case FIELD_MONTH: settings_push(w, PREF_MONTH, (unsigned)(w->edit.month - 1), 0); break;
    case FIELD_DAY: settings_push(w, PREF_DAY, (unsigned)(w->edit.day - 1), 0); break;
    case FIELD_HOUR: settings_push(w, PREF_TIME_HOUR, (unsigned)w->edit.hour, 0); break;
    case FIELD_MINUTE: settings_push(w, PREF_MIN_GROUP, (unsigned)w->edit.minute / 10, 0); break;
    }
}

static enum watch_action apply_manual(watch_t *w, int value)
{
    settings_read_manual(w);
    watch_date_t date = w->edit;
    switch (w->settings_field) {
    case FIELD_YEAR: date.year = value; break;
    case FIELD_MONTH: date.month = value; break;
    case FIELD_DAY: date.day = value; break;
    case FIELD_HOUR:
        if (date.hour != value) date.second = 0;
        date.hour = value;
        break;
    case FIELD_MINUTE:
        if (date.minute != value) date.second = 0;
        date.minute = value;
        break;
    }
    int last_day = watch_month_days(date.year, date.month);
    if (date.day > last_day) date.day = last_day;
    settings_finish_value(w);
    w->requested_utc = 0;
    if (watch_encode_date(&date, w->settings.offset_min, &w->requested_utc) &&
        w->clock_valid && !w->degraded && w->requested_utc == w->utc) {
        return ACTION_NONE;
    }
    return ACTION_SET_UTC;
}

enum watch_action settings_value_select(watch_t *w)
{
    const settings_frame_t *frame = picker(w);
    const unsigned i = frame->focus;
    const unsigned arg = frame->arg;
    w->pending = w->settings;
    switch (frame->page) {
    case PREF_FORMAT: w->pending.format_12 = i == 0; break;
    case PREF_STEP_MODE: w->pending.step_mode = (uint8_t)i; break;
    case PREF_OFFSET_SIGN:
        if (i != AT_UTC) {
            unsigned hour = i == offset_sign(w->settings.offset_min) ? offset_absolute(w) / 60 : 0;
            settings_push(w, PREF_OFFSET_HOUR, hour, i);
            return ACTION_NONE;
        }
        w->pending.offset_min = 0;
        break;
    case PREF_OFFSET_HOUR: {
        unsigned offset = offset_absolute(w);
        unsigned focus = arg == offset_sign(w->settings.offset_min) && i == offset / 60
                         ? offset % 60 / 15 - first_quarter(i) : 0;
        settings_push(w, PREF_OFFSET_MIN, focus, i);
        return ACTION_NONE;
    }
    case PREF_OFFSET_MIN: {
        unsigned sign = w->settings_path[w->settings_depth - 2].arg;
        int minutes = (int)(arg * 60 + (first_quarter(arg) + i) * 15);
        w->pending.offset_min = (int16_t)(sign == BEHIND_UTC ? -minutes : minutes);
        break;
    }
    case PREF_TIME_HOUR:
        if (w->settings_field == FIELD_HOUR) {
            return apply_manual(w, (int)i);
        } else {
            unsigned minutes = selected_time(w);
            unsigned focus = i == minutes / 60 ? minutes % 60 / time_interval(w) : 0;
            settings_push(w, PREF_TIME_MIN, focus, i);
            return ACTION_NONE;
        }
    case PREF_TIME_MIN:
        set_selected_time(&w->pending, w->settings_field, (uint16_t)(arg * 60 + i * time_interval(w)));
        break;
    case PREF_YEAR_GROUP: {
        unsigned year = (unsigned)(w->edit.year - 2000);
        settings_push(w, PREF_YEAR, i == year / 10 ? year % 10 : 0, i);
        return ACTION_NONE;
    }
    case PREF_YEAR: return apply_manual(w, 2000 + (int)(arg * 10 + i));
    case PREF_MONTH:
    case PREF_DAY: return apply_manual(w, (int)i + 1);
    case PREF_MIN_GROUP: {
        unsigned minute = (unsigned)w->edit.minute;
        settings_push(w, PREF_MINUTE, i == minute / 10 ? minute % 10 : 0, i);
        return ACTION_NONE;
    }
    case PREF_MINUTE: return apply_manual(w, (int)(arg * 10 + i));
    default: return ACTION_NONE;
    }
    settings_finish_value(w);
    return settings_store_action(w);
}

void settings_value_list(const watch_t *w, settings_list_t *list)
{
    const settings_frame_t *frame = picker(w);
    unsigned arg = frame->arg;
    switch (frame->page) {
    case PREF_FORMAT:
        list->title = "Clock format";
        list->count = 2;
        list->checked = w->settings.format_12 ? 0 : 1;
        break;
    case PREF_STEP_MODE:
        list->title = "Counting mode";
        list->count = 3;
        list->checked = w->settings.step_mode;
        break;
    case PREF_OFFSET_SIGN:
        list->title = "UTC offset";
        list->count = 3;
        if (!w->settings.offset_min) list->checked = AT_UTC;
        break;
    case PREF_OFFSET_HOUR:
        list->title = "Offset hour";
        list->count = offset_max_hour((enum offset_sign)arg) + 1;
        break;
    case PREF_OFFSET_MIN: {
        enum offset_sign sign = w->settings_path[w->settings_depth - 2].arg;
        unsigned offset = offset_absolute(w);
        list->title = "Exact UTC offset";
        list->count = arg == offset_max_hour(sign) ? 1 : 4 - first_quarter(arg);
        if (sign == offset_sign(w->settings.offset_min) && arg == offset / 60) {
            list->checked = (int)(offset % 60 / 15 - first_quarter(arg));
        }
        break;
    }
    case PREF_TIME_HOUR:
        list->title = "Hour";
        list->count = 24;
        if (w->settings_field == FIELD_HOUR) list->checked = w->edit.hour;
        break;
    case PREF_TIME_MIN: {
        unsigned minutes = selected_time(w);
        unsigned interval = time_interval(w);
        list->title = "Minute";
        list->count = 60 / interval;
        if (arg == minutes / 60) list->checked = (int)(minutes % 60 / interval);
        break;
    }
    case PREF_YEAR_GROUP: list->title = "Decade"; list->count = 10; break;
    case PREF_YEAR: {
        unsigned year = (unsigned)(w->edit.year - 2000);
        list->title = "Year";
        list->count = 10;
        if (arg == year / 10) list->checked = (int)(year % 10);
        break;
    }
    case PREF_MONTH: list->title = "Month"; list->count = 12; list->checked = w->edit.month - 1; break;
    case PREF_DAY:
        list->title = "Day";
        list->count = (unsigned)watch_month_days(w->edit.year, w->edit.month);
        list->checked = w->edit.day - 1;
        break;
    case PREF_MIN_GROUP: list->title = "Minute range"; list->count = 6; break;
    case PREF_MINUTE: {
        unsigned minute = (unsigned)w->edit.minute;
        list->title = "Minute";
        list->count = 10;
        if (arg == minute / 10) list->checked = (int)(minute % 10);
        break;
    }
    default: break;
    }
}

void settings_value_row(const watch_t *w, unsigned i, settings_row_t *row)
{
    static const char *const formats[] = {"12-hour", "24-hour"};
    static const char *const modes[] = {"Off", "Daytime", "Always"};
    static const char *const signs[] = {"Behind UTC", "UTC", "Ahead of UTC"};
    static const char *const months[] = {"January", "February", "March", "April", "May", "June",
                                        "July", "August", "September", "October", "November", "December"};
    const settings_frame_t *frame = picker(w);
    unsigned arg = frame->arg;
    char *label = row->label;
    size_t size = sizeof row->label;
    switch (frame->page) {
    case PREF_FORMAT: snprintf(label, size, "%s", formats[i]); break;
    case PREF_STEP_MODE: snprintf(label, size, "%s", modes[i]); break;
    case PREF_OFFSET_SIGN:
        snprintf(label, size, "%s", signs[i]);
        if (i == AT_UTC) snprintf(row->value, sizeof row->value, "+00:00");
        break;
    case PREF_OFFSET_HOUR: snprintf(label, size, "%c%02u hours", offset_symbol((enum offset_sign)arg), i); break;
    case PREF_OFFSET_MIN: {
        enum offset_sign sign = w->settings_path[w->settings_depth - 2].arg;
        snprintf(label, size, "%c%02u:%02u", offset_symbol(sign), arg, (first_quarter(arg) + i) * 15);
        break;
    }
    case PREF_TIME_HOUR: snprintf(label, size, "%02u", i); break;
    case PREF_TIME_MIN: snprintf(label, size, "%02u:%02u", arg, i * time_interval(w)); break;
    case PREF_YEAR_GROUP: snprintf(label, size, "%u-%u", 2000 + i * 10, 2009 + i * 10); break;
    case PREF_YEAR: snprintf(label, size, "%u", 2000 + arg * 10 + i); break;
    case PREF_MONTH: snprintf(label, size, "%s", months[i]); break;
    case PREF_DAY: snprintf(label, size, "%02u", i + 1); break;
    case PREF_MIN_GROUP: snprintf(label, size, "%02u-%02u", i * 10, i * 10 + 9); break;
    case PREF_MINUTE: snprintf(label, size, "%02u", arg * 10 + i); break;
    default: break;
    }
}
