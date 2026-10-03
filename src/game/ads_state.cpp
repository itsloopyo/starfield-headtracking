#include "pch.h"
#include "ads_state.h"
#include "core/logger.h"
#include "game/build_profile.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT::AdsState {
namespace {

using cameraunlock::memory::SafeRead;

constexpr uint64_t kTransitionLogIntervalMs = 1000;

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

SightsChain ResolveSightsChain() {
    SightsChain chain;
    const BuildProfile* profile = ResolveBuildProfile();
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (profile == nullptr || !gameModule
        || !cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) {
        return chain;
    }
    if (profile->playerSingletonRva >= moduleSize) {
        Logger::Instance().Error(
            "Sights state: the build profile's player RVA is outside the module, so the sights "
            "always read as down");
        return chain;
    }
    chain.singleton = moduleBase + profile->playerSingletonRva;
    chain.processOffset = profile->playerProcessOffset;
    chain.highOffset = profile->processHighOffset;
    chain.ironSightsOffset = profile->highIronSightsOffset;
    return chain;
}

} // namespace

void Update() {
    // Resolved once: the game module cannot move for the life of the process.
    static const SightsChain chain = ResolveSightsChain();

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
        // Rate limited rather than counted, so a later question about the flag
        // still has lines to answer it.
        static std::atomic<uint64_t> s_lastMs{0};
        const uint64_t now = GetTickCount64();
        if (now - s_lastMs.load(std::memory_order_relaxed) >= kTransitionLogIntervalMs) {
            s_lastMs.store(now, std::memory_order_relaxed);
            Logger::Instance().Info("Sights %s", aiming ? "up" : "down");
        }
    }
}

bool IsAiming() { return g_aiming.load(std::memory_order_relaxed); }

} // namespace StarfieldHT::AdsState
