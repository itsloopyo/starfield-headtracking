#pragma once

namespace StarfieldHT {

// How much the head pose has to shrink so that it moves the picture by the same
// amount whatever field of view the game is rendering.
//
// Starfield narrows the world FOV whenever a weapon comes up - measured on a
// shotgun, tan(VFOV/2) drops from 0.5154 to 0.3584 - and a narrow field of view
// magnifies everything in the frame, head tracking with it. The head still turns
// ten degrees and the camera still turns ten degrees; the picture just moves
// 1.44 times as far, which the player reads as the mod's sensitivity jumping the
// moment they aim.
//
// The reference is the player's own fFPWorldFOV, read from the executable rather
// than assumed, so someone who has moved the FOV slider in Display settings is
// measured against where they moved it to. That makes the factor exactly 1.0 in
// ordinary play and leaves ordinary play untouched.
//
// Fed the live frustum, which is tan(HFOV/2) and tan(VFOV/2) at a near plane of
// 1. Returns 1.0 - no compensation rather than a guessed one - when the
// reference cannot be read, and says so in the log once.
float PoseZoomFactor(float frustumRight, float frustumTop);

} // namespace StarfieldHT
