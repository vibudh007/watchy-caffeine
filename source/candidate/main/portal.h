#ifndef PORTAL_H
#define PORTAL_H
#include "frame.h"
#include "sync.h"

enum portal_sync_status {
    PORTAL_SYNC_WAITING, PORTAL_SYNC_EXTERNAL, PORTAL_SYNC_BACKUP, PORTAL_SYNC_OFFSET_FAILED,
};

bool portal_start(void);
bool portal_request(watch_time_sync_t *sync);
void portal_clock_result(enum portal_sync_status status, int16_t offset_min,
                         uint32_t observation_seconds, int32_t rtc_error);
bool portal_finished(void);
void portal_stop(void);
bool portal_frame(uint8_t frame[WATCH_FRAME_SIZE]);

#endif
