#ifndef WATCH_H
#define WATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WATCH_FIRST_UTC UINT32_C(946684800)
#define WATCH_LAST_UTC UINT32_C(4102444799)
#define WATCH_SECOND_US UINT64_C(1000000)
#define WATCH_LAPS 5
#define WATCH_SETTINGS_VERSION 3
#define WATCH_SETTINGS_DEPTH 5

enum watch_screen { FACE, MENU, STEPS = 3, SET_TIME, SETTINGS, ALARM, TIMER, STOPWATCH, SYNC_SCREEN, FACE_CHOOSER, NIGHT_SETTINGS };
enum watch_key { KEY_MENU, KEY_BACK, KEY_UP, KEY_DOWN };
enum watch_step_mode { STEPS_OFF, STEPS_DAYTIME, STEPS_ALWAYS };
enum watch_alert { ALERT_NONE = 0, ALERT_ALARM = 1, ALERT_TIMER = 2 };
enum watch_action { ACTION_NONE, ACTION_STORE_PREFS, ACTION_SET_UTC, ACTION_SYNC };
enum watch_run { STOPPED, RUNNING, PAUSED };
enum watch_face {
    FACE_LEDGER, FACE_SPLIT, FACE_DIAL,
    FACE_SIGNAL_BOX, FACE_PATCHBAY, FACE_POCKET_CONSOLE, FACE_FILING_CARD, FACE_CHART_TABLE,
    FACE_WOODCUT_TIDE, FACE_CONCRETE_STUDY, FACE_BLOCKPRINT_IRIS, FACE_INK_OBSERVATORY, FACE_FILM_LEADER,
    FACE_MINUTE_ATLAS, FACE_QUARTER_DIAL, FACE_LETTERPRESS_TIME, FACE_SHORTWAVE, FACE_DOMINO,
    FACE_COUNT
};

typedef struct {
    int year, month, day, hour, minute, second;
} watch_date_t;

typedef struct {
    int16_t offset_min;
    uint16_t alarm_min, steps_start, steps_end;
    uint8_t version, format_12, step_mode, alarm_on;
    uint8_t face, reserved;
    uint16_t night_start, night_end;
    uint8_t night_on, night_reserved;
} watch_settings_t;

typedef struct {
    uint8_t page, focus, arg;
} settings_frame_t;

typedef struct {
    watch_settings_t settings;
    uint32_t utc, steps, raw_steps;
    int32_t step_day, last_alarm_day;
    bool clock_valid, degraded, raw_steps_valid, steps_available;
    bool night_active;
    int16_t battery;
    uint8_t screen, alert, timer_state, sw_state, lap_count, face_focus;
    uint8_t settings_depth, settings_field, settings_owner;
    settings_frame_t settings_path[WATCH_SETTINGS_DEPTH];
    bool timer_interrupted;
    watch_date_t edit;
    watch_settings_t pending;
    uint32_t requested_utc, timer_seconds;
    uint64_t elapsed, last_activity, timer_mark, sw_mark, sw_elapsed;
    uint64_t laps[WATCH_LAPS];
} watch_t;

bool watch_utc_valid(uint32_t utc);
bool watch_encode_date(const watch_date_t *date, int16_t offset_min, uint32_t *utc);
bool watch_decode_date(uint32_t utc, int16_t offset_min, watch_date_t *date);
int watch_month_days(int year, int month);
bool watch_pcf_decode(const uint8_t wire[7], uint32_t *utc);
bool watch_pcf_encode(uint32_t utc, uint8_t wire[7]);
bool watch_settings_valid(const watch_settings_t *settings);
bool watch_offset_valid(int minutes);
bool watch_settings_decode(watch_settings_t *settings, const void *record, size_t size);
void watch_init(watch_t *watch);
void watch_tick(watch_t *watch, uint32_t utc, bool valid, uint64_t elapsed, bool clock_changed);
bool watch_counting(const watch_t *watch);
void watch_add_steps(watch_t *watch, uint32_t count);
void watch_steps_sample(watch_t *watch, uint32_t raw, bool valid, bool reset);
uint32_t watch_timer_left(const watch_t *watch);
uint64_t watch_stopwatch_us(const watch_t *watch);
uint32_t watch_next_wake(const watch_t *watch);
bool watch_render_due(const watch_t *watch, uint8_t previous_alert, bool requested);
enum watch_face watch_face_neighbor(enum watch_face style, int direction);
enum watch_action watch_press(watch_t *watch, enum watch_key key);
bool watch_apply_preferences(watch_t *watch);
void watch_close_menu(watch_t *watch);

#endif
