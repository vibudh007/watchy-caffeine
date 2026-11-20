#include "frame.h"
#include "face.h"
#include "settings_view.h"
#include "ui_font.h"

#include <string.h>

static const struct {
    char character;
    uint8_t columns[5];
} glyphs[] = {
    {'0',{62,81,73,69,62}}, {'1',{0,66,127,64,0}}, {'2',{66,97,81,73,70}},
    {'3',{33,65,69,75,49}}, {'4',{24,20,18,127,16}}, {'5',{39,69,69,69,57}},
    {'6',{60,74,73,73,48}}, {'7',{1,113,9,5,3}}, {'8',{54,73,73,73,54}},
    {'9',{6,73,73,41,30}}, {'A',{126,17,17,17,126}}, {'B',{127,73,73,73,54}},
    {'C',{62,65,65,65,34}}, {'D',{127,65,65,34,28}}, {'E',{127,73,73,73,65}},
    {'F',{127,9,9,9,1}}, {'G',{62,65,73,73,122}}, {'H',{127,8,8,8,127}},
    {'I',{0,65,127,65,0}}, {'J',{32,64,65,63,1}}, {'K',{127,8,20,34,65}},
    {'L',{127,64,64,64,64}}, {'M',{127,2,12,2,127}}, {'N',{127,4,8,16,127}},
    {'O',{62,65,65,65,62}}, {'P',{127,9,9,9,6}}, {'Q',{62,65,81,33,94}},
    {'R',{127,9,25,41,70}}, {'S',{70,73,73,73,49}}, {'T',{1,1,127,1,1}},
    {'U',{63,64,64,64,63}}, {'V',{31,32,64,32,31}}, {'W',{63,64,56,64,63}},
    {'X',{99,20,8,20,99}}, {'Y',{7,8,112,8,7}}, {'Z',{97,81,73,69,67}},
    {':',{0,54,54,0,0}}, {'-',{8,8,8,8,8}}, {'+',{8,8,62,8,8}},
    {'.',{0,96,96,0,0}}, {'>',{0,65,34,20,8}}, {'%',{99,19,8,100,99}},
    {'/',{96,16,8,4,3}}, {'?',{2,1,81,9,6}}, {' ',{0,0,0,0,0}},
};

void watch_fill(uint8_t *frame, int x, int y, int width, int height)
{
    int64_t right_edge = (int64_t)x + width;
    int64_t bottom_edge = (int64_t)y + height;
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    if (left >= WATCH_WIDTH || top >= WATCH_HEIGHT || right_edge <= left || bottom_edge <= top) {
        return;
    }
    int right = (int)(right_edge < WATCH_WIDTH ? right_edge : WATCH_WIDTH) - 1;
    int bottom = (int)(bottom_edge < WATCH_HEIGHT ? bottom_edge : WATCH_HEIGHT);
    int first = left / 8;
    int last = right / 8;
    uint8_t left_bits = 0xffu >> (left % 8);
    uint8_t right_bits = (uint8_t)(0xffu << (7 - right % 8));
    for (int y_row = top; y_row < bottom; y_row++) {
        uint8_t *row = frame + y_row * WATCH_STRIDE;
        if (first == last) {
            row[first] &= (uint8_t)~(left_bits & right_bits);
        } else {
            row[first] &= (uint8_t)~left_bits;
            memset(row + first + 1, 0, last - first - 1);
            row[last] &= (uint8_t)~right_bits;
        }
    }
}

void watch_text(uint8_t *frame, int x, int y, unsigned scale, const char *text)
{
    if (!scale || scale > 8 || y >= WATCH_HEIGHT || y <= -7 * (int)scale) {
        return;
    }
    int size = (int)scale;
    for (; *text && x < WATCH_WIDTH; text++, x += 6 * size) {
        for (size_t i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++) {
            if (*text == glyphs[i].character) {
                for (int col = 0; col < 5; col++) {
                    for (int row = 0; row < 7; row++) {
                        if (glyphs[i].columns[col] & (1 << row)) {
                            watch_fill(frame, x + col * size, y + row * size, size, size);
                        }
                    }
                }
                break;
            }
        }
    }
}

void watch_count_text(char dst[14], uint32_t count)
{
    char buffer[14];
    char *end = buffer + sizeof buffer;
    char *start = end;
    *--start = '\0';
    unsigned digits = 0;
    do {
        if (digits == 3) {
            *--start = ',';
            digits = 0;
        }
        *--start = (char)('0' + count % 10);
        count /= 10;
        digits++;
    } while (count);
    memcpy(dst, start, end - start);
}

static void right_text(uint8_t *frame, ui_font_t font, int x, int y, const char *text)
{
    ui_text(frame, font, x - ui_text_width(font, text), y, text);
}

static void rule(uint8_t *frame, int y)
{
    watch_fill(frame, 13, y, 174, 1);
}

static void header(uint8_t *frame, const char *title, const char *status)
{
    ui_text(frame, UI_TITLE, 13, 19, title);
    if (status) {
        right_text(frame, UI_TINY, 187, 17, status);
    }
    rule(frame, 40);
}

