#include "pch.h"
#include "weapon_hook.h"
#include "core/logger.h"
#include "game/build_profile.h"
#include "game/weapon_projection.h"
#include "camera_hook.h"
#include "aim_hook.h"
#include "game/scene_layout.h"
#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {
namespace {

// The render camera the game passes around: eye position in the first three
// floats, then a 4x4 view matrix at +8 and its inverse at +24, all as one
// 40-float block. Its rotation is stored in columns rather than rows, so the
// basis is read out with a stride.
constexpr size_t kRenderCameraFloats  = 40;
constexpr size_t kRenderViewFloat     = 8;
constexpr size_t kRenderInverseFloat  = 24;
constexpr size_t kRenderBasisStride   = 4;

// The two passes the builder is called for, in the order it calls them. The
// weapon pass has to reuse the world pass's simulation snapshot, so its
// arguments are captured on pass 0 and consumed on pass 1.
constexpr uint8_t kWorldPass  = 0;
constexpr uint8_t kWeaponPass = 1;

// The submitted camera carries a handle into the renderer's component registry,
// and the rendered transform for that handle is looked up through it:
// registry -> manager, then the producer's index table and the reader's data
// array. 0xffffff is the registry's null handle and also the index mask.
constexpr uintptr_t kCameraHandleOffset   = 0x130;
constexpr uintptr_t kRegistryManagerOffset = 0x108;
constexpr uintptr_t kManagerIndicesOffset  = 0x2c8;
constexpr uintptr_t kManagerDataOffset     = 0x3c8;
constexpr uint32_t  kNullHandle            = 0xffffff;
constexpr uint32_t  kHandleIndexMask       = 0xffffff;
constexpr size_t    kComponentStride       = 0xa0;

// The builder runs once per world/weapon/overlay pass, with jitter packed into
// two floats on the stack. Camera matrices here use right/up/forward columns.
using BuildPass = void (*)(bool, bool, const float*, const float*, const bool*, uint64_t, uint8_t, void*);
BuildPass g_original = nullptr;
void* g_target = nullptr;
using SubmitCamera = uint64_t (*)(uintptr_t, void*);
SubmitCamera g_originalSubmit = nullptr;
void* g_submitTarget = nullptr;
uintptr_t g_registryAddress = 0;
CameraFrameHistory g_renderFrames;

// The submit hook runs once a rendered frame, so a read chain that has gone
// stale would otherwise write a line per frame for the rest of the session.
// Doubling keeps the first failure, the fact that it is still failing, and a
// running total, in a couple of dozen lines however long the session runs.
void ReportSubmitReadFailure() {
    static std::atomic<uint64_t> s_count{0};
    const uint64_t n = s_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) != 0) return;
    Logger::Instance().Error("Cannot read submitted player camera (total=%llu)",
                             static_cast<unsigned long long>(n));
}

CameraBasis RenderBasis(const float* camera) {
    CameraBasis basis{};
    for (size_t i = 0; i < 3; ++i) {
        const size_t column = kRenderViewFloat + i * kRenderBasisStride;
        basis.e[i] = camera[i];
        basis.r[i] = camera[column];
        basis.u[i] = camera[column + 1];
        basis.f[i] = camera[column + 2];
    }
    return basis;
}

uint64_t SubmitTrackedCamera(uintptr_t camera, void* context) {
    using cameraunlock::memory::SafeRead;
    NiMatrix44 local{};
    CameraFrame reference{};
    const bool playerCamera = camera == GetLiveCameraAddress();
    // The local transform is what identifies the simulation frame a submitted
    // camera came from. Its offset is the one the scene layout resolved against
    // this build, not a second constant that can silently disagree with it.
    const bool tracked = playerCamera
        && SafeRead(camera + GetSceneLayout().localTransformOffset, local)
        && FindLocalCameraFrame(camera, local, reference);
    const uint64_t result = g_originalSubmit(camera, context);
    if (!playerCamera) return result;
    if (!tracked) {
        g_renderFrames.Publish({});
        return result;
    }

    uintptr_t registry = 0, manager = 0, indices = 0, data = 0;
    uint32_t handle = 0, index = 0;
    float rendered[kRenderCameraFloats];
    if (SafeRead(camera + kCameraHandleOffset, handle) && handle != kNullHandle
        && SafeRead(g_registryAddress, registry)
        && SafeRead(registry + kRegistryManagerOffset, manager)
        && SafeRead(manager + kManagerIndicesOffset, indices)
        && SafeRead(indices + (handle & kHandleIndexMask) * sizeof(uint32_t), index)
        && SafeRead(manager + kManagerDataOffset, data)
        && SafeRead(data + index * kComponentStride, rendered)) {
        g_renderFrames.Publish(RebaseCameraFrame(reference, RenderBasis(rendered)));
    } else {
        ReportSubmitReadFailure();
    }
    return result;
}

