#include "ui_demo.h"
#include "settings.h"

enum { MENU_FACE, MENU_CLOCK, MENU_STEPS, MENU_ALARM, MENU_NIGHT, MENU_TIMERS };
enum { CLOCK_FORMAT, CLOCK_MANUAL = 2, CLOCK_SYNC };
enum { TIMERS_COUNTDOWN, TIMERS_STOPWATCH };

static bool at_page(const watch_t *watch, enum settings_page page, unsigned focus)
{
    if (!watch->settings_depth) return false;
    const settings_frame_t *frame = &watch->settings_path[watch->settings_depth - 1];
    return frame->page == page && frame->focus == focus;
}

static bool open_menu(const watch_t *watch, void (*press)(enum watch_key))
{
    for (unsigned i = 0; i < WATCH_SETTINGS_DEPTH + 2 && (watch->screen != FACE || watch->alert); i++) {
        press(KEY_BACK);
    }
    if (watch->screen != FACE || watch->alert) return false;
    press(KEY_MENU);
    return watch->screen == MENU && at_page(watch, PREF_ROOT, 0);
}

static bool select_row(const watch_t *watch, unsigned row, void (*press)(enum watch_key))
{
    if (!watch->settings_depth) return false;
    settings_list_t list;
    watch_settings_list(watch, &list);
    if (row >= list.count || list.focus >= list.count) return false;
    unsigned offset = (row + list.count - list.focus) % list.count;
    unsigned moves = offset <= list.count / 2 ? offset : list.count - offset;
    for (unsigned i = 0; i < moves; i++) {
        press(offset <= list.count / 2 ? KEY_DOWN : KEY_UP);
    }
    press(KEY_MENU);
    return true;
}

static bool open_group(const watch_t *watch, unsigned row, void (*press)(enum watch_key))
{
    return open_menu(watch, press) && select_row(watch, row, press);
}

static bool open_timing_tool(const watch_t *watch, unsigned row, void (*press)(enum watch_key))
{
    return open_group(watch, MENU_TIMERS, press) && at_page(watch, PREF_TIMERS, 0) &&
           select_row(watch, row, press);
}

static bool open_chooser(const watch_t *watch, void (*press)(enum watch_key))
{
    return open_group(watch, MENU_FACE, press) && watch->screen == FACE_CHOOSER;
}

static bool close_chooser(const watch_t *watch, void (*press)(enum watch_key))
{
    press(KEY_BACK);
    if (watch->screen != MENU || !at_page(watch, PREF_ROOT, MENU_FACE)) return false;
    press(KEY_BACK);
    return watch->screen == FACE;
}

static bool set_next_face(const watch_t *watch, void (*press)(enum watch_key))
{
    if (!open_chooser(watch, press)) return false;
    press(KEY_DOWN);
    press(KEY_MENU);
    return close_chooser(watch, press);
}

