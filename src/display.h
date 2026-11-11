#pragma once

// Brings up the GC9A01 panel (TFT_eSPI) and registers it with LVGL as a 240x240 display.
// Call once, before ui_init().
void display_init();
