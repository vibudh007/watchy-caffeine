#ifndef WATCH_SYNC_H
#define WATCH_SYNC_H

#include "watch.h"

enum { WATCH_SYNC_BODY_MAX = sizeof("{\"unixMs\":4102444799999,\"offsetMin\":-720}") - 1 };

typedef struct {
    uint32_t utc;
    int16_t offset_min;
    bool has_offset;
} watch_time_sync_t;

bool watch_sync_parse(const char *body, size_t length, watch_time_sync_t *sync);
bool watch_sync_preferences(watch_t *watch, const watch_time_sync_t *sync);
bool watch_sync_correct(uint32_t raw, uint32_t anchor, int32_t slow_ppm, bool inverse, uint32_t *result);
bool watch_sync_interval(uint32_t first_ref, uint32_t first_raw, uint32_t now_ref, uint32_t now_raw,
                         uint32_t *period, int32_t *raw_error);

#endif
