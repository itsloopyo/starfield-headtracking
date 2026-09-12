#include "pch.h"
#include "ads_state.h"
#include "core/logger.h"
#include "game/build_profile.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT::AdsState {
namespace {

using cameraunlock::memory::SafeRead;

// A player raises the sights hundreds of times a session and every one of them
// is routine. The first few are the diagnostic - they say the flag is being
// read correctly on this build - so the rest are dropped.
constexpr int kMaxTransitionLines = 20;

std::atomic<bool> g_aiming{false};

// The chain from the player singleton to the sights flag:
// PlayerCharacter -> AIProcess -> HighProcess, then the flag inside it.
// HUDCrosshairDataModel reads that same flag for bIronSights, which is what
// pins it to the sights rather than to any other aim state. The offsets come
// from the build profile, so a patch that moves one is a new profile.
struct SightsChain {
    uintptr_t singleton = 0;
    uintptr_t processOffset = 0;
    uintptr_t highOffset = 0;
    uintptr_t ironSightsOffset = 0;
};

// The singleton address, or 0 when the profile's RVA does not land inside the
// running module. Reporting 0 leaves the sights reading "down" rather than
// walking a chain from an address that is not the singleton.
uintptr_t ResolvePlayerSingleton() {
    const BuildProfile* profile = ResolveBuildProfile();
    if (profile == nullptr) return 0;

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule) return 0;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return 0;
    if (profile->playerSingletonRva >= moduleSize) {
        Logger::Instance().Error(
            "Sights state: the build profile's player RVA is outside the module - the sights "
            "mode cycle stays on its hip-fire behaviour");
        return 0;
    }
    return moduleBase + profile->playerSingletonRva;
}

SightsChain ResolveSightsChain() {
    SightsChain chain;
    const BuildProfile* profile = ResolveBuildProfile();
    if (profile == nullptr) return chain;
    chain.singleton = ResolvePlayerSingleton();
    chain.processOffset = profile->playerProcessOffset;
    chain.highOffset = profile->processHighOffset;
    chain.ironSightsOffset = profile->highIronSightsOffset;
    return chain;
}

} // namespace

void Update() {
    // Resolved once: the game module cannot move for the life of the process.
    static const SightsChain chain = ResolveSightsChain();

    // Every link is read through SafeRead. This walks four game pointers on
    // every camera update, and a member that has moved under a game patch would
    // otherwise raise inside the camera hook, which loses the whole frame's
    // tracking and reports it as an unexplained hook fault.
    bool aiming = false;
    uintptr_t player = 0, process = 0, high = 0;
    int ironSights = 0;
    if (chain.singleton != 0
        && SafeRead(chain.singleton, player) && player != 0
        && SafeRead(player + chain.processOffset, process) && process != 0
        && SafeRead(process + chain.highOffset, high) && high != 0
        && SafeRead(high + chain.ironSightsOffset, ironSights)) {
        aiming = ironSights != 0;
    }
    if (g_aiming.exchange(aiming, std::memory_order_relaxed) != aiming) {
        static std::atomic<int> s_logged{0};
        if (s_logged.fetch_add(1, std::memory_order_relaxed) < kMaxTransitionLines) {
            Logger::Instance().Info("Sights %s", aiming ? "up" : "down");
        }
    }
}

bool IsAiming() { return g_aiming.load(std::memory_order_relaxed); }
}