static void back_hint(uint8_t *frame, const char *label)
{
    ui_text(frame, UI_TINY, 13, 3, label);
}

static void footer(uint8_t *frame, const char *menu, const char *down)
{
    rule(frame, 170);
    ui_text(frame, UI_BODY, 13, 180, menu);
    if (down) {
        right_text(frame, UI_BODY, 187, 180, down);
    }
}

static void two_digits(char text[2], unsigned value)
{
    text[0] = (char)('0' + value / 10 % 10);
    text[1] = (char)('0' + value % 10);
}

static void elapsed_text(char text[9], uint64_t seconds, bool hours)
{
    if (hours) {
        two_digits(text, (unsigned)(seconds / 3600 % 100));
        text[2] = ':';
        text += 3;
    }
    two_digits(text, (unsigned)(seconds / 60 % 60));
    text[2] = ':';
    two_digits(text + 3, (unsigned)(seconds % 60));
    text[5] = '\0';
}

static const char *run_status(unsigned state, bool interrupted, const char *idle)
{
    return interrupted ? "Interrupted" : state == RUNNING ? "Running" : state == PAUSED ? "Paused" : idle;
}

static const char *run_action(unsigned state)
{
    return state == RUNNING ? "Menu Pause" : state == PAUSED ? "Menu Resume" : "Menu Start";
}

static void render_timer(uint8_t *frame, const watch_t *watch)
{
    unsigned state = watch->timer_state;
    header(frame, "Countdown", run_status(state, watch->timer_interrupted, "Ready"));
    uint32_t seconds = state == STOPPED ? watch->timer_seconds : watch_timer_left(watch);
    bool hours = seconds >= 3600;
    char duration[9];
    elapsed_text(duration, seconds, hours);
    ui_text(frame, hours ? UI_MONO_LARGE : UI_MONO_HERO, 13, 64, duration);
    if (state == RUNNING) {
        ui_text(frame, UI_BODY, 13, 117, "Remaining at last update");
    }
    back_hint(frame, "Back to Timers");
    if (state == STOPPED) {
        right_text(frame, UI_TINY, 187, 3, "↑ +5 sec");
    }
    footer(frame, run_action(state), state == STOPPED ? "↓ -5 sec" : "↓ Cancel");
}

static void render_stopwatch(uint8_t *frame, const watch_t *watch)
{
    unsigned state = watch->sw_state;
    header(frame, "Stopwatch", run_status(state, watch->timer_interrupted, "Ready"));
    char duration[9];
    elapsed_text(duration, watch_stopwatch_us(watch) / WATCH_SECOND_US, true);
    ui_text(frame, UI_MONO_LARGE, 13, 61, duration);

    if (watch->lap_count && watch->lap_count <= WATCH_LAPS) {
        char count[] = "Lap 0";
        count[4] += watch->lap_count;
        elapsed_text(duration, watch->laps[watch->lap_count - 1] / WATCH_SECOND_US, true);
        ui_text(frame, UI_BODY, 13, 105, count);
        right_text(frame, UI_MONO_SMALL, 187, 105, duration);
    }
    if (state == RUNNING) {
        ui_text(frame, UI_SMALL, 13, 125, "At last update");
    }
    back_hint(frame, "Back to Timers");
    if (state == RUNNING) {
        right_text(frame, UI_TINY, 187, 3, watch->lap_count < WATCH_LAPS ? "↑ Record lap" : "Lap limit reached");
    }
    footer(frame, run_action(state), state == RUNNING ? NULL : "↓ Reset");
}

static void render_alert(uint8_t *frame, bool alarm)
{
    header(frame, alarm ? "Alarm" : "Timer", NULL);
    if (alarm) {
        ui_text(frame, UI_TITLE, 13, 71, "Daily alarm");
    } else {
        ui_text(frame, UI_LARGE, 13, 67, "Time's up");
    }
    rule(frame, 155);
    ui_text(frame, UI_MENU, 13, 171, "Any button dismisses");
}

void watch_render(const watch_t *watch, uint8_t frame[WATCH_FRAME_SIZE])
{
    memset(frame, 0xff, WATCH_FRAME_SIZE);
    if (watch->alert) {
        render_alert(frame, watch->alert & ALERT_ALARM);
        return;
    }

    switch (watch->screen) {
    case FACE:
        watch_render_face(watch, frame);
        break;
    case MENU:
    case SET_TIME:
    case SETTINGS:
    case STEPS:
    case ALARM:
    case NIGHT_SETTINGS:
        watch_render_settings(watch, frame);
        break;
    case FACE_CHOOSER:
        watch_render_chooser(watch, frame);
        break;
    case TIMER:
        render_timer(frame, watch);
        break;
    case STOPWATCH:
        render_stopwatch(frame, watch);
        break;
    case SYNC_SCREEN:
        header(frame, "Sync time", NULL);
        ui_text(frame, UI_TITLE, 13, 77, "Starting Wi-Fi");
        back_hint(frame, "Back Cancel");
        break;
    default:
        break;
    }
}
