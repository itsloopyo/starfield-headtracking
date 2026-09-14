#pragma once

#include "game/camera_math.h"

namespace StarfieldHT {

// The helmet light hangs off p-AttachLight, a child of the player skeleton's
// Camera bone. The game points that bone where the mouse aims, and the head pose
// only ever reaches the render camera, so without this the beam stays on the
// aim while the view turns away from it.
//
// Turns the attach node through 1.5 times the head's turn and leans it with the
// eye. Called on the simulation thread, after the camera's own local transform
// has been written.
void TrackHelmetLight(const CameraBasis& clean, const CameraBasis& drawn);

// Hands the attach node back its own transform, on frames tracking contributes
// nothing to.
void ReleaseHelmetLight();

} // namespace StarfieldHT
