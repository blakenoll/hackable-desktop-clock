#pragma once
#include <time.h>

void ui_init();
void ui_update(const tm &t);
void ui_set_status(const char *msg);  // nullptr hides the message