bool ui_demo_run(watch_t *watch, void (*press)(enum watch_key), bool (*capture)(const char *))
{
    if (!watch->clock_valid || !capture("ledger")) return false;
    if (!set_next_face(watch, press) || watch->settings.face != FACE_SPLIT || !capture("split")) return false;
    if (!set_next_face(watch, press) || watch->settings.face != FACE_DIAL || !capture("dial")) return false;

    static const char *const concepts[] = {
        "signal-box", "patchbay", "pocket-console", "filing-card", "chart-table", "woodcut-tide",
        "concrete-study", "blockprint-iris", "ink-observatory", "film-leader", "minute-atlas",
        "quarter-dial", "letterpress-time", "shortwave", "domino",
    };
    _Static_assert(sizeof concepts / sizeof *concepts == FACE_COUNT - FACE_SIGNAL_BOX, "Missing concept capture");
    if (!open_chooser(watch, press)) return false;
    press(KEY_DOWN);
    if (!capture("chooser-unselected")) return false;
    press(KEY_MENU);
    if (!capture("chooser-selected")) return false;
    if (!close_chooser(watch, press)) return false;
    for (unsigned i = 0; i < sizeof concepts / sizeof *concepts; i++) {
        if (i && !set_next_face(watch, press)) return false;
        if (watch->settings.face != FACE_SIGNAL_BOX + i || !capture(concepts[i])) return false;
    }

    if (!open_menu(watch, press) || !capture("menu") || !select_row(watch, MENU_CLOCK, press) ||
        !at_page(watch, PREF_CLOCK, 0) || !select_row(watch, CLOCK_MANUAL, press)) return false;
    for (unsigned i = 0; i < 4; i++) press(KEY_DOWN);
    if (!at_page(watch, PREF_MANUAL, 4) || !capture("set-time")) return false;
    press(KEY_BACK);
    if (!at_page(watch, PREF_CLOCK, CLOCK_MANUAL) || !capture("settings-clock") ||
        !select_row(watch, CLOCK_FORMAT, press) || !at_page(watch, PREF_FORMAT, 1) ||
        !capture("settings-format")) return false;
    press(KEY_UP);
    press(KEY_MENU);
    if (!at_page(watch, PREF_CLOCK, CLOCK_FORMAT) || !watch->settings.format_12 ||
        !capture("settings-updated")) return false;
    press(KEY_BACK);
    if (!watch->settings.format_12 || !at_page(watch, PREF_ROOT, MENU_CLOCK)) return false;
    if (!select_row(watch, MENU_ALARM, press) || !at_page(watch, PREF_ALARM, 0)) return false;
    press(KEY_MENU);
    if (!capture("alarm-editor")) return false;
    if (!open_group(watch, MENU_TIMERS, press) || !at_page(watch, PREF_TIMERS, 0) || !capture("timers") ||
        !select_row(watch, TIMERS_COUNTDOWN, press) || watch->screen != TIMER) return false;
    press(KEY_BACK);
    if (watch->screen != MENU || !at_page(watch, PREF_TIMERS, TIMERS_COUNTDOWN)) return false;
    press(KEY_BACK);
    if (!at_page(watch, PREF_ROOT, MENU_TIMERS)) return false;

    watch_t present = *watch;
    watch->settings.step_mode = STEPS_DAYTIME;
    watch->settings.steps_start = 420;
    watch->settings.steps_end = 1380;
    watch->steps_available = true;
    watch->utc = 1795129200u;  // A late-evening UTC sample for a paused step counter.
    watch->steps = 4286;
    if (!open_group(watch, MENU_STEPS, press) || !at_page(watch, PREF_STEPS, 0) || !capture("steps")) return false;
    *watch = present;

    if (!open_timing_tool(watch, TIMERS_COUNTDOWN, press) || watch->screen != TIMER) return false;
    press(KEY_MENU);
    if (watch->timer_state != RUNNING || !capture("timer")) return false;
    if (!open_timing_tool(watch, TIMERS_STOPWATCH, press) || watch->screen != STOPWATCH) return false;
    press(KEY_MENU);
    watch->elapsed += 23 * WATCH_SECOND_US;
    press(KEY_UP);
    watch->elapsed += 19 * WATCH_SECOND_US;
    press(KEY_MENU);
    if (watch->sw_state != PAUSED || watch->lap_count != 1 || !capture("stopwatch")) return false;
    press(KEY_BACK);
    if (watch->screen != MENU || !at_page(watch, PREF_TIMERS, TIMERS_STOPWATCH)) return false;
    *watch = present;

    watch->settings_depth = 0;
    watch->screen = FACE;
    watch->alert = ALERT_TIMER;
    if (!capture("alert")) return false;
    press(KEY_MENU);
    watch->clock_valid = false;
    if (!capture("unset-clock")) return false;
    watch->clock_valid = true;
    watch->degraded = true;
    watch->settings.face = FACE_LEDGER;
    watch->settings.format_12 = 1;
    if (!capture("backup-clock")) return false;
    watch->battery = 0;
    if (!capture("charge-watch")) return false;
    *watch = present;

    if (!open_group(watch, MENU_CLOCK, press) || !select_row(watch, CLOCK_SYNC, press) ||
        watch->screen != SYNC_SCREEN || !capture("sync")) return false;
    watch->alert = ALERT_TIMER;
    press(KEY_BACK);
    if (!at_page(watch, PREF_CLOCK, CLOCK_SYNC) || watch->alert != ALERT_TIMER) return false;
    press(KEY_BACK);
    if (!at_page(watch, PREF_CLOCK, CLOCK_SYNC) || watch->alert) return false;
    press(KEY_BACK);
    if (!at_page(watch, PREF_ROOT, MENU_CLOCK)) return false;
    press(KEY_BACK);
    return watch->screen == FACE;
}
