#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

namespace StarfieldHT {

// Minimal scene-graph types. Only the shapes are declared here; where each one
// sits inside the engine's objects is resolved at runtime - see scene_layout.h.
// Starfield world coordinates: X=east, Y=north, Z=up, one unit = one metre
// (see camera_boundary.h for how that was settled).

struct NiPoint3 {
    float x, y, z;

    NiPoint3() : x(0), y(0), z(0) {}
    NiPoint3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// Row-major 4x4 transform, 64 bytes. Rows 0..2 are the node's own axes
// expressed in its parent's frame with a 0 in the fourth column, row 3 is the
// translation with a 1. Also the shape of the world-to-clip matrix.
struct NiMatrix44 {
    float entry[4][4];
};
static_assert(sizeof(NiMatrix44) == 0x40, "NiMatrix44 size mismatch");

// NiFrustum, in the engine's own member order. The horizontal and vertical
// extents are measured at a near plane normalised to 1, so `right` is
// tan(HFOV/2) and `top` is tan(VFOV/2); `left` and `bottom` mirror them. The
// projection has an infinite far plane, so `farPlane` is used for culling only.
struct NiFrustum {
    float left;
    float right;
    float top;
    float bottom;
    float nearPlane;
    float farPlane;
};
static_assert(sizeof(NiFrustum) == 24, "NiFrustum size mismatch");

} // namespace StarfieldHT
