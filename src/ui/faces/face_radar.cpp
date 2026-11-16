#include "../ui_internal.h"
#include "../../data/aircraft.h"

namespace ui {

// Radar scope: live aircraft around home (ADS-B from adsb.fi, see aircraft.cpp) on range rings, north up, with a
// rotating sweep that lights each blip as it passes (the blip then fades like phosphor until the next pass). Each
// blip has a short leader line along its track and is dead-reckoned every second between the 10 s fetches. The
// nearest few carry their callsign, the nearest is ringed and spelled out under the scope. Night: the sweep stops
// and the blips stay at a steady dim red.
static constexpr float R = 112;            // scope radius, px (= the query range)
static constexpr int RINGS = 4;            // range rings at 1/4 .. 4/4 of the range
static constexpr int MAXP = AircraftData::MAX;
static constexpr int TAGS = 3;             // nearest aircraft that get a callsign
static constexpr int BOX = 29, MID = 14;   // blip container (leader line + dot) and its centre
static constexpr float LEAD = 12;          // track leader length from the dot's centre, px
static constexpr uint32_t SWEEP_MS = 4000; // one revolution
static constexpr int TRAIL = 4;            // sweep afterglow segments
static constexpr uint32_t PHOSPHOR = 0x2EE59D;

static lv_obj_t *rings[RINGS], *crossH, *crossV, *home, *sweepBox, *sweep[TRAIL];
static lv_obj_t *blip[MAXP], *blipDot[MAXP], *blipLead[MAXP], *tag[TAGS], *target;
static lv_obj_t *nearLabel, *infoLabel, *countLabel, *msgLabel;
static lv_point_precise_t leadPts[MAXP][2];  // must outlive the line objects
static int blipX[MAXP], blipY[MAXP];            // dot centres on screen
static bool blipOn[MAXP];
static float blipDeg[MAXP];                  // bearing of each shown blip (for the sweep brightness)
static lv_opa_t blipOpa[MAXP];
static float sweepDeg = 0;

// Cached data (survives rebuilds).
static Aircraft planes[MAXP];
static int planeN = 0, planeTotal = 0, rangeNm = RADAR_NM;
static uint32_t planesAt = 0;
static char radarMsg[24] = "SCANNING";

static bool quiet() { return nightMix == 255; }  // fully night: nothing animates

// ---- Sweep -----------------------------------------------------------------------------------------
// The beam is a few wedge-shaped arcs (arc width = radius): a bright narrow head, then wider, dimmer trail.
static const struct { float from, to; lv_opa_t opa; } SEG[TRAIL] = {{0, 3, 120}, {3, 12, 64}, {12, 30, 32}, {30, 60, 14}};

static lv_opa_t blipBright(float behindDeg) {  // 0 = the beam is on it, 360 = about to be hit again
  int o = 255 - (int)(behindDeg * (255 - 100) / 360);  // floor 100: leaders stay readable between passes
  return (lv_opa_t)(o & ~7);  // steps of 8: fewer redraws
}

static void applyBlipOpa(int i, lv_opa_t o) {
  if (blipOpa[i] == o) return;
  blipOpa[i] = o;
  lv_obj_set_style_opa(blip[i], o, 0);
  if (i < TAGS) lv_obj_set_style_opa(tag[i], o, 0);
}

static void sweepExec(void *, int32_t v) {
  if (!sweepBox || quiet()) return;
  sweepDeg = v / 10.0f;
  for (int k = 0; k < TRAIL; k++) {
    // lv_arc angles: 0 = 3 o'clock, clockwise; rotation 270 makes 0 = 12 o'clock. The wedge trails the beam.
    int a1 = (int)lroundf(sweepDeg - SEG[k].from), a0 = (int)lroundf(sweepDeg - SEG[k].to);
    lv_arc_set_angles(sweep[k], (a0 + 360) % 360, (a1 + 360) % 360);
  }
  for (int i = 0; i < MAXP; i++) {
    if (lv_obj_has_flag(blip[i], LV_OBJ_FLAG_HIDDEN)) continue;
    applyBlipOpa(i, blipBright(fmodf(sweepDeg - blipDeg[i] + 720, 360)));
  }
}

static void sweepRun(bool on) {
  if (!sweepBox) return;
  bool running = lv_anim_get(sweepBox, sweepExec) != nullptr;
  setHidden(sweepBox, !on);
  if (on == running) return;
  if (!on) {
    lv_anim_delete(sweepBox, sweepExec);
    for (int i = 0; i < MAXP; i++) applyBlipOpa(i, LV_OPA_COVER);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sweepBox);  // deleted along with sweepBox when the face is torn down
  lv_anim_set_exec_cb(&a, sweepExec);
  lv_anim_set_values(&a, 0, 3600);
  lv_anim_set_duration(&a, SWEEP_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

// ---- Drawing -----------------------------------------------------------------------------------------
static void radarPaint() {
  if (!sweepBox) return;
  const Theme &T = THEMES[opt.theme];
  for (int i = 0; i < RINGS; i++)
    lv_obj_set_style_border_color(rings[i], mixDN(scaleHex(PHOSPHOR, i % 2 ? 34 : 20), nightC(i % 2 ? 20 : 13)), 0);
  bgc(crossH, mixDN(scaleHex(PHOSPHOR, 13), nightC(9)));
  bgc(crossV, mixDN(scaleHex(PHOSPHOR, 13), nightC(9)));
  bgc(home, mixDN(PHOSPHOR, nightC(60)));
  for (int k = 0; k < TRAIL; k++) lv_obj_set_style_arc_color(sweep[k], mixDN(PHOSPHOR, nightC(60)), LV_PART_INDICATOR);
  for (int i = 0; i < MAXP; i++) {
    bgc(blipDot[i], mixDN(0xDDFFEF, nightC(92)));
    lv_obj_set_style_line_color(blipLead[i], mixDN(PHOSPHOR, nightC(70)), 0);
  }
  for (int i = 0; i < TAGS; i++) tc(tag[i], mixDN(scaleHex(PHOSPHOR, 85), nightC(60)));
  lv_obj_set_style_border_color(target, mixDN(T.accent, nightC(80)), 0);
  tc(nearLabel, mixDN(T.accent, nightC(85)));
  tc(infoLabel, mixDN(T.sub, nightC(69)));
  tc(countLabel, mixDN(T.colon, nightC(45)));
  tc(msgLabel, mixDN(T.sub, nightC(69)));
  sweepRun(!quiet());
}

static void fmtThousands(int v, char *out, size_t n) {  // 12500 -> "12,500"
  if (v >= 1000) snprintf(out, n, "%d,%03d", v / 1000, v % 1000);
  else snprintf(out, n, "%d", v);
}

// Callsigns for the nearest few: beside the dot, on whichever side (and above / below) is inside the bezel and
// clear of the tags already placed and of the other dots; none fits -> no tag.
static void placeTags() {
  lv_area_t placed[TAGS];
  int np = 0;
  for (int i = 0; i < TAGS; i++) {
    bool shown = false;
    if (blipOn[i]) {
      setText(tag[i], planes[i].flight);
      lv_obj_update_layout(tag[i]);
      int w = lv_obj_get_width(tag[i]), h = 12;
      int bx = blipX[i], by = blipY[i];
      // The plane's own leader: a tag over it reads as "/JBU106". Sample its far half (the near half is beside the dot).
      float lt = planes[i].track * (float)M_PI / 180, ll = planes[i].gs > 30 ? LEAD : 0;
      // Try the side away from the leader first (the inward side when it points mostly up or down).
      bool leftFirst = ll > 0 && fabsf(sinf(lt)) > 0.3f ? sinf(lt) > 0 : bx > RING_CENTER + 30;
      for (int c = 0; c < 4 && !shown; c++) {
        bool left = (c % 2 == 0) == leftFirst;
        lv_area_t r = {left ? bx - 6 - w : bx + 6, c < 2 ? by - 14 : by + 3, 0, 0};
        r.x2 = r.x1 + w - 1;
        r.y2 = r.y1 + h - 1;
        bool ok = true;
        for (float f : {0.5f, 0.75f, 1.0f}) {
          int lx = bx + (int)lroundf(f * ll * sinf(lt)), ly = by - (int)lroundf(f * ll * cosf(lt));
          if (lx >= r.x1 - 2 && lx <= r.x2 + 2 && ly >= r.y1 - 1 && ly <= r.y2 + 1) ok = false;
        }
        for (int cx : {(int)r.x1, (int)r.x2})
          for (int cy : {(int)r.y1, (int)r.y2})
            if ((cx - RING_CENTER) * (cx - RING_CENTER) + (cy - RING_CENTER) * (cy - RING_CENTER) > 114 * 114) ok = false;
        for (int k = 0; k < np && ok; k++)
          if (r.x1 <= placed[k].x2 + 3 && placed[k].x1 <= r.x2 + 3 && r.y1 <= placed[k].y2 + 1 && placed[k].y1 <= r.y2 + 1) ok = false;
        for (int k = 0; k < planeN && ok; k++)
          if (k != i && blipOn[k] && blipX[k] >= r.x1 - 3 && blipX[k] <= r.x2 + 3 && blipY[k] >= r.y1 - 3 && blipY[k] <= r.y2 + 3) ok = false;
        if (!ok) continue;
        lv_obj_set_pos(tag[i], r.x1, r.y1 - 2);  // the caps sit ~2 px below the label's top
        placed[np++] = r;
        shown = true;
      }
    }
    setHidden(tag[i], !shown);
  }
}

static void radarUpdate() {
  if (!sweepBox) return;
  bool have = planeN > 0;
  setText(msgLabel, planeN ? "" : radarMsg[0] ? radarMsg : "NO AIRCRAFT IN RANGE");
  setHidden(msgLabel, have);
  for (lv_obj_t *o : {nearLabel, infoLabel, target}) setHidden(o, !have);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d AIRCRAFT \xC2\xB7 %d NM", planeTotal, rangeNm);
  setText(countLabel, buf);
  setHidden(countLabel, radarMsg[0] != 0);

  // Dead reckoning since the fetch (capped: past a minute the guess is worse than the old fix).
  float age = (millis() - planesAt) / 1000.0f;
  if (age > 60) age = 60;
  float scale = R / rangeNm;
  for (int i = 0; i < MAXP; i++) {
    bool on = i < planeN;
    float px = 0, py = 0;
    if (on) {
      const Aircraft &a = planes[i];
      float t = a.track * (float)M_PI / 180, d = a.gs * age / 3600;
      float x = a.x + d * sinf(t), y = a.y + d * cosf(t);
      px = x * scale;
      py = -y * scale;
      on = px * px + py * py < (R - 3) * (R - 3);  // drifted off the scope
      blipDeg[i] = fmodf(atan2f(px, -py) * 180 / (float)M_PI + 360, 360);
      // Move first, then the leader: its redraw then falls inside the box's own (deduplicated by LVGL), so a
      // second's update costs ~2 dirty areas per blip instead of 4 (see LV_INV_BUF_SIZE in lv_conf.h).
      lv_obj_set_pos(blip[i], (int)lroundf(RING_CENTER + px) - MID, (int)lroundf(RING_CENTER + py) - MID);
      // Leader: LEAD px along the track (from the dot's centre), none when stationary. Unchanged -> left alone.
      float L = a.gs > 30 ? LEAD : 0;
      lv_point_precise_t end = {(lv_value_precise_t)lroundf(MID + L * sinf(t)), (lv_value_precise_t)lroundf(MID - L * cosf(t))};
      if (end.x != leadPts[i][1].x || end.y != leadPts[i][1].y) {
        leadPts[i][0] = {MID, MID};
        leadPts[i][1] = end;
        lv_line_set_points(blipLead[i], leadPts[i], 2);
      }
    }
    setHidden(blip[i], !on);
    blipX[i] = (int)lroundf(RING_CENTER + px);
    blipY[i] = (int)lroundf(RING_CENTER + py);
    blipOn[i] = on;
    if (on && i == 0) {
      lv_obj_set_pos(target, (int)lroundf(RING_CENTER + px) - 6, (int)lroundf(RING_CENTER + py) - 6);
    }
    if (i == 0) setHidden(target, !on);
  }

  placeTags();

  if (have) {  // "UAL2609 · B39M", then "4,950 FT · 3.2 NM"
    const Aircraft &a = planes[0];
    snprintf(buf, sizeof(buf), "%s%s%s", a.flight, a.type[0] ? " \xC2\xB7 " : "", a.type);
    setText(nearLabel, buf);
    char alt[16] = "";
    if (a.alt > 0) { fmtThousands(a.alt, alt, sizeof(alt)); strlcat(alt, " FT", sizeof(alt)); }
    float t = a.track * (float)M_PI / 180, d = a.gs * age / 3600;
    float dist = hypotf(a.x + d * sinf(t), a.y + d * cosf(t));
    snprintf(buf, sizeof(buf), "%s%s%.1f NM", alt, alt[0] ? " \xC2\xB7 " : "", dist);
    setText(infoLabel, buf);
  }
}

static void build() {
  mkDateRow(-90, 5, 1);
  mkTime(-66, &manrope_34);
  mkOffline(-66, -66);

  for (int i = 0; i < RINGS; i++) {
    int d = (int)lroundf(2 * R * (i + 1) / RINGS);
    rings[i] = box(content);
    lv_obj_set_size(rings[i], d + 1, d + 1);
    lv_obj_center(rings[i]);
    lv_obj_set_style_radius(rings[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(rings[i], 1, 0);
    lv_obj_set_style_border_opa(rings[i], LV_OPA_COVER, 0);
  }
  for (lv_obj_t **o : {&crossH, &crossV}) {
    *o = box(content);
    lv_obj_set_style_bg_opa(*o, LV_OPA_COVER, 0);
  }
  lv_obj_set_size(crossH, (int)(2 * R), 1);
  lv_obj_set_size(crossV, 1, (int)(2 * R));
  lv_obj_center(crossH);
  lv_obj_center(crossV);

  sweepBox = box(content);
  lv_obj_set_size(sweepBox, 240, 240);
  for (int k = 0; k < TRAIL; k++) {
    lv_obj_t *a = sweep[k] = lv_arc_create(sweepBox);
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, (int)(2 * R), (int)(2 * R));
    lv_obj_center(a);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 0);
    // 1 px short of the radius: at width >= radius LVGL invalidates the whole arc box on every angle change
    // (lv_draw_arc_get_area), i.e. the full scope each frame. The 1 px hole at the centre sits under `home`.
    lv_obj_set_style_arc_width(a, (int)R - 1, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, SEG[k].opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_arc_set_angles(a, 0, 0);
  }
  home = mkDisc(content, 5);
  lv_obj_center(home);

  target = mkDisc(content, 13);
  lv_obj_set_style_bg_opa(target, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(target, 1, 0);
  lv_obj_set_style_border_opa(target, LV_OPA_COVER, 0);
  for (int i = 0; i < MAXP; i++) {
    blip[i] = box(content);
    lv_obj_set_size(blip[i], BOX, BOX);
    blipLead[i] = lv_line_create(blip[i]);
    lv_obj_remove_style_all(blipLead[i]);
    lv_obj_set_style_line_width(blipLead[i], 2, 0);
    lv_obj_set_style_line_rounded(blipLead[i], true, 0);
    leadPts[i][0] = leadPts[i][1] = {MID, MID};  // no leader until radarUpdate() sets one
    blipDot[i] = mkDisc(blip[i], 5);
    lv_obj_set_pos(blipDot[i], MID - 2, MID - 2);
    blipOpa[i] = LV_OPA_COVER;
    lv_obj_add_flag(blip[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < TAGS; i++) {
    tag[i] = mkLabel(content, &manrope_caps_12);
    lv_obj_add_flag(tag[i], LV_OBJ_FLAG_HIDDEN);
  }
  msgLabel = mkLabelAt(&manrope_caps_12, 1, 0, 24);
  nearLabel = mkLabelAt(&manrope_caps_12, 1, 0, 58);
  infoLabel = mkLabelAt(&manrope_caps_12, 1, 0, 75);
  countLabel = mkLabelAt(&manrope_caps_12, 0, 0, 92);
}

static void palette(Palette &p, const Theme &) { p.day = p.sub; }
static void paint(const Theme &, const Palette &) { radarPaint(); }
static void onTime(const tm &, bool) { radarUpdate(); }  // dead reckoning: every second
static void onData() { radarUpdate(); }

static FaceDef make() {
  FaceDef f;
  f.name = "Radar";
  f.ring = K_NONE;  // the scope is the face
  f.build = build;
  f.date = dateQuietLine;
  f.palette = palette;
  f.paint = paint;
  f.onTime = onTime;
  f.onData = onData;
  return f;
}
extern const FaceDef faceDefRadar = make();

}  // namespace ui

// ---- Public API (ui.h) ----
using namespace ui;

void ui_set_aircraft(const Aircraft *ac, int n, int total, uint32_t at, int range, const char *msg) {
  planeN = 0;
  for (int i = 0; ac && i < n && i < MAXP; i++) planes[planeN++] = ac[i];
  planeTotal = total;
  planesAt = at;
  rangeNm = range > 0 ? range : RADAR_NM;
  snprintf(radarMsg, sizeof(radarMsg), "%s", msg ? msg : "");
  if (face().onData) face().onData();  // only the active face's hook runs, so no stale pointers are touched
}
