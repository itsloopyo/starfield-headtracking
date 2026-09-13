#include "pch.h"
#include "build_profile.h"

#include "core/logger.h"

namespace StarfieldHT {

namespace {

// Newest first: the top entry is the one an unmatched build is compared against
// to say whether the running game is newer or older than anything known.
const BuildProfile* const kKnownProfiles[] = {
    &kSteamProfile_20251129,
    &kGdkProfile_20251129,
};

const BuildProfile* MatchRunningBuild() {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) return nullptr;

    cameraunlock::memory::PeFingerprint running{};
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
