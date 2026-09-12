#include "pch.h"
#include "aim_hook.h"

#include "core/logger.h"
#include "game/build_profile.h"
#include "game/starfield_types.h"
#include "game/scene_layout.h"
#include "game/ship_aim.h"
#include "hooks/camera_hook.h"
#include "hooks/weapon_hook.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeWrite;

// Projects a bounding sphere through the camera's world-to-clip matrix and
// writes the screen bounds it covers. The renderer's culling calls it for every
// candidate object, thousands of times a frame, and it is the earliest point
// that is guaranteed to run only after the matrix the frame is drawn with has
// been built.
//
// Its four arguments are the camera, the sphere and two float outputs - all
// pointers, which is what makes it safe to wrap: a detour taking four integer
// registers forwards them untouched. The larger render function tried before it
// takes a float in xmm1, which a C detour cannot forward, and MinHook could not
// reliably patch its prologue either.
typedef uint64_t (__fastcall *ScreenProject_t)(void* camera, void* sphere,
                                               void* outMin, void* outMax);
ScreenProject_t g_original = nullptr;
void* g_hooked = nullptr;

using ShipAim = bool (*)(uintptr_t, uintptr_t, const float*, uintptr_t, uintptr_t,
                        const float*, float, float*);
using ShipPilot = uintptr_t (*)(uintptr_t);
ShipAim g_shipAim = nullptr;
ShipPilot g_shipPilot = nullptr;
uintptr_t g_playerAddress = 0;

using ConvertShipCamera = void (*)(uintptr_t, uintptr_t, uintptr_t, float*);
ConvertShipCamera g_convertShipCamera = nullptr;
CameraFrameHistory g_shipFrames;
using ActiveCamera = uintptr_t (*)(uintptr_t);
ActiveCamera g_activeCamera = nullptr;
uintptr_t g_shipLockCameraReturn = 0;
uintptr_t g_selectionCameraManager = 0;
using PickShipTarget = uintptr_t (*)(uintptr_t, uint32_t);
using ScoreShipTarget = void (*)(uintptr_t*, uint32_t, uint32_t, uintptr_t, uintptr_t, uintptr_t);
using ScoreSpaceTarget = void (*)(uintptr_t*, uint32_t);
PickShipTarget g_pickShipTarget = nullptr;
ScoreShipTarget g_scoreShipTarget = nullptr;
ScoreSpaceTarget g_scoreSpaceTarget = nullptr;
thread_local const CameraFrame* g_selectionFrame = nullptr;

uintptr_t PickWithCleanAim(uintptr_t result, uint32_t flags) {
    uintptr_t manager = 0, camera = 0;
    NiMatrix44 world{};
    CameraBasis drawn{};
    CameraFrame frame{};
    bool matched = false;
    if (cameraunlock::memory::SafeRead(g_selectionCameraManager, manager)
        && cameraunlock::memory::SafeRead(manager + 0xa8, camera)
        && cameraunlock::memory::SafeRead(camera + GetSceneLayout().worldTransformOffset, world)) {
        BasisFromRotation(RotationOf(world), world.entry[3], drawn);
        matched = camera == GetLiveCameraAddress()
            ? FindSubmittedCameraFrame(drawn, frame) : FindConvertedCameraFrame(drawn, frame);
    }
    if (!matched) {
        static thread_local ULONGLONG lastMismatch = 0;
        const auto now = GetTickCount64();
        if (now - lastMismatch >= 1000) {
            lastMismatch = now;
            Logger::Instance().Error("Ship target picker: no matching camera pose");
        }
    }
    const auto previous = g_selectionFrame;
    g_selectionFrame = matched ? &frame : nullptr;
    const auto returned = g_pickShipTarget(result, flags);
    g_selectionFrame = previous;
    return returned;
}

void ScoreWithCleanAim(uintptr_t* context, uint32_t target, uint32_t kind,
                       uintptr_t body, uintptr_t playerBody, uintptr_t reference) {
    if (g_selectionFrame) {
        // These vectors belong to the picker's stack and are shared by its
        // candidate scorers. Changing them leaves the rendered camera intact.
        std::memcpy(reinterpret_cast<void*>(context[1]), g_selectionFrame->clean.e, sizeof(float) * 3);
        std::memcpy(reinterpret_cast<void*>(context[3]), g_selectionFrame->clean.f, sizeof(float) * 3);
    }
    g_scoreShipTarget(context, target, kind, body, playerBody, reference);
}

