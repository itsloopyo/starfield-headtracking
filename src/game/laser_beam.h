#pragma once

#include "game/camera_math.h"
#include "game/weapon_projection.h"

namespace StarfieldHT {

// A laser sight's beam mesh runs this far along its node's local y, to the
// node's BeamEnd child. The game sets the beam's length by scaling that axis.
inline constexpr float kLaserBeamLength = 10.0f;

// The beam's transform with its start moved to `start` and its far end left
// where it is. The side axes turn with the run by the shortest arc.
inline NiMatrix44 BeamFromStart(const NiMatrix44& beam, const float start[3]) {
    float oldRun[3], newRun[3];
    for (int i = 0; i < 3; ++i) {
        oldRun[i] = beam.entry[1][i];
        newRun[i] = oldRun[i] + (beam.entry[3][i] - start[i]) / kLaserBeamLength;
    }
    NiMatrix44 out = beam;
    for (int i = 0; i < 3; ++i) {
        out.entry[1][i] = newRun[i];
        out.entry[3][i] = start[i];
    }
    float axis[3] = {
        oldRun[1] * newRun[2] - oldRun[2] * newRun[1],
        oldRun[2] * newRun[0] - oldRun[0] * newRun[2],
        oldRun[0] * newRun[1] - oldRun[1] * newRun[0],
    };
    const float sine = sqrtf(Dot3(axis, axis));
    if (sine < 1e-9f * sqrtf(Dot3(oldRun, oldRun) * Dot3(newRun, newRun))) return out;
    for (int i = 0; i < 3; ++i) axis[i] /= sine;
    const float angle = atan2f(sine, Dot3(oldRun, newRun));
    RotateAboutAxis(out.entry[0], axis, angle);
    RotateAboutAxis(out.entry[2], axis, angle);
    return out;
}

// Keeps the start of the held weapon's laser sight beam on the weapon. The
// beam is drawn with the world, from the tracked eye, while the weapon is drawn
// by its own pass, so under a lean the two part unless the beam is moved to
// where that pass puts the weapon. The game places the beam after the camera
// update, so the move is made from a hook on that placement. A build the
// placement cannot be found in is logged and keeps the game's beam.
void InstallLaserBeamHook();

} // namespace StarfieldHT
