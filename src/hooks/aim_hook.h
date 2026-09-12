#pragma once

namespace StarfieldHT {

struct CameraBasis;
struct CameraFrame;
bool FindConvertedCameraFrame(const CameraBasis& drawn, CameraFrame& out);

// Keeps the game's own aim on the mouse while the head moves the view.
//
// This engine builds one camera and uses it for both jobs, so rotating it for
// the picture rotates what the game thinks the player is pointing at: an
// interaction prompt jumps to whatever the head is looking at, and leaves
// whatever the mouse is aimed at. Measured directly - aiming off an NPC and
// then turning only the head onto her produced her "TALK" prompt.
//
// The fix is a matter of timing. The renderer builds the matrix it draws and
// culls with out of the rotated camera early in the frame; everything that
// aims reads the camera node itself, later. Handing the node its clean
// transform back once the renderer has taken what it needs leaves the picture
// rotated and the aim where the mouse put it.
//
// Returns false when the running build has no profile, which is a state the
// mod does not reach: an unrecognised build leaves it dormant before any hook
// is installed, because head tracking without this one aims with the head.
bool InstallAimHook();

} // namespace StarfieldHT
