#include "ui_internal.h"

namespace ui {

// ---- Small helpers -----------------------------------------------------------------------
lv_obj_t *box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, int spacing) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  if (spacing) lv_obj_set_style_text_letter_space(l, spacing, 0);
  lv_label_set_text(l, "");
  return l;
}

lv_obj_t *mkRow(lv_obj_t *parent, int w, int x, int y, int gap, bool column) {
  lv_obj_t *o = box(parent);
  lv_obj_set_size(o, w, LV_SIZE_CONTENT);
  lv_obj_align(o, LV_ALIGN_CENTER, x, y);
  lv_obj_set_flex_flow(o, column ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(o, gap, 0);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}

lv_obj_t *mkDisc(lv_obj_t *parent, int size) {
  lv_obj_t *o = box(parent);
  lv_obj_set_size(o, size, size);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

void tc(lv_obj_t *o, lv_color_t c) { if (o) lv_obj_set_style_text_color(o, c, 0); }

void bgc(lv_obj_t *o, lv_color_t c) { if (o) lv_obj_set_style_bg_color(o, c, 0); }

void setText(lv_obj_t *o, const char *t) { if (o) lv_label_set_text(o, t); }

void setHidden(lv_obj_t *o, bool hide) {
  if (!o) return;
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

// Point at `r` pixels from the centre, `deg` clockwise from 12 o'clock.
void polar(float r, float deg, float &x, float &y) {
  float a = deg * (float)M_PI / 180.0f;
  x = RING_CENTER + r * sinf(a);
  y = RING_CENTER - r * cosf(a);
}

void setOpa(lv_obj_t *o, lv_opa_t to, uint32_t ms) {
  if (!o) return;
  lv_anim_delete(o, nullptr);
  if (!ms) {
    lv_obj_set_style_opa(o, to, 0);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t x) { lv_obj_set_style_opa((lv_obj_t *)v, (lv_opa_t)x, 0); });
  lv_anim_set_values(&a, lv_obj_get_style_opa(o, LV_PART_MAIN), to);
  lv_anim_set_duration(&a, ms);
  lv_anim_start(&a);
}

}  // namespace ui
