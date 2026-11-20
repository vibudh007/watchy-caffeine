#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "branding.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_rom_crc.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "panel.h"
#include "portal.h"
#include "settings.h"
#include "sync.h"
#ifdef WATCHY_BENCH_DEMO
#include "demo.h"
#endif
#ifdef WATCHY_BENCH_UI
#include "ui_demo.h"
#endif

#define RETAINED_SCHEMA UINT32_C(0x5741540f)
#define MOTION_MASK (UINT64_C(1) << BOARD_MOTION_INT)
#define MOTION_HOLDOFF_US (3 * WATCH_SECOND_US)

#ifdef WATCHY_INITIAL_UTC_OFFSET
_Static_assert(WATCHY_INITIAL_UTC_OFFSET >= -720 && WATCHY_INITIAL_UTC_OFFSET <= 840 &&
               WATCHY_INITIAL_UTC_OFFSET % 15 == 0, "Invalid initial UTC offset");
#endif

typedef struct {
    uint32_t schema, checksum;
    watch_t watch;
    uint8_t previous[WATCH_FRAME_SIZE];
    uint8_t partials, masked_keys;
    bool valid_frame, fallback, software_override, bma_on, motion_on, bma_known;
    uint16_t bma_feature_base;
    uint64_t motion_rearm_at;
    uint32_t fallback_utc;
    uint32_t anchor_ref, anchor_rtc;
    uint64_t fallback_elapsed;
#ifdef WATCHY_BENCH_DEMO
    uint8_t demo_frames;
#endif
#ifdef WATCHY_INITIAL_UTC_OFFSET
    uint8_t report_wakes;
#endif
} checkpoint_t;

static RTC_DATA_ATTR checkpoint_t saved;
static uint8_t pixels[WATCH_FRAME_SIZE];
static panel_stats_t panel_stats;
static bool ap_active, sync_result_shown;
static bool portal_frame_ready;
#ifndef WATCHY_BENCH_DEMO
static bool storage_ready;
static const char settings_namespace[] = "watchy_core";
#endif
static uint64_t last_sample;
static uint32_t last_utc;
static bool last_valid;
static esp_sleep_wakeup_cause_t wake_source;

static uint64_t elapsed_us(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return (uint64_t)esp_timer_get_time();
    }
    return (uint64_t)value.tv_sec * WATCH_SECOND_US + (unsigned long)value.tv_nsec / 1000;
}

static uint32_t checksum(void)
{
    size_t offset = offsetof(checkpoint_t, watch);
    const uint8_t *data = (const uint8_t *)&saved + offset;
    return esp_rom_crc32_le(0, data, sizeof saved - offset);
}

static void restore(void)
{
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP && saved.schema == RETAINED_SCHEMA &&
        saved.checksum == checksum() && watch_settings_valid(&saved.watch.settings)) {
        return;
    }
    memset(&saved, 0, sizeof saved);
    saved.bma_feature_base = BOARD_BMA_UNINITIALIZED;
    watch_init(&saved.watch);
#ifdef WATCHY_INITIAL_UTC_OFFSET
    saved.watch.settings.offset_min = WATCHY_INITIAL_UTC_OFFSET;
    saved.report_wakes = 3;
#endif
#ifndef WATCHY_BENCH_DEMO
    storage_ready = nvs_flash_init() == ESP_OK;
    nvs_handle_t nvs;
    uint8_t record[sizeof(watch_settings_t)];
    size_t size = sizeof record;
    if (storage_ready && nvs_open(settings_namespace, NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "settings", record, &size) == ESP_OK) {
            watch_settings_decode(&saved.watch.settings, record, size);
        }
        nvs_close(nvs);
    }
#endif
}