void ScoreSpaceWithCleanAim(uintptr_t* context, uint32_t target) {
    if (g_selectionFrame) {
        std::memcpy(reinterpret_cast<void*>(context[0]), g_selectionFrame->clean.e, sizeof(float) * 3);
        std::memcpy(reinterpret_cast<void*>(context[1]), g_selectionFrame->clean.f, sizeof(float) * 3);
    }
    g_scoreSpaceTarget(context, target);
}
thread_local float g_lockAngleSample = 0;
using LockAngle = float (*)(uintptr_t, uintptr_t);
LockAngle g_lockAngle = nullptr;
float MeasureLockAngle(uintptr_t ship, uintptr_t target) {
    const float angle = g_lockAngle(ship, target);
    g_lockAngleSample = angle;
    return angle;
}
using UpdateLock = void (*)(uintptr_t, uintptr_t);
UpdateLock g_updateLock = nullptr;
void UpdateShipLock(uintptr_t component, uintptr_t ship) {
    g_lockAngleSample = -1.0f;
    g_updateLock(component, ship);
    uintptr_t player = 0;
    cameraunlock::memory::SafeRead(g_playerAddress, player);
    if (g_shipPilot(ship) != player) return;
    static thread_local ULONGLONG last = 0;
    if (GetTickCount64() - last < 1000) return;
    last = GetTickCount64();
    float strength = 0;
    uint32_t target = 0;
    cameraunlock::memory::SafeRead(component + 200, strength);
    cameraunlock::memory::SafeRead(component + 0xc0, target);
    Logger::Instance().Info("Ship lock: target=%08X strength=%.5f angle=%.4f rad", target, strength, g_lockAngleSample);
}

uintptr_t GetShipLockCamera(uintptr_t manager) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t camera = g_activeCamera(manager);
    if (caller != g_shipLockCameraReturn || camera == 0) return camera;

    CameraFrame frame{};
    if (!GetCameraFrame(frame)) return camera;
    alignas(16) thread_local unsigned char snapshot[0x220];
    if (!cameraunlock::memory::SafeRead(camera, snapshot)) {
        Logger::Instance().Error("Ship lock: cannot read the camera snapshot");
        return camera;
    }
    const auto worldOffset = GetSceneLayout().worldTransformOffset;
    NiMatrix44 world{};
    std::memcpy(&world, snapshot + worldOffset, sizeof(world));
    CameraBasis drawn{};
    BasisFromRotation(RotationOf(world), world.entry[3], drawn);
    const bool matched = camera == GetLiveCameraAddress()
        ? FindSubmittedCameraFrame(drawn, frame)
        : FindConvertedCameraFrame(drawn, frame);
    if (!matched) {
        static thread_local ULONGLONG lastMismatch = 0;
        const auto now = GetTickCount64();
        if (now - lastMismatch >= 1000) {
            lastMismatch = now;
            Logger::Instance().Error("Ship lock: no matching camera pose");
        }
        return camera;
    }
    // The angle routine copies this basis immediately. Keep its camera read
    // private so rendering and HUD projection retain the tracked view.
    WriteBasis(frame.clean, world);
    std::memcpy(snapshot + worldOffset, &world, sizeof(world));
    return reinterpret_cast<uintptr_t>(snapshot);
}

void ConvertTrackedShipCamera(uintptr_t context, uintptr_t cell, uintptr_t source, float* parent) {
    CameraFrame reference{};
    NiMatrix44 sourceWorld{};
    CameraBasis drawn{};
    bool matched = false;
    if (source == GetLiveCameraAddress()
        && cameraunlock::memory::SafeRead(source + GetSceneLayout().worldTransformOffset, sourceWorld)) {
        BasisFromRotation(RotationOf(sourceWorld), sourceWorld.entry[3], drawn);
        // The exterior view can still carry an older head pose when firing.
        // Bind the actual submitted source before the coordinate conversion.
        matched = FindSubmittedCameraFrame(drawn, reference);
    }
    g_convertShipCamera(context, cell, source, parent);
    if (matched) {
        Mat3 parentRotation{};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) parentRotation.m[i][j] = parent[i * 4 + j];
        }
        const auto exact = RebaseCameraFrame(reference, drawn);
        g_shipFrames.Publish(MakeShipCameraFrame(exact, parentRotation));
    } else {
        g_shipFrames.Publish({});
    }
}

