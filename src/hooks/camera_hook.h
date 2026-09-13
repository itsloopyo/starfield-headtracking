#pragma once

#include <cstdint>

#include "game/camera_frame.h"

namespace StarfieldHT {

// Discover the PlayerCamera vtable via RTTI and hook TESCamera::Update, which
// PlayerCamera inherits. Returns false if RTTI discovery fails.
bool InstallCameraHook();

// What the camera looked like on the last frame tracking was applied. `clean`
// is the camera the game positioned and still believes it is using - the aim
// line, and what every raycast and projectile follows. `drawn` is the camera
// the picture was rendered from. They differ by exactly the head pose.
// False if no tracked frame has been published yet.
bool GetCameraFrame(CameraFrame& out);

// The published frame whose local transform is `local`, searched back through
// the history so a render pass consuming an older simulation snapshot still
// finds the clean basis that went with it.
bool FindLocalCameraFrame(uintptr_t camera, const NiMatrix44& local, CameraFrame& out);

// The camera's world transform as the game built it, before the head pose was
// applied, together with where it lives.
//
// Only the world one. The local transform has to keep the head rotation right
// through the frame: the scene graph rebuilds the world transform and the clip
// matrix from it after this is handed back, so a clean local means a clean
// picture and no head tracking at all - measured.
//
// `token` identifies the frame it was published for: pass back what the last
// call returned and it reports false until a newer one exists, so a caller that
// runs thousands of times a frame restores once rather than thousands of times.
// It advances on every complete publication, including the empty ones published
// while nothing is tracked, so those frames cost one atomic load per call too.
bool GetCleanWorldTransform(uint32_t& token, uintptr_t& outCamera, uintptr_t& outOffset,
                            NiMatrix44& outWorld);

// Address of the live NiCamera, 0 before the layout resolves.
uintptr_t GetLiveCameraAddress();

// Where the aim shows up in the drawn frame, in normalised device coordinates:
// x right, y up, both -1..1 across the frame.
//
// The ship heading marker projects a direction. The on-foot reticle uses the
// shooting target's depth to account for the separation of clean and drawn eyes.
//
// False when the aim has rolled behind the drawn camera.
bool ProjectAimDirection(const CameraFrame& frame, float& outNdcX, float& outNdcY);
bool ProjectPlayerAim(const CameraFrame& frame, float& outNdcX, float& outNdcY,
                      float* outDistance = nullptr);

} // namespace StarfieldHT
