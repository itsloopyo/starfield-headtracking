#pragma once

#include "game/camera_math.h"

namespace StarfieldHT::SightDepth {

// Re-reads how far in front of the eye the held weapon's rear sight sits, along
// the clean aim. Called once per camera update while the sights are up or
// coming up; `aiming` false forgets the last answer.
void Update(const CameraBasis& clean, bool aiming);

// Metres from the eye to the rear sight, or infinity while it is not known:
// sights down, no weapon in hand, or a weapon with no sight attach node.
float Get();

} // namespace StarfieldHT::SightDepth
