#pragma once
#include "game/camera_frame.h"
#include "game/weapon_projection.h"

namespace StarfieldHT {
bool InstallWeaponHook();
// How the weapon pass last drew the held weapon. False until it has drawn one.
bool LatestWeaponPassView(WeaponPassView& out);
bool FindSubmittedCameraFrame(const CameraBasis& drawn, CameraFrame& out);
}
