#pragma once

#include <cstdint>

namespace StarfieldHT {

// Byte offsets into the engine's scene-graph objects, resolved at runtime from
// the shapes of the structures themselves rather than pinned per game build.
//
// The alternative - a fingerprinted table of offsets per shipped build - would
// strand every player whose build is not in the table, and the Xbox package's
// executable is not readable on disk, so there is no way to prepare that table
// ahead of a patch. Matching on shape instead is re-checked against the live
// data every launch: a basis must be orthonormal, a frustum must have its near
// plane in front of its far plane, and the world-to-clip matrix must be
// reproducible from the world transform and the frustum. When nothing matches,
// the mod says so and stays dormant.
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

// Resolves the layout from a live PlayerCamera. Logs every field it finds and
// the reason it gave up. Safe to call every frame; the result is cached after
// the first success.
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
