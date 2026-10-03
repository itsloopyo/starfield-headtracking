#include "pch.h"
#include "sight_depth.h"

#include "core/logger.h"
#include "game/scene_layout.h"
#include "game/scene_nodes.h"

#include <cameraunlock/memory/safe_memory.h>

#include <limits>

namespace StarfieldHT::SightDepth {
namespace {

using cameraunlock::memory::SafeRead;
using namespace SceneNodes;

// A held weapon hangs its sights off this attach node, iron sights and optics
// alike. On the Eon pistol it sits on the aim line 0.394 m from the eye with the
// sights up, which is where the rear sight was measured from the screen (0.39 to
// 0.40 m, from how fast it grows as the eye closes on it).
constexpr char kSightNodeName[] = "P-Scope";

// The player's skeleton was 191 nodes with a pistol in hand, the sight node
// nine levels down.
constexpr int kMaxNodes = 600;

// The node is looked for again this often rather than kept: a load or a weapon
// swap frees it, and nothing says so.
constexpr uint64_t kRefreshMs = 100;
constexpr uint64_t kMissingLogIntervalMs = 5000;

float g_depth = std::numeric_limits<float>::infinity();
uint64_t g_lastRefreshMs = 0;

// Breadth first from the skeleton's Root, which is the Camera bone's parent and
// the ancestor of the weapon.
uintptr_t FindSightNode(uintptr_t root) {
    uintptr_t queue[kMaxNodes];
    int head = 0, tail = 0;
    queue[tail++] = root;
    while (head < tail) {
        const uintptr_t node = queue[head++];
        if (NameStartsWith(node, kSightNodeName, sizeof(kSightNodeName))) return node;
        uintptr_t children[kMaxChildren];
        const uint16_t count = ReadChildren(node, children);
        for (uint16_t i = 0; i < count && tail < kMaxNodes; ++i) queue[tail++] = children[i];
    }
    return 0;
}

bool ReadDepth(const CameraBasis& clean, float& depth) {
    const uintptr_t bone = ReadCameraBone();
    uintptr_t root = 0;
    if (bone == 0 || !SafeRead(bone + GetSceneLayout().parentOffset, root) || !IsObject(root)) return false;
    const uintptr_t sight = FindSightNode(root);
    if (sight == 0) return false;

    // Both world transforms are the ones the scene graph built last frame, so
    // the sight is measured from the bone of that same frame rather than from
    // this frame's eye: the player's own movement between the two cancels.
    NiMatrix44 sightWorld{}, boneWorld{};
    const uintptr_t worldOffset = GetSceneLayout().worldTransformOffset;
    if (!SafeRead(sight + worldOffset, sightWorld) || !SafeRead(bone + worldOffset, boneWorld)) return false;
    float toSight[3];
    for (int i = 0; i < 3; ++i) toSight[i] = sightWorld.entry[3][i] - boneWorld.entry[3][i];
    depth = Dot3(toSight, clean.f);
    return std::isfinite(depth);
}

} // namespace

void Update(const CameraBasis& clean, bool aiming) {
    if (!aiming) {
        g_depth = std::numeric_limits<float>::infinity();
        g_lastRefreshMs = 0;
        return;
    }
    const uint64_t now = GetTickCount64();
    if (now - g_lastRefreshMs < kRefreshMs) return;
    g_lastRefreshMs = now;

    float depth = 0.0f;
    if (ReadDepth(clean, depth)) {
        g_depth = depth;
        return;
    }
    g_depth = std::numeric_limits<float>::infinity();
    static uint64_t s_lastLogMs = 0;
    if (now - s_lastLogMs >= kMissingLogIntervalMs) {
        s_lastLogMs = now;
        Logger::Instance().Warning(
            "Sights are up and the held weapon has no %s node, so leaning in is not stopped at "
            "its rear sight",
            kSightNodeName);
    }
}

float Get() { return g_depth; }

} // namespace StarfieldHT::SightDepth
