#pragma once

#include "camera_frame.h"

namespace StarfieldHT {

struct ImpactPoint {
    float position[3]{};
    uint32_t originIndex = 0;
};
static_assert(sizeof(ImpactPoint) == 16);

inline bool ProjectImpact(const CameraFrame& frame, const float relativeToEye[3], float& x, float& y) {
    if (frame.niCamera == 0 || frame.frustumRight <= 0 || frame.frustumTop <= 0) return false;
    const float depth = Dot3(relativeToEye, frame.drawn.f);
    if (depth <= 0.01f) return false;
    x = Dot3(relativeToEye, frame.drawn.r) / depth / frame.frustumRight;
    y = Dot3(relativeToEye, frame.drawn.u) / depth / frame.frustumTop;
    return true;
}

}
