#include "ui_internal.h"

namespace ui {

lv_obj_t *timeH, *timeC, *timeM, *ampmLabel, *secLabel, *dayLabel, *dateLabel, *date2Label;
bool floatAmpm;

tm lastTm;
bool haveTm = false;
static int lastMin = -1, lastHour = -1, lastDay = -1;  // ui_update caches; reset on rebuild

lv_obj_t *mkLabelAt(const lv_font_t *font, int spacing, int x, int y) {
  lv_obj_t *l = mkLabel(content, font, spacing);
  lv_obj_align(l, LV_ALIGN_CENTER, x, y);
  return l;
}

// ---- Shared pieces of a face ---------------------------------------------------------------------
void mkTime(int y, const lv_font_t *font) {  // hours, colon, minutes in a centred row
  lv_obj_t *row = mkRow(content, 200, 0, y, 0);
  timeH = mkLabel(row, font);
  timeC = mkLabel(row, font);
  lv_label_set_text(timeC, ":");
  timeM = mkLabel(row, font);
  // The small AM/PM (and optional seconds) are NOT in the row (they would pull the digits off-centre);
  // placeAmpm() hangs them off the right of the minutes.
  floatAmpm = true;
  ampmLabel = mkLabel(content, &manrope_caps_12, 1);
  if (opt.seconds) secLabel = mkLabel(content, &manrope_14);
}

void mkDateRow(int y, int gap, int spacing) {  // weekday + date side by side, small caps
  lv_obj_t *row = mkRow(content, 190, 0, y, gap);
  dayLabel = mkLabel(row, &manrope_caps_12, spacing);
  dateLabel = mkLabel(row, &manrope_caps_12, spacing);
}

void fmtHour(const tm &t, char *out, size_t n) {
  if (opt.h24) snprintf(out, n, "%02d", t.tm_hour);
  else snprintf(out, n, "%d", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12);
}

void dateQuietLine(const DateParts &d) {
  char line[12];
  snprintf(line, sizeof(line), "%s \xC2\xB7", d.wday);
  setText(dayLabel, line);
  setText(dateLabel, d.line);
}

void timeReset() {
  timeH = timeC = timeM = ampmLabel = secLabel = dayLabel = dateLabel = date2Label = nullptr;
  floatAmpm = false;
  lastMin = lastHour = lastDay = -1;
}

static void dateUpdate(const tm &t) {
  const FaceDef &f = face();
  DateParts d{t};
  strftime(d.wday, sizeof(d.wday), f.fullDate ? "%A" : "%a", &t);  // "TUESDAY" / "NOVEMBER 10" on faces that want it
  strftime(d.mon, sizeof(d.mon), f.fullDate ? "%B" : "%b", &t);
  upper(d.wday);
  upper(d.mon);
  snprintf(d.num, sizeof(d.num), "%d", t.tm_mday);  // %d in strftime would zero-pad single digits
  if (opt.dateFmt == 1) snprintf(d.line, sizeof(d.line), "%s %s", d.num, d.mon);
  else snprintf(d.line, sizeof(d.line), "%s %s", d.mon, d.num);
  if (f.date) {
    f.date(d);
  } else {
    setText(dayLabel, d.wday);
    setText(dateLabel, d.line);
  }
}

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

void ui_update(const tm &tIn) {
  tm t = tIn;  // may alias lastTm
  lastTm = t;
  if (!haveTm) {
    haveTm = true;
    lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
  }

  ringUpdate(t.tm_sec);
  if (secLabel) {
    char ss[4];
    snprintf(ss, sizeof(ss), "%02d", t.tm_sec);
    lv_label_set_text(secLabel, ss);
  }

  bool minuteChanged = t.tm_min != lastMin || t.tm_hour != lastHour;  // the hour alone can change (clock set)
  if (minuteChanged) {  // redraw the big text only when the minute changes
    bool first = lastMin < 0;
    lastMin = t.tm_min;
    lastHour = t.tm_hour;
    char hh[8], mm[8];
    fmtHour(t, hh, sizeof(hh));
    snprintf(mm, sizeof(mm), "%02d", t.tm_min);
    setText(ampmLabel, opt.h24 ? "" : (t.tm_hour < 12 ? "AM" : "PM"));
    if (first) {  // no transition on the very first draw
      setText(timeH, hh);
      if (face().onText) face().onText(timeH);
      setText(timeM, mm);
      placeAmpm();
    } else {
      swapText(SW_HOURS, timeH, hh);
      swapText(SW_MINUTES, timeM, mm);
      if (t.tm_min == 0) startChime();
    }
  }
  if (face().onTime) face().onTime(t, minuteChanged);

  if (t.tm_yday != lastDay) {
    lastDay = t.tm_yday;
    dateUpdate(t);
  }
}
