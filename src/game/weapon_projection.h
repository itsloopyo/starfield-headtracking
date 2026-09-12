#pragma once

#include "camera_math.h"

namespace StarfieldHT {

inline void CompensateWeaponProjection(const CameraBasis& clean, const CameraBasis& drawn,
                                      float scaleX, float scaleY, float eye[3],
                                      NiMatrix44& view, NiMatrix44& inverseView) {
    Mat3 stretch{}, inverseStretch{}, drawnView{};
    for (int i = 0; i < 3; ++i) {
        drawnView.m[i][0] = drawn.r[i];
        drawnView.m[i][1] = drawn.u[i];
        drawnView.m[i][2] = drawn.f[i];
        for (int j = 0; j < 3; ++j) {
            const float identity = i == j ? 1.0f : 0.0f;
            stretch.m[i][j] = identity + (scaleX - 1.0f) * clean.r[i] * clean.r[j]
                                      + (scaleY - 1.0f) * clean.u[i] * clean.u[j];
            inverseStretch.m[i][j] = identity + (1.0f / scaleX - 1.0f) * clean.r[i] * clean.r[j]
                                             + (1.0f / scaleY - 1.0f) * clean.u[i] * clean.u[j];
        }
    }
    // Preserve the gun's stock projection in the clean camera, then let head
    // movement use the world projection. Stretching about the drawn eye would
    // incorrectly scale a positional lean as well as the weapon.
    const Mat3 adjusted = Mul(stretch, drawnView);
    const Mat3 inverse = Mul(Transpose(drawnView), inverseStretch);
    const float scale[3] = {scaleX, scaleY, 1.0f};
    view = {};
    inverseView = {};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            view.entry[i][j] = adjusted.m[i][j] / scale[j];
            inverseView.entry[i][j] = inverse.m[i][j] * scale[i];
        }
    }
    view.entry[3][3] = inverseView.entry[3][3] = 1.0f;
    const float lean[3] = {drawn.e[0] - clean.e[0], drawn.e[1] - clean.e[1], drawn.e[2] - clean.e[2]};
    MulRowVec(lean, inverseStretch, eye);
    for (int i = 0; i < 3; ++i) eye[i] += clean.e[i];
}

}