void BuildWeaponPass(bool previous, bool reset, const float* camera, const float* frustum,
                     const bool* ortho, uint64_t jitter, uint8_t pass, void* output) {
    thread_local const float* worldCamera = nullptr;
    thread_local float worldRight = 0, worldTop = 0;
    thread_local CameraFrame frame{};
    thread_local CameraBasis drawn{};
    thread_local bool matched = false;
    if (pass == kWorldPass) {
        worldCamera = camera;
        worldRight = frustum[1];
        worldTop = frustum[2];
        drawn = RenderBasis(camera);
        // Both passes must use the same simulation snapshot even if the next
        // camera update publishes between them.
        // Stations can render through the exterior-coordinate camera as well.
        matched = !*ortho && (g_renderFrames.Find(drawn, frame)
            || FindConvertedCameraFrame(drawn, frame));
    }
    if (pass == kWeaponPass) {
        const bool correct = matched && camera == worldCamera && !*ortho
            && worldRight > 0 && worldTop > 0 && frustum[1] > 0 && frustum[2] > 0;
        matched = false;
        if (correct) {
            alignas(16) float adjusted[kRenderCameraFloats];
            std::memcpy(adjusted, camera, sizeof(adjusted));
            NiMatrix44 view{}, inverse{};
            CompensateWeaponProjection(frame.clean, drawn, worldRight / frustum[1], worldTop / frustum[2],
                                      adjusted, view, inverse);
            std::memcpy(adjusted + kRenderViewFloat, &view, sizeof(view));
            std::memcpy(adjusted + kRenderInverseFloat, &inverse, sizeof(inverse));
            g_original(previous, reset, adjusted, frustum, ortho, jitter, pass, output);
            return;
        }
    }
    g_original(previous, reset, camera, frustum, ortho, jitter, pass, output);
}
}

bool FindSubmittedCameraFrame(const CameraBasis& drawn, CameraFrame& out) {
    return g_renderFrames.Find(drawn, out, 1e-8f);
}

bool InstallWeaponHook() {
    const BuildProfile* profile = ResolveBuildProfile();
    if (!profile) return false;
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t base = 0;
    size_t moduleSize = 0;
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, base, moduleSize)) return false;
    // A profile whose RVAs do not land inside the running image would have the
    // hook patch, and the registry chain read, whatever now sits at that
    // address instead.
    if (profile->weaponPassRva >= moduleSize || profile->cameraSubmitRva >= moduleSize
        || profile->cameraRegistryRva >= moduleSize) {
        Logger::Instance().Error("Weapon render hook: a build profile RVA is outside the module");
        return false;
    }
    g_registryAddress = base + profile->cameraRegistryRva;
    g_target = reinterpret_cast<void*>(base + profile->weaponPassRva);
    const auto result = MH_CreateHook(g_target, reinterpret_cast<void*>(&BuildWeaponPass), reinterpret_cast<void**>(&g_original));
    if (result != MH_OK) {
        Logger::Instance().Error("Weapon render hook: %s", MH_StatusToString(result));
        g_target = nullptr;
        return false;
    }
    g_submitTarget = reinterpret_cast<void*>(base + profile->cameraSubmitRva);
    const auto submitResult = MH_CreateHook(g_submitTarget, reinterpret_cast<void*>(&SubmitTrackedCamera),
                                           reinterpret_cast<void**>(&g_originalSubmit));
    if (submitResult != MH_OK) {
        Logger::Instance().Error("Camera submission hook: %s", MH_StatusToString(submitResult));
        MH_RemoveHook(g_target);
        g_target = nullptr;
        g_submitTarget = nullptr;
        return false;
    }
    Logger::Instance().Info("Weapon render and camera submission hooks installed");
    return true;
}
}