bool BuildShipAim(uintptr_t context, uintptr_t camera, const float* screen, uintptr_t ship,
                  uintptr_t weaponContext, const float* muzzle, float range, float* output) {
    CameraFrame frame{};
    uintptr_t player = 0;
    if (!GetCameraFrame(frame) || camera == frame.niCamera) {
        return g_shipAim(context, camera, screen, ship, weaponContext, muzzle, range, output);
    }
    if (!cameraunlock::memory::SafeRead(g_playerAddress, player)) {
        Logger::Instance().Error("Ship aim: cannot read the player singleton");
        return g_shipAim(context, camera, screen, ship, weaponContext, muzzle, range, output);
    }
    if (player == 0 || g_shipPilot(ship) != player) {
        return g_shipAim(context, camera, screen, ship, weaponContext, muzzle, range, output);
    }

    // Ship weapons use a second NiCamera in the ship's exterior coordinate space.
    // The ray builder reads its world transform, frustum and viewport without
    // retaining the pointer. A private snapshot keeps rendering out of this write.
    alignas(16) unsigned char snapshot[0x220];
    if (!cameraunlock::memory::SafeRead(camera, snapshot)) {
        Logger::Instance().Error("Ship aim: cannot read the camera snapshot");
        return g_shipAim(context, camera, screen, ship, weaponContext, muzzle, range, output);
    }
    const auto worldOffset = GetSceneLayout().worldTransformOffset;
    NiMatrix44 world{};
    std::memcpy(&world, snapshot + worldOffset, sizeof(world));
    CameraBasis drawn{};
    BasisFromRotation(RotationOf(world), world.entry[3], drawn);
    if (!FindConvertedCameraFrame(drawn, frame)) {
        Logger::Instance().Error("Ship aim: no matching converted camera pose");
        return g_shipAim(context, camera, screen, ship, weaponContext, muzzle, range, output);
    }
    WriteBasis(frame.clean, world);
    std::memcpy(snapshot + worldOffset, &world, sizeof(world));
    return g_shipAim(context, reinterpret_cast<uintptr_t>(snapshot), screen, ship,
                     weaponContext, muzzle, range, output);
}

// The token makes this the first call after each camera update that does the
// restoring; the thousands that follow it in the same frame cost one atomic
// load each.
std::atomic<uint32_t> g_restoredToken{0};

uint64_t __fastcall ScreenProjectHook(void* camera, void* sphere, void* outMin, void* outMax) {
    uint32_t token = g_restoredToken.load(std::memory_order_relaxed);
    const uint32_t previous = token;
    uintptr_t niCamera = 0, offset = 0;
    // Left uninitialised on purpose: this runs thousands of times a frame and
    // the read only fills it on the one call per frame that has work to do.
    NiMatrix44 cleanWorld;
    const bool restore = GetCleanWorldTransform(token, niCamera, offset, cleanWorld);
    // A failed write leaves the token where it was so the next call retries.
    if (token != previous && (!restore || SafeWrite(niCamera + offset, cleanWorld))) {
        g_restoredToken.store(token, std::memory_order_relaxed);
    }
    return g_original(camera, sphere, outMin, outMax);
}

} // namespace

bool FindConvertedCameraFrame(const CameraBasis& drawn, CameraFrame& out) {
    auto lookup = drawn;
    for (float& coordinate : lookup.e) coordinate = 0;
    CameraFrame reference{};
    if (!g_shipFrames.Find(lookup, reference, 1e-8f)) return false;
    out = RebaseCameraFrame(reference, drawn);
    return true;
}

