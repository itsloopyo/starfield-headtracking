#include "pch.h"
#include "scene_nodes.h"

#include "game/build_profile.h"
#include "game/build_selection.h"
#include "game/scene_layout.h"

#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT::SceneNodes {
namespace {

using cameraunlock::memory::SafeRead;

// NiObjectNET::name is a pointer to a pooled string entry with the characters
// from +0x18, as helmet_light.cpp reads it.
constexpr uintptr_t kNameOffset = 0x10;
constexpr uintptr_t kNameTextOffset = 0x18;
constexpr uintptr_t kChildCountsOffset = 8;
constexpr size_t kMaxNameBytes = 32;

} // namespace

bool IsObject(uintptr_t p) {
    return p > 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7) == 0;
}

bool NameStartsWith(uintptr_t node, const char* text, size_t size) {
    const auto* contracts = RuntimeContracts();
    const auto nameOffset = contracts ? contracts->members.at("NodeName") : kNameOffset;
    const auto textOffset = contracts ? contracts->members.at("NameCharacters") : kNameTextOffset;
    uintptr_t entry = 0;
    char name[kMaxNameBytes] = {};
    if (size > sizeof(name) || !SafeRead(node + nameOffset, entry) || !IsObject(entry)) return false;
    for (size_t i = 0; i < size; ++i) {
        if (!SafeRead(entry + textOffset + i, name[i]) || name[i] != text[i]) return false;
    }
    return true;
}

bool ParentIs(uintptr_t node, uintptr_t parent) {
    uintptr_t p = 0;
    return SafeRead(node + GetSceneLayout().parentOffset, p) && p == parent;
}

uint16_t ReadChildren(uintptr_t node, uintptr_t out[kMaxChildren]) {
    const SceneLayout& layout = GetSceneLayout();
    uintptr_t data = 0;
    uint16_t count = 0;
    if (!SafeRead(node + layout.childrenDataOffset, data) || !IsObject(data)) return 0;
    if (const auto* contracts = RuntimeContracts()) {
        if (!SafeRead(node + contracts->members.at("NodeChildCount"), count)) return 0;
    } else {
        uint16_t counts[3] = {};
        if (!SafeRead(node + layout.childrenDataOffset + kChildCountsOffset, counts)) return 0;
        count = counts[0];
        if (counts[1] < count) count = counts[1];
        if (counts[2] < count) count = counts[2];
    }
    if (count > kMaxChildren) return 0;
    uint16_t kept = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uintptr_t child = 0;
        if (SafeRead(data + i * sizeof(uintptr_t), child) && IsObject(child) && ParentIs(child, node)) {
            out[kept++] = child;
        }
    }
    return kept;
}

uintptr_t ReadCameraBone() {
    static const BuildProfile* const profile = ResolveBuildProfile();
    static const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(GAME_EXE));
    uintptr_t player = 0, bone = 0;
    if (!profile || !SafeRead(moduleBase + profile->playerSingletonRva, player) || !IsObject(player)
        || !SafeRead(player + profile->playerCameraBoneOffset, bone) || !IsObject(bone)) return 0;
    return bone;
}

} // namespace StarfieldHT::SceneNodes
