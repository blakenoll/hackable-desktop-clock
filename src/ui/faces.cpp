// The face table: one FaceDef per clock face, in `enum Face` order (ui.h). A face's layout and behaviour live
// in faces/face_<name>.cpp; to add a face, add a file there, an entry to `enum Face`, and a line here.
#include "ui_internal.h"

namespace ui {

extern const FaceDef faceDefPolished, faceDefArc, faceDefStacked, faceDefAnalog, faceDefZen, faceDefForecast,
    faceDefRetro, faceDefDaylight;

static const FaceDef *const FACES[FACE_COUNT] = {
    &faceDefPolished, &faceDefArc, &faceDefStacked, &faceDefAnalog,
    &faceDefZen,      &faceDefForecast, &faceDefRetro, &faceDefDaylight,
};

const FaceDef &face() { return *FACES[opt.face < FACE_COUNT ? opt.face : 0]; }

}  // namespace ui
