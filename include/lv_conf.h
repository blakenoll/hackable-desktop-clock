// Minimal LVGL 9 config; everything unset falls back to lv_conf_internal.h defaults.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64 * 1024)

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 33

#define LV_FONT_MONTSERRAT_14 1

// Widgets used by the clock face
#define LV_USE_SCALE 0
#define LV_USE_LINE 1   // seconds-ring ticks
#define LV_USE_LABEL 1
#define LV_USE_CANVAS 1
#define LV_USE_QRCODE 1   // WiFi-setup join code

#endif
#endif
