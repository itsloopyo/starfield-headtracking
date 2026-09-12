#pragma once
#include "game/camera_frame.h"

namespace StarfieldHT {
bool InstallWeaponHook();
bool FindSubmittedCameraFrame(const CameraBasis& drawn, CameraFrame& out);
}
