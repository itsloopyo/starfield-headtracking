#pragma once
#include "build_profile.h"
#include "discovery/contracts.h"
#include <functional>
#include <optional>

namespace StarfieldHT {
struct DiscoveredBuild {
    BuildProfile profile{};
    discovery::ResolvedContracts contracts;
};
struct BuildSelection {
    std::optional<DiscoveredBuild> discovered;
    const BuildProfile* exact = nullptr;
    std::string diagnostic;
    const BuildProfile* Profile() const { return discovered ? &discovered->profile : exact; }
};
DiscoveredBuild DiscoverBuild(const discovery::Image& image);
BuildSelection SelectBuild(cameraunlock::memory::PeFingerprint fingerprint,
                           const std::function<DiscoveredBuild()>& discover);
const discovery::ResolvedContracts* RuntimeContracts();
uintptr_t RuntimeSlot(const char* method, uintptr_t exactProfileSlot);
}
