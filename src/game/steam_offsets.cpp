#include "pch.h"
#include "build_profile.h"

namespace StarfieldHT {

using cameraunlock::memory::PeFingerprint;

// Steam 1.16.244.0 (depot build 23518663). The same release as the GDK profile
// but a separate link, so every RVA moved. Each function here matched its GDK
// counterpart instruction for instruction with only RIP-relative and branch
// displacements differing, which is also what carries the struct offsets over
// unchanged. Keep this list in the same order as the struct.
extern constexpr BuildProfile kSteamProfile_20251129 = {
    "steam-win64-20251129",
    PeFingerprint{0x6a1e0c18, 0x089c2000, 0x061bf3b7},
    0x2BE3AC0,
    0x1EC7F20,
    0x29FBD40,
    0x2BE4750,
    0x59750D0,
    0x1350800,
    0x5F43230,
    0x228,
    0x08,
    0x518,
    0x5F27448,
    0x210F9A0,
    0x21159B0,
    0x12F59D0,
    0x2CED00,
    0x838,
    0x12F0CB0,
    0x2122C5C,
    0x5FB08E8,
    0x1547D30,
    0x1548A70,
    0x1549490,
    0x2122BF0,
    0x2155EA0,
};

static_assert(kSteamProfile_20251129.playerSingletonRva == 0x5F43230, "player singleton RVA");
static_assert(kSteamProfile_20251129.playerProcessOffset == 0x228, "PlayerCharacter -> AIProcess");
static_assert(kSteamProfile_20251129.processHighOffset == 0x08, "AIProcess -> HighProcess");
static_assert(kSteamProfile_20251129.highIronSightsOffset == 0x518, "HighProcess sights flag");
static_assert(kSteamProfile_20251129.baseFovSettingRva == 0x5F27448, "fFPWorldFOV Setting RVA");

} // namespace StarfieldHT
