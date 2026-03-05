#pragma once

#include <LilyGo_AMOLED.h>

// The display module controls brightness via the hardware object.
// amoled is defined in main.cpp and shared here.
extern LilyGo_Class amoled;

// Build the full LVGL UI and register all timers/event callbacks.
void build_ui();
