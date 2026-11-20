#include "sync.h"

#include <string.h>

bool watch_sync_parse(const char *body, size_t length, watch_time_sync_t *sync)
{
    static const char prefix[] = "{\"unixMs\":";
    static const char offset_key[] = ",\"offsetMin\":";
    const size_t start = sizeof prefix - 1;
    if (length < start + 13 || length > WATCH_SYNC_BODY_MAX || memcmp(body, prefix, start) ||
        body[length - 1] != '}' || body[start] == '0') {
        return false;
    }
    const size_t end = length - 1;
    size_t i = start;
    uint64_t ms = 0;
    while (i < end && i - start < 13 && body[i] >= '0' && body[i] <= '9') {
        ms = ms * 10 + (unsigned)(body[i] - '0');
        i++;
    }
    uint64_t seconds = ms / 1000;
    if (i - start < 12 || seconds < WATCH_FIRST_UTC || seconds > WATCH_LAST_UTC) {
        return false;
    }
    watch_time_sync_t parsed = {.utc = (uint32_t)seconds};
    if (i < end) {
        size_t key_length = sizeof offset_key - 1;
        if (end - i <= key_length || memcmp(body + i, offset_key, key_length)) {
            return false;
        }
        i += key_length;
        bool negative = body[i] == '-';
        i += negative;
        if (i == end || end - i > 3 || (body[i] == '0' && (negative || end - i > 1))) {
            return false;
        }
        int minutes = 0;
        for (; i < end; i++) {
            if (body[i] < '0' || body[i] > '9') {
                return false;
            }
            minutes = minutes * 10 + body[i] - '0';
        }
        if (negative) minutes = -minutes;
        if (!watch_offset_valid(minutes)) {
            return false;
        }
        parsed.offset_min = (int16_t)minutes;
        parsed.has_offset = true;
    }
    *sync = parsed;
    return true;
}

bool watch_sync_preferences(watch_t *watch, const watch_time_sync_t *sync)
{
    if (!sync->has_offset || sync->offset_min == watch->settings.offset_min) {
        return false;
    }
    watch->pending = watch->settings;
    watch->pending.offset_min = sync->offset_min;
    return true;
}

bool watch_sync_correct(uint32_t raw, uint32_t anchor, int32_t slow_ppm, bool inverse, uint32_t *result)
{
    if (!watch_utc_valid(raw) || !watch_utc_valid(anchor) || slow_ppm < -1000 || slow_ppm > 1000) {
        return false;
    }
    int64_t delta = (int64_t)raw - anchor;
    int64_t denominator = inverse ? 1000000 : 1000000 - slow_ppm;
    int64_t numerator = inverse ? (1000000 - slow_ppm) : 1000000;
    int64_t scaled = delta * numerator;
    int64_t corrected = anchor + (scaled + (scaled < 0 ? -denominator / 2 : denominator / 2)) / denominator;
    if (corrected < WATCH_FIRST_UTC || corrected > WATCH_LAST_UTC) {
        return false;
    }
    *result = (uint32_t)corrected;
    return true;
}

bool watch_sync_interval(uint32_t first_ref, uint32_t first_raw, uint32_t now_ref, uint32_t now_raw,
                         uint32_t *period, int32_t *raw_error)
{
    if (!watch_utc_valid(first_ref) || !watch_utc_valid(first_raw) || !watch_utc_valid(now_ref) ||
        !watch_utc_valid(now_raw) || now_ref <= first_ref || now_raw <= first_raw) {
        return false;
    }
    uint32_t span = now_ref - first_ref;
    int64_t difference = (int64_t)(now_raw - first_raw) - span;
    if (span < 7 * 86400 || difference < -(int64_t)span / 1000 || difference > span / 1000) {
        return false;
    }
    *period = span;
    *raw_error = (int32_t)difference;
    return true;
}
