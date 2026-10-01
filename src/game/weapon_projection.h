#pragma once

#include "camera_math.h"
#include "aim_projection.h"

#include <cameraunlock/ads/ads_fade.h>

namespace StarfieldHT {

inline void CompensateWeaponProjection(const CameraBasis& clean, const CameraBasis& drawn,
                                      float scaleX, float scaleY, float cleanEyeShare, float eye[3],
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
    // Sights locked draws from the clean eye, so a lean leaves the weapon where
    // it is in the frame. The weapon sits about a third of a metre from the eye,
    // so the lean's honest parallax would throw it most of the way across the
    // frame and take the sights off the eye while aiming. True free look draws
    // from the tracked eye and keeps that parallax: the weapon stays put in the
    // world and the head moves around it. cleanEyeShare is 1 in sights locked
    // and 0 in true free look, and between the two while the toggle slides.
    for (int i = 0; i < 3; ++i) eye[i] = drawn.e[i] + (clean.e[i] - drawn.e[i]) * cleanEyeShare;
}

inline bool AlignWeaponAim(const CameraFrame& frame, float distance, float weaponRight, float weaponTop,
                           float cleanEyeShare, NiMatrix44& view, NiMatrix44& inverseView) {
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

    // Drawing the gun from the clean eye removes near-weapon parallax. Restore
    // only the target's screen displacement so locked sights still mark the aim.
    for (int i = 0; i < 4; ++i) {
        view.entry[i][0] += shiftX * view.entry[i][2];
        view.entry[i][1] += shiftY * view.entry[i][2];
        inverseView.entry[2][i] -= shiftX * inverseView.entry[0][i] + shiftY * inverseView.entry[1][i];
    }
    return true;
}

// The toggle moves the weapon's eye by the whole lean, so it rides AdsFade
// rather than stepping: 1 in sights locked, 0 in true free look.
class WeaponEye {
public:
    float CleanEyeShare(bool trueFreeLook, unsigned long long nowMs) {
        return m_fade.Update(trueFreeLook, nowMs);
    }

private:
    cameraunlock::ads::AdsFade m_fade;
};

}
