#include "pch.h"
#include "build_selection.h"
#include "core/logger.h"
#include <cameraunlock/memory/pattern_scanner.h>

namespace StarfieldHT {
namespace {
discovery::Image SnapshotImage(HMODULE module) {
    uintptr_t base = 0;
    size_t size = 0;
    discovery::Require(cameraunlock::memory::GetModuleRange(module, base, size)
                       && size > 0 && size <= 512u * 1024 * 1024,
                       "Could not determine bounded game image");
    std::vector<uint8_t> bytes(size);
    uintptr_t cursor = base, run = 0;
    size_t length = 0;
    while (cameraunlock::memory::NextReadableRange(cursor, base + size, run, length)) {
        discovery::Require(run >= base && run - base <= size && length <= size - (run - base),
                           "Readable mapping exceeds game image");
        SIZE_T copied = 0;
        discovery::Require(ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(run),
                           bytes.data() + (run - base), length, &copied) && copied == length,
                           "Game image changed protection during discovery");
    }
    discovery::Image image(std::move(bytes));
    image.base = base;
    return image;
}

BuildSelection MatchRunningBuild() {
    const auto module = GetModuleHandleA(GAME_EXE);
    cameraunlock::memory::PeFingerprint fingerprint{};
    if (!module || !cameraunlock::memory::ReadPeFingerprint(module, fingerprint)) {
        Logger::Instance().Error("Could not read the game image fingerprint; head tracking remains dormant");
        return {};
    }
    Logger::Instance().Info("Validating native camera dependencies for image %08X/%08X/%08X",
                            fingerprint.TimeDateStamp, fingerprint.SizeOfImage, fingerprint.CheckSum);
    for (const auto* profile : {&kSteamProfile_20251129, &kGdkProfile_20251129}) {
        Logger::Instance().Info("Compatibility profile %s: %08X/%08X/%08X (%s)", profile->name,
                                profile->fingerprint.TimeDateStamp, profile->fingerprint.SizeOfImage,
                                profile->fingerprint.CheckSum, profile->fingerprint.Matches(fingerprint) ? "exact" : "different");
    }
    auto selection = SelectBuild(fingerprint, [&] { return DiscoverBuild(SnapshotImage(module)); });
    if (selection.discovered) Logger::Instance().Info("%s", selection.diagnostic.c_str());
    else Logger::Instance().Warning("%s", selection.diagnostic.c_str());
    if (selection.exact) {
        Logger::Instance().Info("Using complete exact compatibility profile %s", selection.exact->name);
    }
    if (selection.discovered) {
        const auto& contracts = selection.discovered->contracts;
        for (const auto& [name, value] : contracts.methods) Logger::Instance().Info("Discovery method %s RVA 0x%X", name.c_str(), value);
        for (const auto& [name, value] : contracts.data) Logger::Instance().Info("Discovery global %s RVA 0x%X", name.c_str(), value);
        for (const auto& [name, value] : contracts.members) Logger::Instance().Info("Discovery field %s +0x%X", name.c_str(), value);
        for (const auto& [name, value] : contracts.slots) Logger::Instance().Info("Discovery virtual slot %s %u", name.c_str(), value);
    }
    return selection;
}

const BuildSelection& Selection() {
    static const BuildSelection selected = MatchRunningBuild();
    return selected;
}
}

const BuildProfile* ResolveBuildProfile() { return Selection().Profile(); }
const discovery::ResolvedContracts* RuntimeContracts() {
    const auto& selection = Selection();
    return selection.discovered ? &selection.discovered->contracts : nullptr;
}
uintptr_t RuntimeSlot(const char* method, uintptr_t exactProfileSlot) {
    const auto* contracts = RuntimeContracts();
    return contracts ? contracts->slots.at(method) : exactProfileSlot;
}
}
