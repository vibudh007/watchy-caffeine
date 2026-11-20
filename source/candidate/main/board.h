#ifndef BOARD_H
#define BOARD_H
#include "watch.h"

#define BOARD_RTC_INT 27
#define BOARD_MOTION_INT 14
#define BOARD_BMA_UNINITIALIZED UINT16_MAX

bool board_start(void);
void board_stop(void);
uint8_t board_keys(void);
uint64_t board_wake_mask(uint8_t held);
bool board_rtc_read(uint32_t *utc);
bool board_rtc_write(uint32_t utc);
bool board_rtc_disarm(void);
bool board_rtc_arm(uint32_t target);
int16_t board_battery(void);
bool board_motion(bool steps, bool wake, uint16_t *feature_base);
bool board_step_sample(uint32_t *count);
bool board_motion_ack(void);

#endif
