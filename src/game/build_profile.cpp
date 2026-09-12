#include "pch.h"
#include "build_profile.h"

#include "core/logger.h"

namespace StarfieldHT {

namespace {

using cameraunlock::memory::PeFingerprint;

// Xbox (GDK) package 1.16.244.0. The initializer is positional and every member
// is a uintptr_t, so a value in the wrong slot compiles clean and walks the
// wrong pointer at runtime: keep this list in the same order as the struct.
constexpr BuildProfile kGdkProfile_20251129 = {
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
};

// The initializer above is positional, so these pin the slots that are easy to
// shift: three small struct offsets sitting between two large RVAs, where a
// value in the wrong place is still a plausible-looking number.
static_assert(kGdkProfile_20251129.playerSingletonRva == 0x5F4ED90, "player singleton RVA");
static_assert(kGdkProfile_20251129.playerProcessOffset == 0x228, "PlayerCharacter -> AIProcess");
static_assert(kGdkProfile_20251129.processHighOffset == 0x08, "AIProcess -> HighProcess");
static_assert(kGdkProfile_20251129.highIronSightsOffset == 0x518, "HighProcess sights flag");
static_assert(kGdkProfile_20251129.baseFovSettingRva == 0x5F32F90, "fFPWorldFOV Setting RVA");

const BuildProfile* const kKnownProfiles[] = {
    &kGdkProfile_20251129,
};

const BuildProfile* MatchRunningBuild() {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) return nullptr;

    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(gameModule, running)) {
        Logger::Instance().Error("Could not read the game's PE header - aim decoupling is off");
        return nullptr;
    }

    for (const BuildProfile* profile : kKnownProfiles) {
        if (profile->fingerprint.Matches(running)) {
            Logger::Instance().Info("Build profile %s matched", profile->name);
            return profile;
        }
    }

    const BuildProfile* primary = kKnownProfiles[0];
    const auto mismatch = cameraunlock::memory::ClassifyMismatch(running, primary->fingerprint);
    const char* reason =
        mismatch == cameraunlock::memory::FingerprintMismatch::Newer
            ? "this game build is newer than the mod knows about - check the releases page for an update"
        : mismatch == cameraunlock::memory::FingerprintMismatch::Older
            ? "this game build is older than the mod knows about - let the store finish updating"
            : "this executable does not match any build the mod knows about";
    Logger::Instance().Warning(
        "No build profile for the running game (TimeDateStamp 0x%08X, SizeOfImage 0x%08X, "
        "CheckSum 0x%08X): %s. The mod stays dormant - it installs no hooks and modifies "
        "nothing, so the game runs exactly as it would without it.",
        running.TimeDateStamp, running.SizeOfImage, running.CheckSum, reason);
    return nullptr;
}

} // namespace

// The match runs once. A function-local static is what makes that safe: the
// hook installs resolve on the init thread while the camera and HUD hooks
// resolve on their own, and a hand-rolled done flag is a data race between
// them that costs a duplicated fingerprint report at best.
const BuildProfile* ResolveBuildProfile() {
    static const BuildProfile* const s_resolved = MatchRunningBuild();
    return s_resolved;
}

} // namespace StarfieldHT
