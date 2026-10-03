#pragma once

#include "game/camera_frame.h"

namespace StarfieldHT {

// The aim marker of free look with a marker: drawn only in that mode and only
// while the sights are up, where the round will land. `frame` is the tracked
// frame just published, or null on a frame that is not tracked, which hides it.
void UpdateAimMarker(const CameraFrame* frame);

} // namespace StarfieldHT
