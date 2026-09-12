#pragma once

#include <cmath>

#include "game/starfield_types.h"

namespace StarfieldHT {

// The engine's transforms multiply as row vectors: a direction in a node's own
// axes becomes a world direction as v * M, and a node's world transform is
// local * parentWorld. Mat3 carries the rotation half of one of those.
struct Mat3 {
    float m[3][3];
};

inline Mat3 RotationOf(const NiMatrix44& t) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = t.entry[i][j];
    return r;
}

inline Mat3 Mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

inline Mat3 Transpose(const Mat3& a) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
    return r;
}

// v * m, the row-vector product.
inline void MulRowVec(const float v[3], const Mat3& m, float out[3]) {
    for (int j = 0; j < 3; ++j) {
        out[j] = v[0] * m.m[0][j] + v[1] * m.m[1][j] + v[2] * m.m[2][j];
    }
}

// The camera node's world transform, unpacked. Every vector is world space.
// The node's local axes are x=forward, y=up, z=right, so these are rows 0, 1
// and 2 of the 4x4; row 3 is the eye.
struct CameraBasis {
    float f[3];
    float u[3];
    float r[3];
    float e[3];
};

inline void ReadBasis(const NiMatrix44& m, CameraBasis& b) {
    for (int i = 0; i < 3; ++i) {
        b.f[i] = m.entry[0][i];
        b.u[i] = m.entry[1][i];
        b.r[i] = m.entry[2][i];
        b.e[i] = m.entry[3][i];
    }
}

inline void WriteBasis(const CameraBasis& b, NiMatrix44& m) {
    for (int i = 0; i < 3; ++i) {
        m.entry[0][i] = b.f[i];
        m.entry[1][i] = b.u[i];
        m.entry[2][i] = b.r[i];
        m.entry[3][i] = b.e[i];
    }
    m.entry[0][3] = 0.0f;
    m.entry[1][3] = 0.0f;
    m.entry[2][3] = 0.0f;
    m.entry[3][3] = 1.0f;
}

inline void BasisFromRotation(const Mat3& rot, const float eye[3], CameraBasis& b) {
    for (int i = 0; i < 3; ++i) {
        b.f[i] = rot.m[0][i];
        b.u[i] = rot.m[1][i];
        b.r[i] = rot.m[2][i];
        b.e[i] = eye[i];
    }
}

inline Mat3 RotationOfBasis(const CameraBasis& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        r.m[0][i] = b.f[i];
        r.m[1][i] = b.u[i];
        r.m[2][i] = b.r[i];
    }
    return r;
}

inline float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The whole clip matrix follows from the basis and the frustum's two extents;
// z/w works out to 1 - near/distance, which is an infinite far plane.
//
// This one is stored TRANSPOSED relative to the node transforms above. Its rows
// are the clip-space coefficient vectors, so it composes as M * v with column
// vectors, not v * M - which is why the translation terms sit in column 3 rather
// than row 3. Same storage order, opposite meaning.
inline void BuildWorldToClip(const CameraBasis& b, const NiFrustum& frustum, NiMatrix44& out) {
    const float invRight = 1.0f / frustum.right;
    const float invTop = 1.0f / frustum.top;
    for (int i = 0; i < 3; ++i) {
        out.entry[0][i] = b.r[i] * invRight;
        out.entry[1][i] = b.u[i] * invTop;
        out.entry[2][i] = b.f[i];
        out.entry[3][i] = b.f[i];
    }
    const float fe = Dot3(b.f, b.e);
    out.entry[0][3] = -Dot3(b.r, b.e) * invRight;
    out.entry[1][3] = -Dot3(b.u, b.e) * invTop;
    out.entry[2][3] = -fe - frustum.nearPlane;
    out.entry[3][3] = -fe;
}

// Rotates `v` about a unit `axis` by `angle` radians (Rodrigues).
inline void RotateAboutAxis(float v[3], const float axis[3], float angle) {
    const float c = cosf(angle);
    const float s = sinf(angle);
    const float d = Dot3(axis, v) * (1.0f - c);
    const float cross[3] = {
        axis[1] * v[2] - axis[2] * v[1],
        axis[2] * v[0] - axis[0] * v[2],
        axis[0] * v[1] - axis[1] * v[0],
    };
    for (int i = 0; i < 3; ++i) {
        v[i] = v[i] * c + cross[i] * s + axis[i] * d;
    }
}

inline void RotateBasis(CameraBasis& b, const float axis[3], float angle) {
    RotateAboutAxis(b.f, axis, angle);
    RotateAboutAxis(b.u, axis, angle);
    RotateAboutAxis(b.r, axis, angle);
}

// Floating point drift accumulates over a session's worth of per-frame
// rotations, and a basis that has stopped being orthonormal skews the picture
// rather than turning it.
inline void Orthonormalise(CameraBasis& b) {
    float len = sqrtf(Dot3(b.f, b.f));
    if (len > 1e-6f) {
        for (int i = 0; i < 3; ++i) b.f[i] /= len;
    }
    const float fu = Dot3(b.f, b.u);
    for (int i = 0; i < 3; ++i) b.u[i] -= fu * b.f[i];
    len = sqrtf(Dot3(b.u, b.u));
    if (len > 1e-6f) {
        for (int i = 0; i < 3; ++i) b.u[i] /= len;
    }
    // Right completes the frame in the handedness the engine already uses:
    // taking it from the cross product directly would flip it if the engine's
    // frame is left-handed, so the sign is carried over from the live basis.
    const float cross[3] = {
        b.f[1] * b.u[2] - b.f[2] * b.u[1],
        b.f[2] * b.u[0] - b.f[0] * b.u[2],
        b.f[0] * b.u[1] - b.f[1] * b.u[0],
    };
    const float sign = Dot3(cross, b.r) >= 0.0f ? 1.0f : -1.0f;
    for (int i = 0; i < 3; ++i) b.r[i] = cross[i] * sign;
}

} // namespace StarfieldHT
