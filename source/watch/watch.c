#include "watch.h"
#include "settings.h"

#include <string.h>

bool watch_offset_valid(int minutes)
{
    return minutes >= -720 && minutes <= 840 && minutes % 15 == 0;
}

bool watch_settings_valid(const watch_settings_t *s)
{
    return s->version == WATCH_SETTINGS_VERSION && s->face < FACE_COUNT && s->reserved == 0 &&
           watch_offset_valid(s->offset_min) && s->alarm_min < 1440 && s->alarm_min % 5 == 0 &&
           s->steps_start < 1440 && s->steps_start % 15 == 0 && s->steps_end < 1440 &&
           s->steps_end % 15 == 0 && s->format_12 <= 1 && s->step_mode <= STEPS_ALWAYS && s->alarm_on <= 1 &&
           s->night_start < 1440 && s->night_start % 15 == 0 && s->night_end < 1440 &&
           s->night_end % 15 == 0 && s->night_on <= 1 && s->night_reserved == 0;
}

enum { NIGHT_START = 1380, NIGHT_END = 420 };

bool watch_settings_decode(watch_settings_t *settings, const void *record, size_t size)
{
    enum { LEGACY_SIZE = 12, FACE_RECORD_SIZE = 14 };
    _Static_assert(offsetof(watch_settings_t, face) == LEGACY_SIZE &&
                   offsetof(watch_settings_t, night_start) == FACE_RECORD_SIZE && sizeof(watch_settings_t) == 20,
                   "Watch settings layout changed");
    if (size != LEGACY_SIZE && size != FACE_RECORD_SIZE && size != sizeof *settings) {
        return false;
    }
    watch_settings_t loaded = {0};
    memcpy(&loaded, record, size);
    if (size < sizeof loaded) {
        if (loaded.version != (size == LEGACY_SIZE ? 1 : 2)) return false;
        loaded.version = WATCH_SETTINGS_VERSION;
        loaded.night_start = NIGHT_START;
        loaded.night_end = NIGHT_END;
    }
    if (!watch_settings_valid(&loaded)) {
        return false;
    }
    *settings = loaded;
    return true;
}

void watch_init(watch_t *w)
{
    *w = (watch_t){
        .settings = {.version = WATCH_SETTINGS_VERSION, .steps_start = 420, .steps_end = 1380, .alarm_min = 420,
                     .night_start = NIGHT_START, .night_end = NIGHT_END},
        .step_day = -1, .last_alarm_day = -1, .battery = -1, .timer_seconds = 60,
    };
}

static uint32_t local_seconds(const watch_t *w, uint32_t utc)
{
    return (uint32_t)((int64_t)utc + w->settings.offset_min * 60);
}

static bool in_window(unsigned minute, unsigned start, unsigned end)
{
    return start < end ? start <= minute && minute < end : start > end && (minute >= start || minute < end);
}

void watch_tick(watch_t *w, uint32_t utc, bool valid, uint64_t elapsed, bool clock_changed)
{
    bool continuing = w->clock_valid && !clock_changed;
    uint32_t previous = w->utc;
    w->clock_valid = valid && watch_utc_valid(utc);
    w->utc = utc;
    w->night_active = w->clock_valid && w->settings.night_on &&
                      in_window(local_seconds(w, utc) / 60 % 1440, w->settings.night_start, w->settings.night_end);
    if (elapsed < w->elapsed) {
        w->timer_interrupted = w->timer_state != STOPPED || w->sw_state != STOPPED;
        w->timer_state = w->sw_state = STOPPED;
        w->timer_mark = w->sw_mark = w->sw_elapsed = w->last_activity = 0;
        w->lap_count = 0;
    }
    w->elapsed = elapsed;
    if (w->clock_valid) {
        uint32_t local = local_seconds(w, utc);
        int32_t day = (int32_t)(local / 86400);
        if (w->step_day != day) {
            w->steps = 0;
            w->step_day = day;
        }
        uint32_t since_alarm = local % 86400;
        uint32_t alarm = w->settings.alarm_min * 60u;
        if (continuing && utc >= previous && w->settings.alarm_on && day > w->last_alarm_day &&
            since_alarm >= alarm && since_alarm - alarm < 120 &&
            local_seconds(w, previous) <= local - (since_alarm - alarm)) {
            w->alert |= ALERT_ALARM;
            w->last_alarm_day = day;
        }
    }
    if (w->timer_state == RUNNING && w->elapsed >= w->timer_mark) {
        w->timer_state = STOPPED;
        w->alert |= ALERT_TIMER;
    }
    if (w->screen != FACE && w->screen != SYNC_SCREEN &&
        elapsed - w->last_activity >= 15 * WATCH_SECOND_US) {
        watch_close_menu(w);
    }
}

bool watch_counting(const watch_t *w)
{
    if (!w->clock_valid || w->settings.step_mode == STEPS_OFF) {
        return false;
    }
    if (w->settings.step_mode == STEPS_ALWAYS) {
        return true;
    }
    unsigned minute = local_seconds(w, w->utc) / 60 % 1440;
    unsigned start = w->settings.steps_start;
    unsigned end = w->settings.steps_end;
    return in_window(minute, start, end);
}

void watch_add_steps(watch_t *w, uint32_t count)
{
    if (watch_counting(w)) {
        w->steps += count > UINT32_MAX - w->steps ? UINT32_MAX - w->steps : count;
    }
}

