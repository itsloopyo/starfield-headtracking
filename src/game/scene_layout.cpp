#include "pch.h"
#include "scene_layout.h"

#include "core/logger.h"
#include "core/rtti_utils.h"
#include "game/camera_math.h"
#include "game/starfield_types.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeRead;

SceneLayout g_layout;
uintptr_t   g_niCameraVtable = 0;
int         g_cameraChildIndex = -2;  // -1 = the node slot holds the NiCamera itself
uintptr_t   g_moduleBase = 0;
size_t      g_moduleSize = 0;

constexpr uintptr_t kPointerSize = sizeof(uintptr_t);

bool InModule(uintptr_t p) {
    return g_moduleBase != 0 && p >= g_moduleBase && p < g_moduleBase + g_moduleSize;
}

// A heap object we are willing to walk: 8-byte aligned, outside the module
// image, and low enough to be a user-mode address.
bool LooksLikeObject(uintptr_t p) {
    return p > 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7) == 0 && !InModule(p);
}

bool ReadPtr(uintptr_t addr, uintptr_t& out) {
    return SafeRead(addr, out);
}

bool HasVtable(uintptr_t obj, uintptr_t vtable) {
    uintptr_t v = 0;
    return LooksLikeObject(obj) && ReadPtr(obj, v) && v == vtable;
}

bool NearlyEqual(float a, float b, float tol) {
    const float d = a - b;
    return (d < 0 ? -d : d) <= tol;
}

bool IsFinite(float f) {
    return f == f && f > -3.0e38f && f < 3.0e38f;
}

bool IsOrthonormal(const NiMatrix44& m) {
    for (int r = 0; r < 3; ++r) {
        float len2 = 0.0f;
        for (int c = 0; c < 3; ++c) {
            const float v = m.entry[r][c];
            if (!IsFinite(v) || v < -1.0001f || v > 1.0001f) return false;
            len2 += v * v;
        }
        if (!NearlyEqual(len2, 1.0f, 0.005f)) return false;
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            float dot = 0.0f;
            for (int c = 0; c < 3; ++c) dot += m.entry[a][c] * m.entry[b][c];
            if (!NearlyEqual(dot, 0.0f, 0.005f)) return false;
        }
    }
    return true;
}

// A scene-graph node transform: an orthonormal basis in rows 0..2 with a zero
// fourth column, and the translation in row 3 with a 1 beside it.
bool IsNodeTransform(const NiMatrix44& m) {
    if (!IsOrthonormal(m)) return false;
    for (int r = 0; r < 3; ++r) {
        if (m.entry[r][3] != 0.0f) return false;
    }
    for (int c = 0; c < 3; ++c) {
        if (!IsFinite(m.entry[3][c])) return false;
    }
    return m.entry[3][3] == 1.0f;
}

// The bounds a real NiFrustum has to fall inside. Wide enough to admit any
// field of view the game can render and any near/far pair it can ship with;
// narrow enough that six unrelated floats do not pass by accident.
constexpr float kMinTangent      = 0.05f;
constexpr float kMaxTangent      = 20.0f;
constexpr float kSymmetryTol     = 0.001f;   // fraction of the extent itself
constexpr float kMaxNearPlane    = 1000.0f;
constexpr float kMinFarNearRatio = 50.0f;
constexpr float kMinAspect       = 0.9f;
constexpr float kMaxAspect       = 4.0f;

// Symmetric about the view axis, with the far plane well beyond the near one.
bool IsFrustum(const NiFrustum& f) {
    if (!IsFinite(f.left) || !IsFinite(f.right) || !IsFinite(f.top)
        || !IsFinite(f.bottom) || !IsFinite(f.nearPlane) || !IsFinite(f.farPlane)) return false;
    if (!(f.right > kMinTangent && f.right < kMaxTangent)) return false;
    if (!(f.top > kMinTangent && f.top < kMaxTangent)) return false;
    if (!NearlyEqual(f.left, -f.right, f.right * kSymmetryTol)) return false;
    if (!NearlyEqual(f.bottom, -f.top, f.top * kSymmetryTol)) return false;
    if (!(f.nearPlane > 0.0f && f.nearPlane < kMaxNearPlane)) return false;
    if (!(f.farPlane > f.nearPlane * kMinFarNearRatio)) return false;
    const float aspect = f.right / f.top;
    return aspect > kMinAspect && aspect < kMaxAspect;
}