static bool store(const watch_settings_t *settings)
{
#if defined(WATCHY_BENCH_DEMO) || defined(WATCHY_NIGHT_TEST)
    return watch_settings_valid(settings);
#else
    if (!storage_ready) {
        storage_ready = nvs_flash_init() == ESP_OK;
    }
    nvs_handle_t nvs;
    if (!storage_ready || nvs_open(settings_namespace, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_blob(nvs, "settings", settings, sizeof *settings) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return ok;
#endif
}

static void sample_clock(void)
{
    last_sample = elapsed_us();
    // A valid but unchanged RTC must not undo a time set accepted in fallback mode.
    bool read = !saved.software_override && board_rtc_read(&last_utc);
    last_valid = read;
    saved.watch.degraded = !read;
    if (!read) {
        saved.anchor_ref = 0;
    }
    if (read) {
        saved.fallback = true;
        saved.fallback_utc = last_utc;
        saved.fallback_elapsed = last_sample;
    } else if (saved.fallback && last_sample >= saved.fallback_elapsed) {
        uint64_t duration = (last_sample - saved.fallback_elapsed) / WATCH_SECOND_US;
        if (duration <= WATCH_LAST_UTC - saved.fallback_utc) {
            last_utc = saved.fallback_utc + (uint32_t)duration;
            last_valid = true;
        }
    }
}

static void tick(bool changed)
{
    uint64_t now = elapsed_us();
    uint64_t duration = now >= last_sample ? (now - last_sample) / WATCH_SECOND_US : 0;
    bool in_range = last_valid && duration <= WATCH_LAST_UTC - last_utc;
    watch_tick(&saved.watch, in_range ? last_utc + (uint32_t)duration : last_utc, in_range, now, changed);
}

static void anchor_clock(uint32_t utc, bool verified)
{
    saved.anchor_ref = 0;
    saved.watch.degraded = !verified;
    saved.fallback = true;
    saved.fallback_utc = utc;
    saved.fallback_elapsed = elapsed_us();
    last_utc = utc;
    last_sample = saved.fallback_elapsed;
    last_valid = true;
    tick(true);
}

static bool set_clock(uint32_t utc)
{
    bool verified = board_rtc_write(utc);
    saved.software_override = !verified;
    anchor_clock(utc, verified);
    return true;
}

static void sense_steps(void)
{
    if (!saved.bma_on) {
        return;
    }
    uint32_t raw = 0;
    bool valid = board_step_sample(&raw);
    watch_steps_sample(&saved.watch, raw, valid, false);
    if (!valid) {
        saved.bma_known = false;
        saved.bma_feature_base = BOARD_BMA_UNINITIALIZED;
    }
}

static void configure_motion(void)
{
#ifdef WATCHY_BENCH_DEMO
    saved.watch.steps_available = false;
#else
    bool want = saved.watch.battery != 0 && watch_counting(&saved.watch);
    bool wake_on_motion = saved.watch.battery != 0 && saved.watch.night_active;
    if (!saved.bma_known || want != saved.bma_on || wake_on_motion != saved.motion_on) {
        bool new_baseline = !saved.bma_known || want != saved.bma_on;
        bool ok = board_motion(want, wake_on_motion, &saved.bma_feature_base);
        saved.bma_on = ok && want;
        saved.motion_on = ok && wake_on_motion;
        if (!saved.motion_on) saved.motion_rearm_at = 0;
        saved.bma_known = ok;
        if (new_baseline || !ok) saved.watch.raw_steps_valid = false;
        if (saved.bma_on && new_baseline) {
            sense_steps();
        } else if (!saved.bma_on) {
            saved.watch.steps_available = ok;
        }
    }
#endif
}

static void draw_message(const char *message)
{
    memset(pixels, 0xff, sizeof pixels);
    watch_text(pixels, 8, 66, 2, message);
    watch_text(pixels, 8, 162, 2, "RETURNING...");
}

static bool display_pixels(void)
{
    if (saved.valid_frame && !memcmp(pixels, saved.previous, sizeof pixels)) {
        return true;
    }
    bool full = !saved.valid_frame || saved.partials >= 60;
    bool drawn = panel_update(pixels, saved.previous, full, !ap_active, &panel_stats);
    if (!drawn && !full) {
        drawn = panel_update(pixels, saved.previous, true, !ap_active, &panel_stats);
        full = true;
    }
    saved.valid_frame = drawn;
    if (drawn) {
        memcpy(saved.previous, pixels, sizeof pixels);
        saved.partials = full ? 0 : saved.partials + 1;
    }
    return drawn;
}

static bool show(void)
{
    if (ap_active) {
        if (!portal_frame_ready && !portal_frame(pixels)) {
            return false;
        }
        portal_frame_ready = true;
    } else {
        watch_render(&saved.watch, pixels);
    }
    return display_pixels();
}

static void handle_key(enum watch_key key)
{
    watch_t *w = &saved.watch;
    if (ap_active) {
        if (key == KEY_BACK) {
            portal_stop();
            ap_active = false;
            watch_press(w, KEY_BACK);
        }
        return;
    }
    enum watch_action action = watch_press(w, key);
    if (action == ACTION_SET_UTC) {
        if (watch_utc_valid(w->requested_utc)) {
            set_clock(w->requested_utc);
            settings_read_manual(w);
            configure_motion();
            if (!w->degraded) return;
        }
        draw_message(watch_utc_valid(w->requested_utc) ? "BACKUP CLOCK SET" : "OUT OF RANGE");
        display_pixels();
        vTaskDelay(pdMS_TO_TICKS(700));
    } else if (action == ACTION_STORE_PREFS) {
        if (store(&w->pending)) {
            tick(watch_apply_preferences(w));
            configure_motion();
        } else {
            draw_message("SAVE FAILED");
            display_pixels();
            vTaskDelay(pdMS_TO_TICKS(700));
        }
    } else if (action == ACTION_SYNC) {
        sync_result_shown = false;
        portal_frame_ready = false;
        ap_active = w->battery != 0 && portal_start();
        if (!ap_active) {
            draw_message(w->battery == 0 ? "CHARGE WATCH" : "WI-FI UNAVAILABLE");
            display_pixels();
            vTaskDelay(pdMS_TO_TICKS(700));
            watch_press(w, KEY_BACK);
        }
    }
}

#ifdef WATCHY_BENCH_UI
static bool capture_ui_frame(const char *name)
{
    if (!show()) {
        printf("WATCHY_UI_FAILED %s\n", name);
        return false;
    }
    printf("WATCHY_UI_FRAME %s panel_us=%" PRIu32 " wave_gpio=%d booster_gpio=%d\n",
           name, panel_stats.update_us, panel_stats.waveform.gpio_wake, panel_stats.shutdown.gpio_wake);
    demo_emit(pixels);
    return true;
}
#endif

static bool handle_portal(void)
{
    if (!ap_active) {
        return false;
    }
    if (portal_finished()) {
        portal_stop();
        ap_active = false;
        if (sync_result_shown) {
            draw_message(saved.watch.degraded ? "BACKUP CLOCK SET" : "TIME SET");
            display_pixels();
            vTaskDelay(pdMS_TO_TICKS(700));
        }
        watch_close_menu(&saved.watch);
        bool drawn = show();
#ifdef WATCHY_BENCH_DEMO
        if (sync_result_shown) {
            printf("WATCHY_DEMO_SYNC_DONE frame=%d\n", drawn);
        } else {
            puts("WATCHY_DEMO_SYNC_ENDED");
        }
#else
        (void)drawn;
#endif
        return true;
    }
    watch_time_sync_t sync;
    if (portal_request(&sync)) {
        watch_t *watch = &saved.watch;
        if (watch_sync_preferences(watch, &sync)) {
            if (!store(&watch->pending)) {
                portal_clock_result(PORTAL_SYNC_OFFSET_FAILED, watch->settings.offset_min, 0, 0);
                return false;
            }
            watch_apply_preferences(watch);
        }
#ifdef WATCHY_BENCH_DEMO
        uint32_t previous = watch->utc;
#endif
        uint32_t raw = 0, period = 0;
        int32_t error = 0;
        if (saved.anchor_ref && board_rtc_read(&raw)) {
            watch_sync_interval(saved.anchor_ref, saved.anchor_rtc, sync.utc, raw, &period, &error);
        }
        set_clock(sync.utc);
        configure_motion();
        if (!watch->degraded && board_rtc_read(&saved.anchor_rtc)) {
            saved.anchor_ref = sync.utc;
        }
        sync_result_shown = true;
        portal_clock_result(watch->degraded ? PORTAL_SYNC_BACKUP : PORTAL_SYNC_EXTERNAL,
                            watch->settings.offset_min, period, error);
#ifdef WATCHY_INITIAL_UTC_OFFSET
        printf(WATCH_PRODUCT_NAME " sync: rtc=%d offset=%d\n", !watch->degraded, watch->settings.offset_min);
#endif
#ifdef WATCHY_BENCH_DEMO
        printf("WATCHY_DEMO_SYNC previous=%" PRIu32 " posted=%" PRIu32 " applied=%" PRIu32 " backup=%d\n",
               previous, sync.utc, watch->utc, watch->degraded);
#endif
    }
    return false;
}

static void sleep_watch(uint8_t held)
{
    uint32_t rendered_minute = saved.watch.utc / 60;
    bool rendered_valid = saved.watch.clock_valid;
    uint8_t rendered_alert = saved.watch.alert;
    bool was_night = saved.watch.night_active;
    sample_clock();
    tick(false);
    configure_motion();
    if (watch_render_due(&saved.watch, rendered_alert, false) &&
        (rendered_minute != saved.watch.utc / 60 || rendered_valid != saved.watch.clock_valid ||
         was_night != saved.watch.night_active || rendered_alert != saved.watch.alert)) {
        show();
    }
    uint32_t delay = watch_next_wake(&saved.watch);
    bool hold_motion = saved.motion_on && saved.watch.elapsed < saved.motion_rearm_at;
    if (hold_motion) {
        uint64_t remaining = saved.motion_rearm_at - saved.watch.elapsed;
        uint32_t seconds = (uint32_t)((remaining + WATCH_SECOND_US - 1) / WATCH_SECOND_US);
        if (seconds < delay) delay = seconds;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    bool check_release = (held & (uint8_t)~saved.masked_keys) != 0;
#ifdef WATCHY_BENCH_DEMO
    bool rtc_minute = false;
#else
    uint32_t target = saved.watch.utc + delay;
    bool rtc_minute = !check_release && saved.watch.battery != 0 && !saved.watch.degraded &&
                      saved.watch.clock_valid && delay <= WATCH_LAST_UTC - saved.watch.utc &&
                      board_rtc_arm(target);
    if (rtc_minute) {
        uint32_t checked;
        if (!board_rtc_read(&checked) || checked >= target || checked < saved.watch.utc) {
            rtc_minute = false;
            delay = 1;
        } else {
            delay = target - checked;
        }
    }
    if (rtc_minute && esp_sleep_enable_ext0_wakeup(BOARD_RTC_INT, 0) != ESP_OK) {
        rtc_minute = false;
    }
    if (!rtc_minute) {
        board_rtc_disarm();
    }
#endif
    if (saved.watch.battery == 0 && !saved.watch.alert && saved.watch.timer_state != RUNNING) {
        delay = 300;
    }
    if (check_release && delay > 2) {
        delay = 2;
    }
    esp_sleep_enable_timer_wakeup((delay + (rtc_minute ? 2 : 0)) * WATCH_SECOND_US);
    uint64_t keys = board_wake_mask(held);
    if (saved.motion_on && !hold_motion) {
        if (gpio_get_level(BOARD_MOTION_INT)) board_motion_ack();
        if (!gpio_get_level(BOARD_MOTION_INT)) keys |= MOTION_MASK;
    }
    if (keys) {
        esp_sleep_enable_ext1_wakeup(keys, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
#ifdef WATCHY_NIGHT_TEST
    printf("NIGHT_TEST_SLEEP utc=%" PRIu32 " delay=%" PRIu32 " rtc=%d motion=%d\n",
           saved.watch.utc, delay, rtc_minute, (keys & MOTION_MASK) != 0);
    fflush(stdout);
#endif
    saved.masked_keys = held;
    saved.schema = RETAINED_SCHEMA;
    saved.checksum = checksum();
    board_stop();
    if (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED) {
        printf(WATCH_PRODUCT_NAME " sleep: rtc=%d frame=%d delay=%lu\n",
               saved.watch.clock_valid, saved.valid_frame, (unsigned long)delay);
#ifdef WATCHY_INITIAL_UTC_OFFSET
        printf(WATCH_PRODUCT_NAME " wake plan: external_alarm=%d\n", rtc_minute);
#endif
    }
    esp_deep_sleep_start();
}

#ifdef WATCHY_NIGHT_TEST
static void seed_night_test(void)
{
    watch_t *w = &saved.watch;
    if (!w->clock_valid || w->degraded || w->battery <= 0) {
        puts("NIGHT_TEST_NOT_READY");
        return;
    }
    unsigned seconds = (unsigned)((int64_t)w->utc + w->settings.offset_min * 60) % 86400;
    unsigned start = seconds / 900 * 15;
    unsigned alarm = (seconds + 180 + 299) / 300 * 5 % 1440;
    w->settings.night_start = start;
    w->settings.night_end = (start + 60) % 1440;
    w->settings.night_on = 1;
    w->settings.step_mode = STEPS_ALWAYS;
    w->settings.alarm_on = 1;
    w->settings.alarm_min = alarm;
    w->timer_state = RUNNING;
    w->timer_mark = w->elapsed + 130 * WATCH_SECOND_US;
    tick(true);
    printf("NIGHT_TEST_READY utc=%" PRIu32 " countdown_s=130 alarm_s=%u\n", w->utc,
           (alarm * 60 + 86400 - seconds) % 86400);
}
#endif

void app_main(void)
{
    restore();
#ifdef WATCHY_BENCH_DEMO
    uint32_t previous_minute = saved.watch.utc / 60;
    bool start_portal = false;
    bool seeded = false;
    uint32_t seed = 0;
    int16_t offset = 0;
    bool restart_probe = demo_reboot_begin(&offset);
#endif
    wake_source = esp_sleep_get_wakeup_cause();
    bool motion_wake = wake_source == ESP_SLEEP_WAKEUP_EXT1 && (esp_sleep_get_ext1_wakeup_status() & MOTION_MASK);
    uint8_t previous_alert = saved.watch.alert;
    bool was_empty = saved.watch.battery == 0;
    if (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED) {
        printf(WATCH_PRODUCT_NAME ": reset=%d\n", esp_reset_reason());
    }
    board_start();
    if (motion_wake) motion_wake = board_motion_ack();
#ifdef WATCHY_BENCH_DEMO
    if (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED && !restart_probe) {
        seeded = demo_seed(&seed, &offset, &start_portal);
    }
#else
    board_rtc_disarm();
#endif
    sense_steps();
    sample_clock();
#ifdef WATCHY_BENCH_DEMO
    if (seeded || restart_probe) {
        saved.watch.settings.offset_min = offset;
    }
    if (seeded && (start_portal || !last_valid)) {
        anchor_clock(seed, false);
    }
#endif
    tick(false);
    if (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED || saved.watch.battery <= 0 ||
        (saved.watch.clock_valid && saved.watch.utc % 3600 < 4)) {
        saved.watch.battery = board_battery();
    }
#ifdef WATCHY_NIGHT_TEST
    if (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED) seed_night_test();
#endif
    configure_motion();
    if (motion_wake && saved.watch.night_active) {
        saved.motion_rearm_at = saved.watch.elapsed + MOTION_HOLDOFF_US;
    }

#ifdef WATCHY_BENCH_UI
    if (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED) {
        watch_t baseline = saved.watch;
        bool passed = ui_demo_run(&saved.watch, handle_key, capture_ui_frame);
        if (ap_active) {
            handle_key(KEY_BACK);
        }
        saved.watch = baseline;
        show();
        printf("WATCHY_UI_DONE %d\n", passed);
        fflush(stdout);
        sleep_watch(board_keys());
        return;
    }
#endif

#ifdef WATCHY_BENCH_DEMO
    if (restart_probe) {
        bool external = saved.watch.clock_valid && !saved.watch.degraded;
        demo_reboot_finish(saved.watch.utc, external);
        draw_message(external ? "RTC REBOOT OK" : "RTC READ FAILED");
        display_pixels();
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    demo_reboot_report();
    if (start_portal) {
        handle_key(KEY_MENU);
        handle_key(KEY_DOWN);
        handle_key(KEY_MENU);
        handle_key(KEY_UP);
        handle_key(KEY_MENU);
    }
#endif

    uint8_t stable = board_keys();
    if (stable || (wake_source == ESP_SLEEP_WAKEUP_EXT1 && !motion_wake)) {
        vTaskDelay(pdMS_TO_TICKS(20));
        stable = board_keys();
    }
#ifdef WATCHY_BENCH_DEMO
    if (wake_source != ESP_SLEEP_WAKEUP_UNDEFINED) {
        demo_restart_if_held(stable, saved.watch.settings.offset_min);
    }
#endif
    uint8_t pressed = stable & (uint8_t)~saved.masked_keys;
    for (unsigned key = KEY_MENU; key <= KEY_DOWN; key++) {
        if (pressed & (1u << key)) {
            handle_key((enum watch_key)key);
        }
    }
    bool draw = watch_render_due(&saved.watch, previous_alert,
                                 pressed || motion_wake || ap_active || !saved.valid_frame ||
                                 was_empty != (saved.watch.battery == 0));
    bool visible = draw ? show() : saved.valid_frame;
#ifdef WATCHY_NIGHT_TEST
    printf("NIGHT_TEST_WAKE utc=%" PRIu32 " cause=%d motion=%d keys=%u night=%d alert=%u draw=%d frame=%d\n",
           saved.watch.utc, wake_source, motion_wake, pressed, saved.watch.night_active, saved.watch.alert, draw, visible);
    printf("NIGHT_TEST_MOTION configured=%d steps=%" PRIu32 " raw=%" PRIu32 " valid=%d feature_base=%u\n",
           saved.bma_known && saved.bma_on && saved.motion_on, saved.watch.steps, saved.watch.raw_steps,
           saved.watch.raw_steps_valid, saved.bma_feature_base);
    fflush(stdout);
#endif
#ifdef WATCHY_INITIAL_UTC_OFFSET
    bool scheduled = wake_source == ESP_SLEEP_WAKEUP_EXT0 || wake_source == ESP_SLEEP_WAKEUP_TIMER;
    if (saved.report_wakes && (scheduled || wake_source == ESP_SLEEP_WAKEUP_UNDEFINED)) {
        printf(WATCH_PRODUCT_NAME " panel: utc=%" PRIu32 " full=%d frame=%d gpio=%d wave_us=%" PRIu32
               " sleep_call_us=%" PRIu32 " update_us=%" PRIu32 " off_us=%" PRIu32
               " off_sleep_us=%" PRIu32 " off_gpio=%d\n",
               saved.watch.utc, panel_stats.full, visible, panel_stats.waveform.gpio_wake,
               panel_stats.waveform.elapsed_us, panel_stats.waveform.sleep_call_us, panel_stats.update_us,
               panel_stats.shutdown.elapsed_us, panel_stats.shutdown.sleep_call_us, panel_stats.shutdown.gpio_wake);
    }
    if (saved.report_wakes && scheduled) {
        saved.report_wakes--;
        printf(WATCH_PRODUCT_NAME " scheduled wake: cause=%d frame=%d\n", wake_source, visible);
    }
#endif
#ifdef WATCHY_BENCH_DEMO
    if (ap_active && visible) {
        puts("WATCHY_DEMO_QR_READY");
        fflush(stdout);
    }
    if (visible && (wake_source == ESP_SLEEP_WAKEUP_UNDEFINED ||
        (wake_source == ESP_SLEEP_WAKEUP_TIMER && saved.demo_frames == 1 &&
         saved.watch.utc / 60 != previous_minute))) {
        saved.demo_frames++;
        printf("WATCHY_DEMO_DISPLAY wake=%d\n", wake_source);
        demo_emit(pixels);
    }
#endif
    if (!visible && ap_active) {
        portal_stop();
        ap_active = false;
        watch_press(&saved.watch, KEY_BACK);
        show();
    }
    uint8_t candidate = stable;
    unsigned samples = 0;
    while (saved.watch.screen != FACE || ap_active) {
        vTaskDelay(pdMS_TO_TICKS(20));
        enum watch_screen before = saved.watch.screen;
        uint8_t alert = saved.watch.alert;
        tick(false);
        if (!handle_portal() && (before != saved.watch.screen || alert != saved.watch.alert)) {
            show();
        }
        uint8_t reading = board_keys();
        if (reading != candidate) {
            samples = 0;
            candidate = reading;
        } else if (candidate != stable && ++samples >= 2) {
#ifdef WATCHY_BENCH_DEMO
            demo_restart_if_held(candidate, saved.watch.settings.offset_min);
#endif
            pressed = candidate & (uint8_t)~stable;
            stable = candidate;
            for (unsigned key = KEY_MENU; key <= KEY_DOWN; key++) {
                if (pressed & (1u << key)) {
                    handle_key((enum watch_key)key);
                }
            }
            if (!show() && ap_active) {
                portal_stop();
                ap_active = false;
                watch_press(&saved.watch, KEY_BACK);
                show();
            }
        }
    }
    sleep_watch(board_keys() & stable);
}
