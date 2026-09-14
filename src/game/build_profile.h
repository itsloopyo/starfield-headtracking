#pragma once

#include <cstdint>
#include <cameraunlock/memory/pe_fingerprint.h>

namespace StarfieldHT {

struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;
    uintptr_t screenProjectRva;
    uintptr_t hudUpdateRva;
    uintptr_t weaponPassRva;
    uintptr_t cameraSubmitRva;
    uintptr_t cameraRegistryRva;
    uintptr_t gfxReleaseValueRva;
    uintptr_t playerSingletonRva;

    // PlayerCharacter -> AIProcess -> HighProcess, then the sights flag inside
    // it. Struct offsets move under a patch exactly like RVAs do, so they live
    // in the profile: a build that keeps the singleton where it is and moves
    // the flag is then a new profile rather than an edit that changes behaviour
    // for everyone still on the old build.
    uintptr_t playerProcessOffset;
    uintptr_t processHighOffset;
    uintptr_t highIronSightsOffset;

    // The static Setting object for fFPWorldFOV:Camera, the player's own
    // un-zoomed field of view. It is the reference the live frustum is measured
    // against when the head pose is scaled for whatever the game has done to
    // the field of view.
    uintptr_t baseFovSettingRva;
    uintptr_t shipAimRva;
    uintptr_t shipPilotRva;
    uintptr_t shipCameraConvertRva;
    uintptr_t relativeAimPointRva;
    uintptr_t playerAimPointOffset;
    uintptr_t activeCameraRva;
    uintptr_t shipLockCameraReturnRva;
    uintptr_t selectionCameraManagerRva;
    uintptr_t pickShipTargetRva;
    uintptr_t scoreShipTargetRva;
    uintptr_t scoreSpaceTargetRva;
    uintptr_t shipLockAngleRva;
    uintptr_t shipLockUpdateRva;

    // PlayerCharacter -> the skeleton's Camera bone, whose p-AttachLight child
    // carries the helmet light.
    uintptr_t playerCameraBoneOffset;
    uintptr_t playerHitEventRva;
    uintptr_t hudHitEventRva;
    uintptr_t worldOriginIndexRva;
};

// One file per store, every build for that store inside it. Append new builds;
// never edit or delete an existing profile.
extern const BuildProfile kGdkProfile_20251129;
extern const BuildProfile kSteamProfile_20251129;

const BuildProfile* ResolveBuildProfile();
}