constexpr uintptr_t kMaxCameraScan   = 0x400;
constexpr uintptr_t kMaxNodeScan     = 0x400;
constexpr uintptr_t kMaxNiCameraScan = 0x220;
constexpr int       kMaxChildProbe   = 8;
constexpr uintptr_t kScanStart       = 8;
constexpr uintptr_t kScanStride      = 4;

// A stored clip matrix that differs from the rebuilt one by more than this in
// any single entry is not the matrix the renderer built from this frustum.
constexpr float kMaxClipError = 0.01f;

bool ProbeForNiCamera(uintptr_t slot, int& childIndex, uintptr_t& niCamera) {
    if (!LooksLikeObject(slot)) return false;
    if (HasVtable(slot, g_niCameraVtable)) {
        childIndex = -1;
        niCamera = slot;
        return true;
    }
    for (int i = 0; i < kMaxChildProbe; ++i) {
        uintptr_t child = 0;
        if (!ReadPtr(slot + i * kPointerSize, child)) return false;
        if (HasVtable(child, g_niCameraVtable)) {
            childIndex = i;
            niCamera = child;
            return true;
        }
    }
    return false;
}

bool FindCameraChain(uintptr_t playerCamera, uintptr_t& rootOffset,
                     uintptr_t& childrenOffset, int& childIndex,
                     uintptr_t& cameraRoot, uintptr_t& niCamera) {
    for (uintptr_t ro = kPointerSize; ro < kMaxCameraScan; ro += kPointerSize) {
        uintptr_t node = 0;
        if (!ReadPtr(playerCamera + ro, node)) continue;
        if (!LooksLikeObject(node)) continue;
        uintptr_t nodeVtable = 0;
        if (!ReadPtr(node, nodeVtable) || !InModule(nodeVtable)) continue;

        for (uintptr_t co = kPointerSize; co < kMaxNodeScan; co += kPointerSize) {
            uintptr_t slot = 0;
            if (!ReadPtr(node + co, slot)) continue;
            if (ProbeForNiCamera(slot, childIndex, niCamera)) {
                rootOffset = ro;
                childrenOffset = co;
                cameraRoot = node;
                return true;
            }
        }
    }
    return false;
}

// Every offset in the NiCamera that could be a node transform or a frustum,
// rather than the first of each. The first is not necessarily the right one: an
// unrelated member that happens to hold three near-unit rows passes
// IsNodeTransform, and six floats in the right ranges pass IsFrustum. Committing
// to it left the mod permanently dormant on a build where the SECOND candidate
// would have resolved cleanly, because resolution retries every camera update
// and re-derives the identical first answer every time.
constexpr int kMaxTransformCandidates = 16;
constexpr int kMaxFrustumCandidates   = 8;

struct FrustumCandidate {
    uintptr_t offset;
    NiFrustum frustum;
};

int CollectNodeTransforms(uintptr_t niCamera, uintptr_t* out, int capacity) {
    int found = 0;
    for (uintptr_t o = kScanStart; o + sizeof(NiMatrix44) <= kMaxNiCameraScan; o += kScanStride) {
        NiMatrix44 m{};
        if (!SafeRead(niCamera + o, m)) continue;
        if (!IsNodeTransform(m)) continue;
        out[found++] = o;
        if (found == capacity) break;
    }
    return found;
}

int CollectFrustums(uintptr_t niCamera, FrustumCandidate* out, int capacity) {
    int found = 0;
    for (uintptr_t o = kScanStart; o + sizeof(NiFrustum) <= kMaxNiCameraScan; o += kScanStride) {
        NiFrustum f{};
        if (!SafeRead(niCamera + o, f)) continue;
        if (!IsFrustum(f)) continue;
        out[found].offset = o;
        out[found].frustum = f;
        ++found;
        if (found == capacity) break;
    }
    return found;
}

