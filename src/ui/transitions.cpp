#include "ui_internal.h"

namespace ui {

// ---- Text transitions: the changed label fades out, swaps text, fades back in ----
uint32_t iconDayColor = 0xFFFFFF;
uint32_t pendingIconColor;

struct Swap { lv_obj_t *label; char text[32]; };
static Swap swaps[SW_COUNT];

void transitionsReset() { memset(swaps, 0, sizeof(swaps)); }

void placeAmpm() {
  if (!floatAmpm || !timeM) return;
  lv_obj_update_layout(lv_obj_get_parent(timeM));  // digits changed width: re-place AM/PM beside them
  if (ampmLabel) lv_obj_align_to(ampmLabel, timeM, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, 3);
  if (secLabel) lv_obj_align_to(secLabel, timeM, LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
}

// Weather fades rise a few pixels; the time digits crossfade in place (movement made the whole string bounce).
static int riseFor(void *o) { return (o == timeH || o == timeM) ? 0 : 6; }

static void fadeOutExec(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
  lv_obj_set_style_translate_y((lv_obj_t *)o, -(255 - v) * riseFor(o) / 255, 0);
}

static void fadeInExec(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
  lv_obj_set_style_translate_y((lv_obj_t *)o, (255 - v) * riseFor(o) / 255, 0);
}

// Fade an object in (rising slightly). Starts fully transparent so there is no one-frame flash.
void fadeIn(lv_obj_t *o) {
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
  lv_anim_t in;
  lv_anim_init(&in);
  lv_anim_set_var(&in, o);
  lv_anim_set_exec_cb(&in, fadeInExec);
  lv_anim_set_values(&in, 0, 255);
  lv_anim_set_duration(&in, 260);
  lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
  lv_anim_start(&in);
}

static void swapOutDone(lv_anim_t *a) {
  Swap *sw = (Swap *)lv_anim_get_user_data(a);
  lv_label_set_text(sw->label, sw->text);
  if (face().onText) face().onText(sw->label);
  if (sw->label == weatherIcon) {  // new glyph arrives with its own colour
    iconDayColor = pendingIconColor;
    applyIconColor();
  }
  placeAmpm();
  fadeIn(sw->label);
}

// Text change with a quick fade-out / fade-in. A hidden label just appears (fade-in only).
void swapText(int idx, lv_obj_t *label, const char *text) {
  if (!label) return;
  if (lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
    lv_anim_delete(label, nullptr);
    lv_label_set_text(label, text);
    if (face().onText) face().onText(label);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    if (label == weatherIcon) { iconDayColor = pendingIconColor; applyIconColor(); }
    fadeIn(label);
    return;
  }
  if (!strcmp(lv_label_get_text(label), text)) return;
  swaps[idx].label = label;
  strncpy(swaps[idx].text, text, sizeof(swaps[idx].text) - 1);
  swaps[idx].text[sizeof(swaps[idx].text) - 1] = 0;
  lv_anim_delete(label, nullptr);
  lv_anim_t out;
  lv_anim_init(&out);
  lv_anim_set_var(&out, label);
  lv_anim_set_exec_cb(&out, fadeOutExec);
  lv_anim_set_values(&out, 255, 0);
  lv_anim_set_duration(&out, 160);
  lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
  lv_anim_set_user_data(&out, &swaps[idx]);
  lv_anim_set_completed_cb(&out, swapOutDone);
  lv_anim_start(&out);
}

}  // namespace ui