void watch_steps_sample(watch_t *w, uint32_t raw, bool valid, bool reset)
{
    w->steps_available = valid;
    if (valid) {
        if (w->raw_steps_valid && !reset) {
            uint32_t delta = raw - w->raw_steps;
            if (delta <= 10000) {
                watch_add_steps(w, delta);
            }
        }
        w->raw_steps = raw;
    }
    w->raw_steps_valid = valid;
}

uint32_t watch_timer_left(const watch_t *w)
{
    uint64_t left = w->timer_mark;
    if (w->timer_state == STOPPED) {
        return 0;
    }
    if (w->timer_state == RUNNING) {
        left = w->elapsed < left ? left - w->elapsed : 0;
    }
    return (uint32_t)((left + WATCH_SECOND_US - 1) / WATCH_SECOND_US);
}

uint64_t watch_stopwatch_us(const watch_t *w)
{
    return w->sw_elapsed + (w->sw_state == RUNNING ? w->elapsed - w->sw_mark : 0);
}

static uint32_t earlier(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t until_minute(uint32_t local, unsigned minute)
{
    uint32_t target = minute * 60u;
    return target > local ? target - local : target + 86400u - local;
}

uint32_t watch_next_wake(const watch_t *w)
{
    uint32_t next = w->clock_valid ? 60 - w->utc % 60 : 300;
    if (w->night_active) {
        const watch_settings_t *s = &w->settings;
        uint32_t local = local_seconds(w, w->utc) % 86400;
        next = until_minute(local, s->night_end);
        if (s->alarm_on) next = earlier(next, until_minute(local, s->alarm_min));
        if (s->step_mode != STEPS_OFF) next = earlier(next, until_minute(local, 0));
        if (s->step_mode == STEPS_DAYTIME && s->steps_start != s->steps_end) {
            next = earlier(next, until_minute(local, s->steps_start));
            next = earlier(next, until_minute(local, s->steps_end));
        }
    }
    if (w->timer_state == RUNNING) {
        uint32_t timer = watch_timer_left(w);
        if (timer < next) {
            next = timer ? timer : 1;
        }
    }
    return next;
}

bool watch_render_due(const watch_t *w, uint8_t previous_alert, bool requested)
{
    return requested || !w->night_active || w->alert != previous_alert;
}

static int wrap(int value, int min, int max)
{
    return value < min ? max : value > max ? min : value;
}

enum watch_face watch_face_neighbor(enum watch_face style, int direction)
{
    return wrap((int)style + direction, FACE_LEDGER, FACE_COUNT - 1);
}

static void timer_key(watch_t *w, enum watch_key key)
{
    if (key == KEY_MENU) {
        if (w->timer_state == RUNNING) {
            w->timer_mark = w->timer_mark > w->elapsed ? w->timer_mark - w->elapsed : 0;
            w->timer_state = PAUSED;
        } else {
            w->timer_mark = w->elapsed + (w->timer_state == PAUSED ? w->timer_mark : w->timer_seconds * WATCH_SECOND_US);
            w->timer_state = RUNNING;
            w->timer_interrupted = false;
        }
    } else if (key == KEY_DOWN && w->timer_state != STOPPED) {
        w->timer_state = STOPPED;
    } else if (w->timer_state == STOPPED) {
        w->timer_seconds = (uint32_t)wrap((int)w->timer_seconds + (key == KEY_UP ? 5 : -5), 5, 5995);
    }
}

static void stopwatch_key(watch_t *w, enum watch_key key)
{
    if (key == KEY_MENU) {
        if (w->sw_state == RUNNING) {
            w->sw_elapsed = watch_stopwatch_us(w);
            w->sw_state = PAUSED;
        } else {
            w->sw_mark = w->elapsed;
            w->sw_state = RUNNING;
            w->timer_interrupted = false;
        }
    } else if (key == KEY_UP && w->sw_state == RUNNING) {
        if (w->lap_count < WATCH_LAPS) {
            w->laps[w->lap_count++] = watch_stopwatch_us(w);
        }
    } else if (key == KEY_DOWN && w->sw_state != RUNNING) {
        w->sw_state = STOPPED;
        w->sw_elapsed = 0;
        w->lap_count = 0;
    }
}

enum watch_action watch_press(watch_t *w, enum watch_key key)
{
    w->last_activity = w->elapsed;
    if (w->screen == SYNC_SCREEN && w->settings_depth) {
        return watch_settings_press(w, key);
    }
    if (w->alert) {
        w->alert &= (uint8_t)~(w->alert & ALERT_ALARM ? ALERT_ALARM : ALERT_TIMER);
    } else if (key != KEY_BACK && w->screen == TIMER) {
        timer_key(w, key);
    } else if (key != KEY_BACK && w->screen == STOPWATCH) {
        stopwatch_key(w, key);
    } else if (w->settings_depth) {
        return watch_settings_press(w, key);
    } else if (w->screen == FACE && key == KEY_MENU) {
        watch_settings_enter(w);
    }
    return ACTION_NONE;
}

bool watch_apply_preferences(watch_t *w)
{
    bool clock_changed = w->settings.offset_min != w->pending.offset_min ||
                         w->settings.alarm_on != w->pending.alarm_on ||
                         w->settings.alarm_min != w->pending.alarm_min;
    w->settings = w->pending;
    return clock_changed;
}

void watch_close_menu(watch_t *w)
{
    w->settings_depth = 0;
    w->screen = FACE;
}