// How far `local` composed with the parent's world transform lands from the
// stored world transform - the scene graph's own rule, world = local *
// parentWorld, used backwards to say which candidate is the local one.
//
// It is a score rather than a gate because the two are one rebuild apart at this
// point in the frame: the world transform still holds what the previous frame's
// local produced, so a camera that is moving leaves a residual proportional to
// how fast. Ranking on it still picks the real local transform out of a field of
// decoys, which committing to the lowest offset did not.
float LocalCompositionResidual(uintptr_t niCamera, uintptr_t localOffset,
                               const NiMatrix44& rootWorld, const NiMatrix44& world) {
    NiMatrix44 local{};
    if (!SafeRead(niCamera + localOffset, local)) return 1e30f;

    const Mat3 rootRot = RotationOf(rootWorld);
    const Mat3 expectedRot = Mul(RotationOf(local), rootRot);
    float worst = 0.0f;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const float d = fabsf(expectedRot.m[r][c] - world.entry[r][c]);
            if (d > worst) worst = d;
        }
    }

    float expectedEye[3] = {};
    MulRowVec(&local.entry[3][0], rootRot, expectedEye);
    for (int i = 0; i < 3; ++i) {
        const float d = fabsf(expectedEye[i] + rootWorld.entry[3][i] - world.entry[3][i]);
        if (d > worst) worst = d;
    }
    return worst;
}

float ClipMismatch(const NiMatrix44& a, const NiMatrix44& b) {
    float worst = 0.0f;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float d = fabsf(a.entry[r][c] - b.entry[r][c]);
            if (d > worst) worst = d;
        }
    }
    return worst;
}

// The clip matrix is a pure function of the world transform and the frustum, so
// it is identified by rebuilding it and looking for the copy the engine stored.
// That doubles as proof that the world transform and frustum offsets are the
// ones the renderer actually consumes. Reports 0 when nothing matched closely
// enough, with `outError` carrying how close the best candidate got.
uintptr_t FindClipOffset(uintptr_t niCamera, uintptr_t worldOffset, const NiFrustum& frustum,
                         float& outError) {
    // Not 0: the caller keeps the smallest error it has seen across every pair
    // it tried, and a pair that could not even be read reporting a perfect score
    // made the dormancy line claim an exact match for a total miss.
    outError = 1e30f;
    NiMatrix44 world{};
    if (!SafeRead(niCamera + worldOffset, world)) return 0;

    CameraBasis basis{};
    ReadBasis(world, basis);
    NiMatrix44 expected{};
    BuildWorldToClip(basis, frustum, expected);

    uintptr_t bestOffset = 0;
    float best = 1e30f;
    for (uintptr_t o = kScanStart; o + sizeof(NiMatrix44) <= kMaxNiCameraScan; o += kScanStride) {
        NiMatrix44 stored{};
        if (!SafeRead(niCamera + o, stored)) continue;
        const float err = ClipMismatch(stored, expected);
        if (err < best) {
            best = err;
            bestOffset = o;
        }
    }
    outError = best;
    return best > kMaxClipError ? 0 : bestOffset;
}

// Both residuals carry a sentinel meaning "never measured", and printing that
// as a float gives a triager 1e30 where they needed to see that no pair was
// tried at all.
const char* FormatResidual(float value, char (&buffer)[24]) {
    if (value >= 1e29f) return "n/a";
    snprintf(buffer, sizeof(buffer), "%.5f", value);
    return buffer;
}

void LogFloatWindow(const char* what, uintptr_t base, uintptr_t from, uintptr_t to) {
    Logger::Instance().Info("%s float dump 0x%llX..0x%llX:", what,
                            static_cast<unsigned long long>(from),
                            static_cast<unsigned long long>(to));
    for (uintptr_t o = from; o < to; o += 32) {
        float f[8] = {};
        if (!SafeRead(base + o, f)) return;
        Logger::Instance().Info("  +0x%03llX %+.4f %+.4f %+.4f %+.4f %+.4f %+.4f %+.4f %+.4f",
                                static_cast<unsigned long long>(o),
                                f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
    }
}

} // namespace

const SceneLayout& GetSceneLayout() { return g_layout; }

