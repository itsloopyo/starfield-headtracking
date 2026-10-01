#pragma once

#include <cstddef>
#include <cstdint>

namespace StarfieldHT {

// Decoded member offsets are checked against the live camera's type, parent
// and projection. Exact historical profiles retain their live layout resolver.
//
// Transforms are 4x4 row-major and 64 bytes: rows 0..2 are the node's local
// axes expressed in world space with a 0 in the fourth column, row 3 is the
// translation with a 1. The camera node's axes are x=forward, y=up, z=right.
struct SceneLayout {
    bool      valid = false;
    uintptr_t cameraRootOffset = 0;    // TESCamera -> NiNode* (the camera root)
    uintptr_t childrenDataOffset = 0;  // NiNode -> NiAVObject** (children array)
    uintptr_t parentOffset = 0;        // NiAVObject -> NiNode* (its parent)
    uintptr_t localTransformOffset = 0;
    uintptr_t worldTransformOffset = 0;
    uintptr_t worldToClipOffset = 0;   // NiCamera only
    uintptr_t frustumOffset = 0;       // NiCamera only
};

// Finds the NiCamera vtable the layout is matched against. The image scan
// takes a few hundred milliseconds, so it runs on the init thread rather than
// inside the first camera update, where it stalled the game's own frame.
bool InitializeSceneLayout(uintptr_t moduleBase, size_t moduleSize);

// Caches successful validation. A structural failure on the discovered route
// disables tracking for the session; only identified not-ready states wait.
bool ResolveSceneLayout(void* playerCamera);

// Valid only after ResolveSceneLayout has returned true.
const SceneLayout& GetSceneLayout();

// Walks PlayerCamera -> cameraRoot -> NiCamera using the resolved layout.
// False if either link is null or the camera is no longer an NiCamera.
bool GetSceneGraph(void* playerCamera, uintptr_t& cameraRoot, uintptr_t& niCamera);

// Dumps both nodes' transforms, the frustum and the world-to-clip matrix, plus
// the difference between the stored clip matrix and one rebuilt from the world
// transform. Diagnostic only.
void LogCameraSurvey(uintptr_t cameraRoot, uintptr_t niCamera);

} // namespace StarfieldHT
