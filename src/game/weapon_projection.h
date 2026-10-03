#pragma once

#include "camera_math.h"
#include "aim_projection.h"

#include <cameraunlock/ads/ads_fade.h>

namespace StarfieldHT {

// Sights locked draws from the clean eye, so a lean leaves the weapon where
// it is in the frame. The weapon sits about a third of a metre from the eye,
// so the lean's honest parallax would throw it most of the way across the
// frame and take the sights off the eye while aiming. True free look draws
// from the tracked eye and keeps that parallax: the weapon stays put in the
// world and the head moves around it. cleanEyeShare is 1 in sights locked
// and 0 in the free look modes, and between the two while the mode slides.
//
// With the sights up the part of the lean along the aim is kept in every
// mode: the eye moves along the sight line, which leaves it on the sights,
// so the weapon stays put and leaning in brings the sights closer. At the hip
// the weapon comes with the eye along the aim as well, where a forward lean
// would otherwise carry the eye over the top of it. sightsUp is 0 at the hip
// and 1 with the sights fully up.
inline void WeaponPassEye(const CameraBasis& clean, const CameraBasis& drawn, float cleanEyeShare,
                          float sightsUp, float eye[3]) {
    float lean[3];
    for (int i = 0; i < 3; ++i) lean[i] = drawn.e[i] - clean.e[i];
    const float along = Dot3(lean, clean.f) * sightsUp;
    for (int i = 0; i < 3; ++i) {
        eye[i] = drawn.e[i] - (lean[i] - clean.f[i] * along) * cleanEyeShare;
    }
}

inline void CompensateWeaponProjection(const CameraBasis& clean, const CameraBasis& drawn,
                                      float scaleX, float scaleY, float cleanEyeShare, float sightsUp,
                                      float eye[3], NiMatrix44& view, NiMatrix44& inverseView) {
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
    // rotation use the world projection.
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
    WeaponPassEye(clean, drawn, cleanEyeShare, sightsUp, eye);
}

// What the weapon pass did to the held weapon, for anything drawn with the
// world that has to meet the weapon on screen. scale is the world frustum's
// extent over the weapon pass's, and shift is AlignWeaponAim's, in half frames.
struct WeaponPassView {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float cleanEyeShare = 0.0f;
    float shiftX = 0.0f;
    float shiftY = 0.0f;
};

inline bool AlignWeaponAim(const CameraFrame& frame, float distance, float weaponRight, float weaponTop,
                           WeaponPassView& pass, NiMatrix44& view, NiMatrix44& inverseView) {
    const float cleanEyeShare = pass.cleanEyeShare;
    pass.shiftX = pass.shiftY = 0.0f;
    if (cleanEyeShare == 0.0f) return true;
    float aimX = 0, aimY = 0;
    const float depth = Dot3(frame.clean.f, frame.drawn.f);
    if (!std::isfinite(distance) || depth <= 0.01f
        || !ProjectAimAtDistance(frame, distance, aimX, aimY)) return false;
    const float directionX = Dot3(frame.clean.f, frame.drawn.r) / depth / frame.frustumRight;
    const float directionY = Dot3(frame.clean.f, frame.drawn.u) / depth / frame.frustumTop;
    const float shiftX = (aimX - directionX) * weaponRight * cleanEyeShare;
    const float shiftY = (aimY - directionY) * weaponTop * cleanEyeShare;
    if (!std::isfinite(shiftX) || !std::isfinite(shiftY)) return false;
    pass.shiftX = shiftX / weaponRight;
    pass.shiftY = shiftY / weaponTop;

    // Drawing the gun from the clean eye removes near-weapon parallax. Restore
    // only the target's screen displacement so locked sights still mark the aim.
    for (int i = 0; i < 4; ++i) {
        view.entry[i][0] += shiftX * view.entry[i][2];
        view.entry[i][1] += shiftY * view.entry[i][2];
        inverseView.entry[2][i] -= shiftX * inverseView.entry[0][i] + shiftY * inverseView.entry[1][i];
    }
    return true;
}

// The world-space point the world pass draws on the pixel the weapon pass draws
// `point` on. The weapon pass has a narrower projection and, in sights locked,
// an eye of its own, so a point of the weapon is not where the world pass
// would put it.
inline void WeaponPointInWorldPass(const CameraBasis& clean, const CameraBasis& drawn,
                                   float worldRight, float worldTop, float sightsUp,
                                   const WeaponPassView& pass, const float point[3], float out[3]) {
    float eye[3], fromEye[3];
    WeaponPassEye(clean, drawn, pass.cleanEyeShare, sightsUp, eye);
    for (int i = 0; i < 3; ++i) fromEye[i] = point[i] - eye[i];
    const float right = (pass.scaleX - 1.0f) * Dot3(fromEye, clean.r);
    const float up = (pass.scaleY - 1.0f) * Dot3(fromEye, clean.u);
    for (int i = 0; i < 3; ++i) fromEye[i] += right * clean.r[i] + up * clean.u[i];
    const float depth = Dot3(fromEye, drawn.f);
    for (int i = 0; i < 3; ++i) {
        out[i] = drawn.e[i] + fromEye[i]
               + depth * (worldRight * pass.shiftX * drawn.r[i] + worldTop * pass.shiftY * drawn.u[i]);
    }
}

// The aim mode key moves the weapon's eye by the whole lean, so it rides AdsFade
// rather than stepping: 1 in sights locked, 0 in the free look modes.
class WeaponEye {
public:
    float CleanEyeShare(bool trueFreeLook, unsigned long long nowMs) {
        return m_fade.Update(trueFreeLook, nowMs);
    }

private:
    cameraunlock::ads::AdsFade m_fade;
};

}