bool ResolveSceneLayout(void* playerCamera) {
    if (g_layout.valid) return true;
    if (!playerCamera) return false;

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, g_moduleBase, g_moduleSize)) return false;

    if (g_niCameraVtable == 0) {
        g_niCameraVtable = FindVtableByRTTI(g_moduleBase, g_moduleSize, ".?AVNiCamera@@");
        if (g_niCameraVtable == 0) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                Logger::Instance().Error("Scene layout: NiCamera vtable not found - staying dormant "
                                         "(retrying every camera update, reported once)");
            }
            return false;
        }
        Logger::Instance().Info("Scene layout: NiCamera vtable 0x%llX (RVA 0x%llX)",
                                static_cast<unsigned long long>(g_niCameraVtable),
                                static_cast<unsigned long long>(g_niCameraVtable - g_moduleBase));
    }

    const uintptr_t cam = reinterpret_cast<uintptr_t>(playerCamera);
    uintptr_t rootOffset = 0, childrenOffset = 0, cameraRoot = 0, niCamera = 0;
    int childIndex = -2;
    if (!FindCameraChain(cam, rootOffset, childrenOffset, childIndex, cameraRoot, niCamera)) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Logger::Instance().Warning(
                "Scene layout: no NiCamera reachable from PlayerCamera yet (normal before a save "
                "is loaded; retrying every camera update)");
        }
        return false;
    }

    uintptr_t transforms[kMaxTransformCandidates] = {};
    const int transformCount = CollectNodeTransforms(niCamera, transforms, kMaxTransformCandidates);

    FrustumCandidate frustums[kMaxFrustumCandidates] = {};
    const int frustumCount = CollectFrustums(niCamera, frustums, kMaxFrustumCandidates);

    // The world transform and the frustum are proved together, by rebuilding the
    // clip matrix from them and finding the copy the engine stored. Every pair is
    // tried rather than only the first of each, so one decoy does not wedge the
    // whole resolution.
    uintptr_t worldOffset = 0, frustumOffset = 0, clipOffset = 0;
    NiFrustum frustum{};
    float clipError = 1e30f;
    for (int w = 0; w < transformCount && clipOffset == 0; ++w) {
        for (int f = 0; f < frustumCount && clipOffset == 0; ++f) {
            float error = 0.0f;
            const uintptr_t candidate =
                FindClipOffset(niCamera, transforms[w], frustums[f].frustum, error);
            if (error < clipError) clipError = error;
            if (candidate == 0) continue;
            worldOffset = transforms[w];
            frustumOffset = frustums[f].offset;
            frustum = frustums[f].frustum;
            clipOffset = candidate;
        }
    }

    // The local transform is the remaining candidate that best satisfies the
    // scene graph's own composition rule against the world transform just proved.
    // "Best" is not enough on its own - the winner of a field of decoys is still
    // a decoy - so it also has to be close. The two are one rebuild apart at this
    // point in the frame, so the bound has to absorb a frame of camera motion:
    // a fast mouse flick moves a basis entry by about 0.2 and a sprint moves the
    // eye about 0.1m, while an unrelated node transform is out by order 1.
    constexpr float kMaxLocalResidual = 0.5f;
    uintptr_t localOffset = 0;
    float localResidual = 1e30f;
    if (worldOffset != 0) {
        NiMatrix44 rootWorld{}, world{};
        if (SafeRead(cameraRoot + worldOffset, rootWorld) && SafeRead(niCamera + worldOffset, world)) {
            for (int i = 0; i < transformCount; ++i) {
                if (transforms[i] == worldOffset) continue;
                const float residual =
                    LocalCompositionResidual(niCamera, transforms[i], rootWorld, world);
                if (residual >= localResidual) continue;
                localResidual = residual;
                localOffset = transforms[i];
            }
        }
        if (localResidual > kMaxLocalResidual) localOffset = 0;
    }

    if (localOffset == 0 || worldOffset == 0 || frustumOffset == 0 || clipOffset == 0) {
        // Reported once: this runs on every camera update until it succeeds, and
        // the float window below is 17 lines a call. A later success still says
        // so on its own line, so one report is enough to tell the two apart.
        static bool logged = false;
        if (!logged) {
            logged = true;
            char clipText[24], localText[24];
            Logger::Instance().Error(
                "Scene layout: NiCamera members not identified (local=0x%llX world=0x%llX "
                "frustum=0x%llX clip=0x%llX, %d transform and %d frustum candidates, best clip "
                "error %s, local residual %s) - staying dormant, retrying every camera update",
                static_cast<unsigned long long>(localOffset),
                static_cast<unsigned long long>(worldOffset),
                static_cast<unsigned long long>(frustumOffset),
                static_cast<unsigned long long>(clipOffset),
                transformCount, frustumCount,
                FormatResidual(clipError, clipText), FormatResidual(localResidual, localText));
            LogFloatWindow("NiCamera", niCamera, 0, kMaxNiCameraScan);
        }
        return false;
    }

    g_layout.cameraRootOffset     = rootOffset;
    g_layout.childrenDataOffset   = childrenOffset;
    g_layout.localTransformOffset = localOffset;
    g_layout.worldTransformOffset = worldOffset;
    g_layout.worldToClipOffset    = clipOffset;
    g_layout.frustumOffset        = frustumOffset;
    g_layout.valid = true;
    g_cameraChildIndex = childIndex;

    Logger::Instance().Info(
        "Scene layout resolved: cameraRoot=+0x%llX children=+0x%llX childIndex=%d "
        "local=+0x%llX (composition residual %.6f) world=+0x%llX clip=+0x%llX (error %.6f) "
        "frustum=+0x%llX",
        static_cast<unsigned long long>(rootOffset),
        static_cast<unsigned long long>(childrenOffset),
        childIndex,
        static_cast<unsigned long long>(localOffset), localResidual,
        static_cast<unsigned long long>(worldOffset),
        static_cast<unsigned long long>(clipOffset), clipError,
        static_cast<unsigned long long>(frustumOffset));
    Logger::Instance().Info(
        "Frustum: right=%.4f top=%.4f near=%.3f far=%.1f (HFOV %.1f deg, VFOV %.1f deg, aspect %.3f)",
        frustum.right, frustum.top, frustum.nearPlane, frustum.farPlane,
        2.0f * atanf(frustum.right) * RAD_TO_DEG,
        2.0f * atanf(frustum.top) * RAD_TO_DEG,
        frustum.right / frustum.top);
    return true;
}

