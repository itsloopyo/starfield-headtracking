#include "pch.h"
#include "laser_beam.h"

#include "core/logger.h"
#include "game/scene_layout.h"
#include "game/scene_nodes.h"
#include "hooks/camera_hook.h"
#include "hooks/weapon_hook.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {
namespace {

using cameraunlock::memory::SafeRead;
using cameraunlock::memory::SafeWrite;
using namespace SceneNodes;

// A laser sight's beam is a node of its own in world space, placed at the
// weapon's laser module and pointed along the aim. The game updates each one
// from an entry holding the beam node and the module node it follows, which on
// the player's weapon hangs off the skeleton fourteen levels below Root.
//
// For a first-person weapon (the entry's flag at +0x39) the game does not put
// the beam at the module itself. The weapon pass draws through a narrower
// projection than the world, so it moves the start to where the world pass
// shows the module as the weapon pass draws it, about the camera it knows:
// measured, the start sits on the ray through the module's position stretched
// sideways by the ratio of the two projections, at the module's own distance.
// That camera is the clean one, so the move here starts again from the module.
constexpr size_t kEntryBeam = 0;
constexpr size_t kEntryModule = 2;
constexpr uintptr_t kEntryFirstPersonOffset = 0x39;
constexpr int kMaxAncestors = 32;

// The game's per-beam update copies the laser module's world transform into the
// beam node's local one, stretches it to what it hits and updates the node.
// Both signatures sit inside that one function. The first is where it tests the
// entry's first-person flag before the copy; the second is the end, where it
// loads the scene update flags and calls the node's update through its vtable.
constexpr char kUpdateSignature[] = "44 38 67 39 74 19 4C 8D 45 60 48 8D 55 60 48 8B 0D ?? ?? ?? ?? E8";
constexpr char kNodeUpdateSignature[] = "8B 05 ?? ?? ?? ?? 89 45 F4 48 8B 01 48 8D 55 B0 FF 90 48 02 00 00";
constexpr size_t kNodeUpdateSlot = 0x248 / sizeof(uintptr_t);

// What the game hands the node's update: zeroes, then the flags.
struct NodeUpdateData {
    uint8_t  zero[0x44];
    uint32_t flags;
};
static_assert(sizeof(NodeUpdateData) == 0x48, "the update data the game builds is 0x48 bytes");

using UpdateBeam = uintptr_t (*)(uintptr_t*);
using UpdateNode = void (*)(uintptr_t, NodeUpdateData*);

UpdateBeam g_original = nullptr;
uintptr_t g_updateFlagsAddress = 0;

bool IsOnPlayerSkeleton(uintptr_t node) {
    const uintptr_t bone = ReadCameraBone();
    uintptr_t root = 0;
    const uintptr_t parentOffset = GetSceneLayout().parentOffset;
    if (bone == 0 || !SafeRead(bone + parentOffset, root) || !IsObject(root)) return false;
    for (int level = 0; level < kMaxAncestors && IsObject(node); ++level) {
        if (node == root) return true;
        if (!SafeRead(node + parentOffset, node)) return false;
    }
    return false;
}

// False when there is nothing to move: another actor's beam, a third-person
// view, no tracked frame, or no weapon pass to meet.
bool MoveBeamOntoWeapon(const uintptr_t* entry) {
    const uintptr_t node = entry[kEntryBeam];
    const uintptr_t module = entry[kEntryModule];
    CameraFrame frame{};
    WeaponPassView pass{};
    uint8_t firstPerson = 0;
    if (!IsObject(node) || !SafeRead(reinterpret_cast<uintptr_t>(entry) + kEntryFirstPersonOffset, firstPerson)
        || firstPerson == 0 || !IsOnPlayerSkeleton(module) || !GetCameraFrame(frame)
        || !LatestWeaponPassView(pass)) return false;
    const SceneLayout& layout = GetSceneLayout();
    const uintptr_t localOffset = layout.localTransformOffset;
    NiMatrix44 local{}, moduleWorld{};
    if (!SafeRead(node + localOffset, local) || !SafeRead(module + layout.worldTransformOffset, moduleWorld)) return false;
    float start[3];
    WeaponPointInWorldPass(frame.clean, frame.drawn, frame.frustumRight, frame.frustumTop, frame.sightsUp, pass,
                           moduleWorld.entry[3], start);
    return SafeWrite(node + localOffset, BeamFromStart(local, start));
}

uintptr_t UpdateTrackedBeam(uintptr_t* entry) {
    const uintptr_t result = g_original(entry);
    const uintptr_t node = entry[kEntryBeam];
    uintptr_t vtable = 0;
    NodeUpdateData data{};
    if (MoveBeamOntoWeapon(entry) && SafeRead(node, vtable) && SafeRead(g_updateFlagsAddress, data.flags)) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            Logger::Instance().Info("Laser sight: the beam's start follows the weapon as drawn");
        }
        reinterpret_cast<UpdateNode*>(vtable)[kNodeUpdateSlot](node, &data);
    }
    return result;
}

// The one place in the image the signature matches, or 0 when it matches
// nowhere or more than once.
uintptr_t ScanUnique(uintptr_t base, size_t size, std::string_view signature) {
    using cameraunlock::memory::ScanPatternInRange;
    const auto first = reinterpret_cast<uintptr_t>(ScanPatternInRange(base, size, signature));
    if (first == 0) return 0;
    const uintptr_t next = first + 1;
    return ScanPatternInRange(next, base + size - next, signature) == nullptr ? first : 0;
}

} // namespace

void InstallLaserBeamHook() {
    uintptr_t base = 0;
    size_t size = 0;
    if (!cameraunlock::memory::GetModuleRange(GetModuleHandleA(GAME_EXE), base, size)) return;
    const uintptr_t update = ScanUnique(base, size, kUpdateSignature);
    const uintptr_t nodeUpdate = ScanUnique(base, size, kNodeUpdateSignature);
    DWORD64 imageBase = 0;
    const PRUNTIME_FUNCTION function = update ? RtlLookupFunctionEntry(update, &imageBase, nullptr) : nullptr;
    if (!function || nodeUpdate < imageBase + function->BeginAddress || nodeUpdate >= imageBase + function->EndAddress) {
        Logger::Instance().Warning(
            "Laser sight: the game's beam update was not found in this build, so under a lean a laser "
            "sight's beam does not start at the weapon");
        return;
    }
    g_updateFlagsAddress = reinterpret_cast<uintptr_t>(
        cameraunlock::memory::ResolveRIPRelative(reinterpret_cast<void*>(nodeUpdate), 2, 6));
    void* const target = reinterpret_cast<void*>(imageBase + function->BeginAddress);
    const auto result = MH_CreateHook(target, reinterpret_cast<void*>(&UpdateTrackedBeam),
                                      reinterpret_cast<void**>(&g_original));
    if (result != MH_OK) {
        Logger::Instance().Error("Laser sight beam hook: %s", MH_StatusToString(result));
        return;
    }
    Logger::Instance().Info("Laser sight beam hook installed at RVA 0x%llX",
                            static_cast<unsigned long long>(function->BeginAddress));
}

} // namespace StarfieldHT
