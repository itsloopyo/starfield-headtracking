#pragma once

#include "core/constants.h"
#include "game/camera_math.h"

namespace StarfieldHT {

// Starfield's world is metric: one world unit is one metre.
//
// This is worth stating because the older Creation Engine games are not, and
// carrying their 70-units-per-metre over made every lean seventy times too big -
// a 0.3 m lean threw the camera twenty metres sideways. The camera's own frustum
// settles it: the near plane is 0.05 and the far plane 6000, which is 5 cm and
// 6 km. At 1.42 cm per unit those would be 0.7 mm and 85 m, and no game draws
// distant planets with an 85 m far plane.
inline constexpr float UNITS_PER_METER = 1.0f;

// Maps a processed position offset (metres, X=right, Y=up, Z=depth) onto the
// (forward, up, right) component order the caller resolves against whichever
// basis it leans along - the horizon-locked one below while horizon lock is on,
// the camera node's own axes otherwise. The order matches the node's axis order
// (x=forward, y=up, z=right, see scene_layout.h) because that is where it came
// from.
//
// Two sign conversions happen here, at the boundary between the tracker's
// convention and the engine's, and nowhere else:
//
//   - Depth. Negative z is the forward lean throughout the library, and the
//     node's forward axis points forward, so the sign flips. Doing this in the
//     processor's InvertZ instead would move the generous 0.40m allowance onto
//     the backward lean, because the inversion there happens before the
//     asymmetric [-LimitZ, +LimitZBack] clamp.
//
//   - Lateral. The tracker's positive x arrives mirrored relative to the
//     engine's right axis, so it flips too. This is the fleet norm rather than
//     something derived from the engine's handedness, which says nothing about
//     which way the tracker calls positive.
inline NiPoint3 CameraLocalLeanOffset(float posX, float posY, float posZ) {
    return NiPoint3(-posZ * UNITS_PER_METER,
                     posY * UNITS_PER_METER,
                    -posX * UNITS_PER_METER);
}

// The camera's basis with its pitch and roll taken out: forward flattened onto
// the world's horizontal plane, up along world up, right the horizontal
// remainder. A lean travels along this while horizon lock is on, so leaning in
// moves the eye across the floor rather than into it.
//
// The camera's own basis is the wrong one for that. Pitch it 60 degrees down to
// look at something and the tracker's forward axis points 60 degrees down with
// it, so leaning in drives the eye at the floor and only a fraction of the lean
// reaches the direction the player meant. Roll does the same to a lateral lean.
// The player's body does neither: it yaws with the camera and stays upright.
inline CameraBasis HorizonLockedBasis(const CameraBasis& basis) {
    static const float kWorldUp[3] = {0.0f, 0.0f, 1.0f};

    float flatF[3], flatR[3];
    const float fUp = Dot3(basis.f, kWorldUp);
    const float rUp = Dot3(basis.r, kWorldUp);
    for (int i = 0; i < 3; ++i) {
        flatF[i] = basis.f[i] - kWorldUp[i] * fUp;
        flatR[i] = basis.r[i] - kWorldUp[i] * rUp;
    }

    // For an orthonormal basis (f.up)^2 + (u.up)^2 + (r.up)^2 = 1, so the longer
    // of the two flattened vectors is always at least 1/sqrt(2). Normalising
    // that one and taking the other from a cross product is what stays defined
    // at every camera orientation: flattening both and orthogonalising instead
    // collapses when the camera is rolled a quarter turn, which leaves f and r
    // in the same vertical plane and their projections on the same line.
    //
    // The bound depends on the input being orthonormal, and the input is two
    // matrices read out of game memory: a recycled node gives a degenerate
    // basis, and dividing by its length would put NaN into the camera's local
    // transform, which the scene graph then rebuilds everything else from.
    const bool forwardLeads = Dot3(flatF, flatF) >= Dot3(flatR, flatR);
    float* lead = forwardLeads ? flatF : flatR;
    float* follow = forwardLeads ? flatR : flatF;

    const float leadLength = sqrtf(Dot3(lead, lead));
    if (!(leadLength > 1e-3f)) return basis;
    for (int i = 0; i < 3; ++i) lead[i] /= leadLength;

    // Handedness comes from the live basis's own triple product, not from how
    // its up axis compares with the world's. Reading it off the up axis inverted
    // the lateral lean the moment the camera passed its side - a ship rolling
    // through 90 degrees - because the comparison it rests on changes sign
    // there while the handedness does not.
    const float fCrossU[3] = {
        basis.f[1] * basis.u[2] - basis.f[2] * basis.u[1],
        basis.f[2] * basis.u[0] - basis.f[0] * basis.u[2],
        basis.f[0] * basis.u[1] - basis.f[1] * basis.u[0],
    };
    const float handedness = Dot3(fCrossU, basis.r) >= 0.0f ? 1.0f : -1.0f;

    const float cross[3] = {
        kWorldUp[1] * lead[2] - kWorldUp[2] * lead[1],
        kWorldUp[2] * lead[0] - kWorldUp[0] * lead[2],
        kWorldUp[0] * lead[1] - kWorldUp[1] * lead[0],
    };
    // up x forward runs against right, and up x right runs with forward.
    const float sign = forwardLeads ? -handedness : handedness;
    for (int i = 0; i < 3; ++i) follow[i] = cross[i] * sign;

    CameraBasis out{};
    for (int i = 0; i < 3; ++i) {
        out.f[i] = flatF[i];
        out.u[i] = kWorldUp[i];
        out.r[i] = flatR[i];
        out.e[i] = basis.e[i];
    }
    return out;
}

// Applies the head pose to a copy of the clean basis. Yaw turns about the
// world's up axis in horizon-locked mode and about the camera's own up axis
// otherwise; pitch and roll always turn about the axes yaw just moved, so the
// composition is yaw, then pitch, then roll with no gimbal degeneracy.
//
// Yaw and roll are negated here and pitch is not. The protocol says nothing
// about which way the tracker calls positive, so this is the fleet's convention
// rather than anything derived from the engine, confirmed in game against the
// camera basis the renderer draws with.
inline void ApplyHeadRotationToBasis(CameraBasis& basis, float yawDeg, float pitchDeg,
                                     float rollDeg, bool worldSpaceYaw) {
    // Starfield's world is Z-up.
    static const float kWorldUp[3] = {0.0f, 0.0f, 1.0f};

    const float yawRad   = -yawDeg   * DEG_TO_RAD;
    const float pitchRad =  pitchDeg * DEG_TO_RAD;
    const float rollRad  = -rollDeg  * DEG_TO_RAD;

    if (yawRad != 0.0f) {
        const float localUp[3] = {basis.u[0], basis.u[1], basis.u[2]};
        RotateBasis(basis, worldSpaceYaw ? kWorldUp : localUp, yawRad);
    }
    if (pitchRad != 0.0f) {
        const float axis[3] = {basis.r[0], basis.r[1], basis.r[2]};
        RotateBasis(basis, axis, pitchRad);
    }
    if (rollRad != 0.0f) {
        const float axis[3] = {basis.f[0], basis.f[1], basis.f[2]};
        RotateBasis(basis, axis, rollRad);
    }
    Orthonormalise(basis);
}

} // namespace StarfieldHT
