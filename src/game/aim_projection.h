#pragma once

#include "camera_frame.h"

namespace StarfieldHT {

inline bool ProjectAimAtDistance(const CameraFrame& frame, float distance, float& x, float& y) {
    if (frame.niCamera == 0 || frame.frustumRight <= 0 || frame.frustumTop <= 0 || distance <= 0)
        return false;
    float direction[3];
    for (int i = 0; i < 3; ++i)
        direction[i] = frame.clean.f[i] * distance - (frame.drawn.e[i] - frame.clean.e[i]);
    const float depth = Dot3(direction, frame.drawn.f);
    if (depth <= 0.01f) return false;
    x = Dot3(direction, frame.drawn.r) / depth / frame.frustumRight;
    y = Dot3(direction, frame.drawn.u) / depth / frame.frustumTop;
    return true;
}

}
