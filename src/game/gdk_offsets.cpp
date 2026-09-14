#include "pch.h"
#include "build_profile.h"

namespace StarfieldHT {

using cameraunlock::memory::PeFingerprint;

// Xbox (GDK) package 1.16.244.0. The initializer is positional and every member
// is a uintptr_t, so a value in the wrong slot compiles clean and walks the
// wrong pointer at runtime: keep this list in the same order as the struct.
extern constexpr BuildProfile kGdkProfile_20251129 = {
    "gdk-win64-20251129",
    PeFingerprint{0x6a1e0bbe, 0x08992000, 0x06194a6a},
    0x2C4E920,
    0x1EC7020,
    0x2A66BB0,
    0x2C4F5B0,
    0x597CCE0,
    0x1350F50,
    0x5F4ED90,
    0x228,
    0x08,
    0x518,
    0x5F32F90,
    0x210EA90,
    0x2114AA0,
    0x12F64F0,
    0x2D0140,
    0x838,
    0x12F17D0,
    0x2121D4C,
    0x5FBB6A8,
    0x1546F90,
    0x1547CD0,
    0x15486F0,
    0x2121CE0,
    0x2154F90,
    0xDF8,
};

// The initializer above is positional, so these pin the slots that are easy to
// shift: three small struct offsets sitting between two large RVAs, where a
// value in the wrong place is still a plausible-looking number.
static_assert(kGdkProfile_20251129.playerSingletonRva == 0x5F4ED90, "player singleton RVA");
static_assert(kGdkProfile_20251129.playerProcessOffset == 0x228, "PlayerCharacter -> AIProcess");
static_assert(kGdkProfile_20251129.processHighOffset == 0x08, "AIProcess -> HighProcess");
static_assert(kGdkProfile_20251129.highIronSightsOffset == 0x518, "HighProcess sights flag");
static_assert(kGdkProfile_20251129.baseFovSettingRva == 0x5F32F90, "fFPWorldFOV Setting RVA");

} // namespace StarfieldHT
