#ifndef WATCH_SETTINGS_H
#define WATCH_SETTINGS_H

#include "watch.h"

enum settings_page {
    PREF_ROOT, PREF_CLOCK, PREF_STEPS, PREF_ALARM, PREF_MANUAL, PREF_FACE, PREF_SYNC,
    PREF_FORMAT, PREF_STEP_MODE, PREF_OFFSET_SIGN, PREF_OFFSET_HOUR, PREF_OFFSET_MIN,
    PREF_TIME_HOUR, PREF_TIME_MIN, PREF_YEAR_GROUP, PREF_YEAR, PREF_MONTH, PREF_DAY, PREF_MIN_GROUP, PREF_MINUTE,
    PREF_TIMERS, PREF_TOOL, PREF_NIGHT
};

enum settings_field {
    FIELD_FORMAT, FIELD_OFFSET, FIELD_STEP_MODE, FIELD_STEP_START, FIELD_STEP_END, FIELD_ALARM_TIME,
    FIELD_YEAR, FIELD_MONTH, FIELD_DAY, FIELD_HOUR, FIELD_MINUTE, FIELD_NIGHT_START, FIELD_NIGHT_END
};

enum settings_toggle { SETTINGS_NO_TOGGLE, SETTINGS_OFF, SETTINGS_ON };

typedef struct {
    const char *title;
    char hint[30];
    unsigned count, focus;
    int checked;
} settings_list_t;

typedef struct {
    char label[25], value[23];
    enum settings_toggle toggle;
} settings_row_t;

void watch_settings_enter(watch_t *watch);
enum watch_action watch_settings_press(watch_t *watch, enum watch_key key);
void watch_settings_list(const watch_t *watch, settings_list_t *list);
void watch_settings_row(const watch_t *watch, unsigned index, settings_row_t *row);

void settings_push(watch_t *watch, enum settings_page page, unsigned focus, unsigned arg);
void settings_finish_value(watch_t *watch);
void settings_read_manual(watch_t *watch);
enum watch_action settings_store_action(const watch_t *watch);
void settings_value_open(watch_t *watch, enum settings_field field);
enum watch_action settings_value_select(watch_t *watch);
void settings_value_list(const watch_t *watch, settings_list_t *list);
void settings_value_row(const watch_t *watch, unsigned index, settings_row_t *row);

#endif
