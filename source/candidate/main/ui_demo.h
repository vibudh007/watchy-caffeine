#ifndef WATCHY_UI_DEMO_H
#define WATCHY_UI_DEMO_H

#include "watch.h"

bool ui_demo_run(watch_t *watch, void (*press)(enum watch_key), bool (*capture)(const char *));

#endif
