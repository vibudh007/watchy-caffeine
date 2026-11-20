#ifndef PANEL_H
#define PANEL_H
#include "frame.h"

typedef struct {
    uint32_t elapsed_us;
    uint32_t sleep_call_us;
    bool gpio_wake;
} panel_wait_t;

typedef struct {
    panel_wait_t waveform;
    panel_wait_t shutdown;
    uint32_t update_us;
    bool full;
} panel_stats_t;

bool panel_update(const uint8_t *frame, const uint8_t *previous, bool full,
                  bool allow_sleep, panel_stats_t *stats);

#endif