bool InstallAimHook() {
    const BuildProfile* profile = ResolveBuildProfile();
    if (profile == nullptr) return false;

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return false;
    if (profile->screenProjectRva >= moduleSize || profile->shipAimRva >= moduleSize
        || profile->shipPilotRva >= moduleSize || profile->playerSingletonRva >= moduleSize
        || profile->shipCameraConvertRva >= moduleSize
        || profile->activeCameraRva >= moduleSize || profile->shipLockCameraReturnRva >= moduleSize
        || profile->selectionCameraManagerRva >= moduleSize || profile->pickShipTargetRva >= moduleSize
        || profile->scoreShipTargetRva >= moduleSize || profile->scoreSpaceTargetRva >= moduleSize
        || profile->shipLockAngleRva >= moduleSize || profile->shipLockUpdateRva >= moduleSize) {
        Logger::Instance().Error("Aim hook: the profile RVA is outside the module");
        return false;
    }

    g_playerAddress = moduleBase + profile->playerSingletonRva;
    g_shipPilot = reinterpret_cast<ShipPilot>(moduleBase + profile->shipPilotRva);
    g_shipLockCameraReturn = moduleBase + profile->shipLockCameraReturnRva;
    g_selectionCameraManager = moduleBase + profile->selectionCameraManagerRva;
    g_hooked = reinterpret_cast<void*>(moduleBase + profile->screenProjectRva);
    const MH_STATUS st = MH_CreateHook(g_hooked, reinterpret_cast<LPVOID>(&ScreenProjectHook),
                                       reinterpret_cast<LPVOID*>(&g_original));
    if (st != MH_OK) {
        Logger::Instance().Error("Aim hook: MH_CreateHook at RVA 0x%llX failed: %d",
                                 static_cast<unsigned long long>(profile->screenProjectRva),
                                 static_cast<int>(st));
        g_hooked = nullptr;
        return false;
    }

    const auto shipStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->shipAimRva),
        reinterpret_cast<void*>(&BuildShipAim), reinterpret_cast<void**>(&g_shipAim));
    if (shipStatus != MH_OK) {
        Logger::Instance().Error("Ship aim hook: %s", MH_StatusToString(shipStatus));
        MH_RemoveHook(g_hooked);
        g_hooked = nullptr;
        return false;
    }
    const auto convertStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->shipCameraConvertRva),
        reinterpret_cast<void*>(&ConvertTrackedShipCamera), reinterpret_cast<void**>(&g_convertShipCamera));
    if (convertStatus != MH_OK) {
        Logger::Instance().Error("Ship camera conversion hook: %s", MH_StatusToString(convertStatus));
        MH_RemoveHook(reinterpret_cast<void*>(moduleBase + profile->shipAimRva));
        MH_RemoveHook(g_hooked);
        g_hooked = nullptr;
        return false;
    }
    Logger::Instance().Info("Ship weapon aim hook installed at RVA 0x%llX",
                            static_cast<unsigned long long>(profile->shipAimRva));
    const auto cameraStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->activeCameraRva),
        reinterpret_cast<void*>(&GetShipLockCamera), reinterpret_cast<void**>(&g_activeCamera));
    if (cameraStatus != MH_OK) {
        Logger::Instance().Error("Ship lock camera hook: %s", MH_StatusToString(cameraStatus));
        return false;
    }
    Logger::Instance().Info("Ship lock camera hook installed at RVA 0x%llX",
                            static_cast<unsigned long long>(profile->activeCameraRva));
    const auto angleStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->shipLockAngleRva),
        reinterpret_cast<void*>(&MeasureLockAngle), reinterpret_cast<void**>(&g_lockAngle));
    const auto probeStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->shipLockUpdateRva),
        reinterpret_cast<void*>(&UpdateShipLock), reinterpret_cast<void**>(&g_updateLock));
    if (angleStatus != MH_OK || probeStatus != MH_OK) {
        Logger::Instance().Error("Ship lock diagnostics: angle=%d update=%d", angleStatus, probeStatus);
        return false;
    }
    const auto pickStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->pickShipTargetRva),
        reinterpret_cast<void*>(&PickWithCleanAim), reinterpret_cast<void**>(&g_pickShipTarget));
    const auto scoreStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->scoreShipTargetRva),
        reinterpret_cast<void*>(&ScoreWithCleanAim), reinterpret_cast<void**>(&g_scoreShipTarget));
    const auto spaceStatus = MH_CreateHook(reinterpret_cast<void*>(moduleBase + profile->scoreSpaceTargetRva),
        reinterpret_cast<void*>(&ScoreSpaceWithCleanAim), reinterpret_cast<void**>(&g_scoreSpaceTarget));
    if (pickStatus != MH_OK || scoreStatus != MH_OK || spaceStatus != MH_OK) {
        Logger::Instance().Error("Ship selection hooks: %d %d %d", pickStatus, scoreStatus, spaceStatus);
        return false;
    }
    Logger::Instance().Info("Ship target picker hooks installed");
    Logger::Instance().Info("Aim hook installed at RVA 0x%llX - the mouse keeps the aim",
                            static_cast<unsigned long long>(profile->screenProjectRva));
    return true;
}

} // namespace StarfieldHT