bool GetSceneGraph(void* playerCamera, uintptr_t& cameraRoot, uintptr_t& niCamera) {
    if (!g_layout.valid || !playerCamera) return false;
    const uintptr_t cam = reinterpret_cast<uintptr_t>(playerCamera);
    uintptr_t root = 0;
    if (!ReadPtr(cam + g_layout.cameraRootOffset, root) || !LooksLikeObject(root)) return false;
    uintptr_t slot = 0;
    if (!ReadPtr(root + g_layout.childrenDataOffset, slot) || !LooksLikeObject(slot)) return false;

    uintptr_t camera = slot;
    if (g_cameraChildIndex >= 0) {
        if (!ReadPtr(slot + g_cameraChildIndex * kPointerSize, camera)) return false;
    }
    if (!HasVtable(camera, g_niCameraVtable)) return false;

    cameraRoot = root;
    niCamera = camera;
    return true;
}

void LogCameraSurvey(uintptr_t cameraRoot, uintptr_t niCamera) {
    const SceneLayout& L = GetSceneLayout();
    if (!L.valid) return;
    auto& log = Logger::Instance();

    NiMatrix44 rootWorld{}, camLocal{}, camWorld{}, clip{};
    NiFrustum frustum{};
    if (!SafeRead(cameraRoot + L.worldTransformOffset, rootWorld)) return;
    if (!SafeRead(niCamera + L.localTransformOffset, camLocal)) return;
    if (!SafeRead(niCamera + L.worldTransformOffset, camWorld)) return;
    if (!SafeRead(niCamera + L.worldToClipOffset, clip)) return;
    if (!SafeRead(niCamera + L.frustumOffset, frustum)) return;

    log.Info("--- camera survey (root=0x%llX cam=0x%llX) ---",
             static_cast<unsigned long long>(cameraRoot),
             static_cast<unsigned long long>(niCamera));
    for (int i = 0; i < 4; ++i) {
        log.Info("rootWorld[%d] %+.5f %+.5f %+.5f %+.5f", i,
                 rootWorld.entry[i][0], rootWorld.entry[i][1], rootWorld.entry[i][2], rootWorld.entry[i][3]);
    }
    for (int i = 0; i < 4; ++i) {
        log.Info("camLocal[%d]  %+.5f %+.5f %+.5f %+.5f", i,
                 camLocal.entry[i][0], camLocal.entry[i][1], camLocal.entry[i][2], camLocal.entry[i][3]);
    }
    for (int i = 0; i < 4; ++i) {
        log.Info("camWorld[%d]  %+.5f %+.5f %+.5f %+.5f", i,
                 camWorld.entry[i][0], camWorld.entry[i][1], camWorld.entry[i][2], camWorld.entry[i][3]);
    }
    CameraBasis basis{};
    ReadBasis(camWorld, basis);
    NiMatrix44 expected{};
    BuildWorldToClip(basis, frustum, expected);
    log.Info("clip rebuild worst-entry error %.6f", ClipMismatch(clip, expected));
    log.Info("frustum r=%.4f t=%.4f n=%.3f f=%.1f",
             frustum.right, frustum.top, frustum.nearPlane, frustum.farPlane);
    log.Info("--- end camera survey ---");
}

} // namespace StarfieldHT
