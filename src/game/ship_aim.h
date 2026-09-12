#pragma once

#include "camera_frame.h"

namespace StarfieldHT {

inline CameraFrame MakeShipCameraFrame(const CameraFrame& source, const Mat3& shipRotation) {
    float difference[3], cleanEye[3];
    for (int i = 0; i < 3; ++i) difference[i] = source.clean.e[i] - source.drawn.e[i];
    MulRowVec(difference, shipRotation, cleanEye);
    const float zero[3]{};
    CameraFrame converted{};
    converted.niCamera = source.niCamera;
    // The exterior origin can change before firing. Retain relative lean and
    // match rotation, then recover the clean eye from the firing camera's origin.
    BasisFromRotation(Mul(RotationOfBasis(source.clean), shipRotation), cleanEye, converted.clean);
    BasisFromRotation(Mul(RotationOfBasis(source.drawn), shipRotation), zero, converted.drawn);
    return converted;
}

}
