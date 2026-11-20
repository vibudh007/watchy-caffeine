#ifndef WATCHY_DEMO_H
#define WATCHY_DEMO_H
#include "frame.h"

bool demo_seed(uint32_t *utc, int16_t *offset, bool *portal);
void demo_emit(const uint8_t pixels[WATCH_FRAME_SIZE]);
bool demo_reboot_begin(int16_t *offset);
void demo_reboot_finish(uint32_t utc, bool external);
void demo_reboot_report(void);
void demo_restart_if_held(uint8_t buttons, int16_t offset);

#endif
