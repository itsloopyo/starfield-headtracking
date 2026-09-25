#include "pch.h"
#include "helmet_light.h"

#include "core/logger.h"
#include "game/build_profile.h"
#include "game/head_attached.h"
#include "game/scene_layout.h"

#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeRead;
using cameraunlock::memory::SafeWrite;

// NiObjectNET::name is a pointer to a pooled string entry, which stores the
// length at +0x08 and the characters from +0x18.
constexpr uintptr_t kNameOffset = 0x10;
constexpr uintptr_t kNameTextOffset = 0x18;
constexpr char kBoneName[] = "Camera";
constexpr char kAttachName[] = "p-AttachLight";

// The children pointer is followed by three 16-bit counts, equal on every node
// read so far. The smallest is taken, and every child is still checked against
// its own parent pointer before it is used.
constexpr uintptr_t kChildCountsOffset = 8;
constexpr uint16_t kMaxBoneChildren = 16;

bool IsObject(uintptr_t p) {
    return p > 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7) == 0;
}

bool NameIs(uintptr_t node, const char* name, size_t size) {
    uintptr_t entry = 0;
    char text[16] = {};
    return size <= sizeof(text) && SafeRead(node + kNameOffset, entry) && IsObject(entry)
        && SafeRead(entry + kNameTextOffset, text) && std::memcmp(text, name, size) == 0;
}

bool ParentIs(uintptr_t node, uintptr_t parent) {
    uintptr_t p = 0;
    return SafeRead(node + GetSceneLayout().parentOffset, p) && p == parent;
}

// The skeleton's Camera bone, or 0 while the player has no 3D loaded.
uintptr_t ReadCameraBone() {
    static const BuildProfile* const profile = ResolveBuildProfile();
    static const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(GAME_EXE));
    uintptr_t player = 0, bone = 0;
    if (!SafeRead(moduleBase + profile->playerSingletonRva, player) || !IsObject(player)) return 0;
    if (!SafeRead(player + profile->playerCameraBoneOffset, bone) || !IsObject(bone)) return 0;
    if (!NameIs(bone, kBoneName, sizeof(kBoneName))) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Logger::Instance().Warning(
                "Helmet light: PlayerCharacter+0x%llX is not the skeleton's Camera bone on this build, "
                "so the helmet light keeps following the aim",
                static_cast<unsigned long long>(profile->playerCameraBoneOffset));
        }
        return 0;
    }
    return bone;
}

uintptr_t FindAttachNode(uintptr_t bone) {
    const SceneLayout& layout = GetSceneLayout();
    uintptr_t children = 0;
    uint16_t counts[3] = {};
    if (!SafeRead(bone + layout.childrenDataOffset, children) || !IsObject(children)
        || !SafeRead(bone + layout.childrenDataOffset + kChildCountsOffset, counts)) return 0;
    uint16_t count = counts[0];
    if (counts[1] < count) count = counts[1];
    if (counts[2] < count) count = counts[2];
    if (count > kMaxBoneChildren) count = kMaxBoneChildren;
    for (uint16_t i = 0; i < count; ++i) {
        uintptr_t child = 0;
        if (!SafeRead(children + i * sizeof(uintptr_t), child) || !IsObject(child)) continue;
        if (ParentIs(child, bone) && NameIs(child, kAttachName, sizeof(kAttachName))) return child;
    }
    return 0;
}

// The same bookkeeping the camera node needs: the node keeps whatever was last
// written into it, so the game's own transform is remembered beside ours rather
// than read back.
struct AttachState {
    uintptr_t  bone = 0;
    uintptr_t  node = 0;
    NiMatrix44 written{};
    NiMatrix44 pristine{};
    bool       have = false;
};

AttachState g_attach;

// The attach node as it stands this frame, 0 when there is none. A node from an
// earlier frame is only trusted while it still hangs off the current bone under
// its own name, since a reloaded skeleton frees it.
uintptr_t CurrentAttachNode() {
    const uintptr_t bone = ReadCameraBone();
    if (bone == 0) return 0;
    if (g_attach.node != 0 && g_attach.bone == bone && ParentIs(g_attach.node, bone)
        && NameIs(g_attach.node, kAttachName, sizeof(kAttachName))) {
        return g_attach.node;
    }
    const uintptr_t node = FindAttachNode(bone);
    static bool s_foundLogged = false;
    if (node != 0 && !s_foundLogged) {
        s_foundLogged = true;
        Logger::Instance().Info("Helmet light: attach node found under the skeleton's Camera bone");
    }
    g_attach = AttachState{};
    g_attach.bone = bone;
    g_attach.node = node;
    return node;
}

} // namespace

void TrackHelmetLight(const CameraBasis& clean, const CameraBasis& drawn, float turnScale) {
    const SceneLayout& layout = GetSceneLayout();
    const uintptr_t node = CurrentAttachNode();
    if (node == 0) return;

    NiMatrix44 stored{}, boneWorld{};
    if (!SafeRead(node + layout.localTransformOffset, stored)
        || !SafeRead(g_attach.bone + layout.worldTransformOffset, boneWorld)) return;

    if (!(g_attach.have && std::memcmp(&stored, &g_attach.written, sizeof(stored)) == 0)) {
        g_attach.pristine = stored;
        g_attach.have = true;
    }

    const NiMatrix44 local = HeadAttachedLocal(g_attach.pristine, boneWorld, clean, drawn, turnScale);
    if (SafeWrite(node + layout.localTransformOffset, local)) {
        g_attach.written = local;
    }
}

void ReleaseHelmetLight() {
    if (!g_attach.have) return;
    const SceneLayout& layout = GetSceneLayout();
    const uintptr_t node = CurrentAttachNode();
    NiMatrix44 stored{};
    if (node != 0 && SafeRead(node + layout.localTransformOffset, stored)
        && std::memcmp(&stored, &g_attach.written, sizeof(stored)) == 0) {
        SafeWrite(node + layout.localTransformOffset, g_attach.pristine);
    }
    g_attach.have = false;
}

} // namespace StarfieldHT
