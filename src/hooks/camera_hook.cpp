#include "pch.h"
#include "camera_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/rtti_utils.h"
#include "game/camera_math.h"
#include "game/ads_state.h"
#include "game/base_fov.h"
#include "game/build_profile.h"
#include "game/aim_projection.h"
#include "game/game_state.h"
#include "game/scene_layout.h"
#include "game/starfield_types.h"
#include "camera_boundary.h"
#include "ui/reticle.h"

#include <cameraunlock/camera/zoom_compensation.h>
#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeRead;
using cameraunlock::memory::SafeWrite;

// TESCamera::Update takes no arguments. It is slot 3 of the TESCamera vtable,
// which PlayerCamera does not override: the body clamps the float pairs at
// +0x30/+0x3c and +0x38/+0x40 and then dispatches to the current camera state.
// Slots 1, 2 and 4 are the camera-root setter, an enable flag and the state
// setter, which is why hooking slot 2 (Update in the older Creation Engine
// games) never fired.
typedef void (__fastcall *CameraUpdate_t)(void* thisCamera);
CameraUpdate_t g_originalUpdate = nullptr;

constexpr int kTesCameraUpdateSlot = 3;
constexpr size_t kPointerSize = sizeof(uintptr_t);

// TESCamera::currentState. Update loads it from this+0x10 and dispatches the
// per-frame work through it.
constexpr uintptr_t kCameraStateOffset = 0x10;

// Diagnostic budgets. The survey is 17 lines a call and the apply line is one a
// second, so both are bounded rather than left to run for the session.
constexpr int      kMaxSurveys           = 2;
constexpr uint64_t kSurveyIntervalMs     = 5000;
constexpr int      kMaxApplyLines        = 400;
constexpr uint64_t kApplyLogIntervalMs   = 1000;

// Below this the clean forward has rolled onto or behind the drawn view plane
// and the aim has no screen position at all.
constexpr float kMinAimDepth = 0.01f;

uintptr_t g_playerCameraVtable = 0;
uintptr_t g_playerAddress = 0;
uintptr_t g_aimPointOffset = 0;
using RelativeAimPoint = float* (*)(float*, const float*, const float*);
RelativeAimPoint g_relativeAimPoint = nullptr;
std::atomic<uintptr_t> g_liveCamera{0};

CameraFrameHistory g_frames;

bool SameMatrix(const NiMatrix44& a, const NiMatrix44& b) {
    return std::memcmp(&a, &b, sizeof(NiMatrix44)) == 0;
}

// Head tracking is written into the camera node's LOCAL transform. The scene
// graph rebuilds every world transform from local * parentWorld after this hook
// runs, so a world transform written here is discarded, and the clip matrix, the
// culling frustum and the shadow passes all come out of that rebuild - which is
// exactly why driving the local transform is what reaches the picture. Two
// tighter injection points were measured and rejected: writing the world
// transform or the clip matrix here is overwritten by that rebuild, and
// restoring from the swap chain's Present runs a frame behind on its own thread.
//
// Keeping the pristine transform beside the one we wrote is what stops a
// session's worth of frames compounding one rotation on top of the last.
struct PristineLocal {
    NiMatrix44 written{};
    NiMatrix44 pristine{};
    bool       have = false;

    // True when what the node still holds is the transform we wrote last frame,
    // which means `pristine` remains the game's own and carries forward.
    // Anything else is a fresh transform from the game and becomes the pristine.
    bool Observe(const NiMatrix44& stored) {
        const bool ourWriteStood = have && SameMatrix(stored, written);
        if (!ourWriteStood) {
            pristine = stored;
            have = true;
        }
        return ourWriteStood;
    }
};

PristineLocal g_local;

// The world transform the game itself built this frame, published for the aim
// hook to hand back to the game once the renderer has taken what it needs. The
// reader runs on the render thread while this is written on the simulation
// thread, and half of one frame's transform beside half of the next would point
// the camera nowhere in particular - hence the seqlock rather than a plain copy.
class CleanWorldSlot {
public:
    void Publish(uintptr_t camera, uintptr_t offset, const NiMatrix44& world) {
        // The odd marker has to become visible BEFORE the payload it protects,
        // and a release store only orders what precedes it. The fence is what
        // stops the three writes below being hoisted above it and handing a
        // reader a torn transform under an even sequence.
        seq_.store(++writeSeq_, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        camera_ = camera;
        offset_ = offset;
        world_ = world;
        seq_.store(++writeSeq_, std::memory_order_release);
    }

    // True when there is a transform to restore. `token` advances to whatever
    // complete publication was read, INCLUDING the empty ones published while
    // tracking contributes nothing: without that the caller re-copies the
    // transform on every one of its thousands of calls a frame for as long as
    // the mod is switched off or the player is in a menu.
    bool Read(uint32_t& token, uintptr_t& outCamera, uintptr_t& outOffset,
              NiMatrix44& outWorld) const {
        for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
            const uint32_t seq = seq_.load(std::memory_order_acquire);
            if (seq == 0 || (seq & 1u) || seq == token) return false;
            const uintptr_t camera = camera_;
            const uintptr_t offset = offset_;
            const NiMatrix44 world = world_;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) != seq) continue;
            token = seq;
            if (camera == 0) return false;
            outCamera = camera;
            outOffset = offset;
            outWorld = world;
            return true;
        }
        return false;
    }

private:
    static constexpr int kMaxReadAttempts = 8;

    NiMatrix44 world_{};
    uintptr_t  camera_ = 0;
    uintptr_t  offset_ = 0;
    // Odd while a write is in flight; only the writer touches writeSeq_.
    std::atomic<uint32_t> seq_{0};
    uint32_t writeSeq_ = 0;
};

CleanWorldSlot g_cleanWorld;

void ReportException(DWORD code) {
    static std::atomic<uint64_t> s_count{0};
    static std::atomic<DWORD> s_lastCode{0};
    const uint64_t n = s_count.fetch_add(1, std::memory_order_relaxed) + 1;
    const DWORD prev = s_lastCode.exchange(code, std::memory_order_relaxed);
    const bool isPow2 = (n & (n - 1)) == 0;
    if (n == 1 || isPow2 || prev != code) {
        Logger::Instance().Warning(
            "Exception in camera hook (code=0x%08X, total=%llu) - skipping frame",
            code, static_cast<unsigned long long>(n));
    }
}

std::atomic<int>      g_surveysLogged{0};
std::atomic<uint64_t> g_lastSurveyMs{0};

void MaybeLogSurvey(uintptr_t cameraRoot, uintptr_t niCamera) {
    if (g_surveysLogged.load(std::memory_order_relaxed) >= kMaxSurveys) return;
    const uint64_t now = GetTickCount64();
    if (now - g_lastSurveyMs.load(std::memory_order_relaxed) < kSurveyIntervalMs) return;
    g_lastSurveyMs.store(now, std::memory_order_relaxed);
    g_surveysLogged.fetch_add(1, std::memory_order_relaxed);
    LogCameraSurvey(cameraRoot, niCamera);
}

void PublishCameraFrame(uintptr_t niCamera, const CameraBasis& clean, const CameraBasis& drawn,
                        const NiFrustum& frustum, const NiMatrix44& local) {
    CameraFrame f{};
    f.niCamera = niCamera;
    f.clean = clean;
    f.drawn = drawn;
    f.frustumRight = frustum.right;
    f.frustumTop = frustum.top;
    f.frustumNear = frustum.nearPlane;
    f.local = local;
    g_frames.Publish(f);
}

// Publishing an empty frame is how the render-side consumers are told there is
// nothing tracked to correct for this frame.
void PublishNoFrame() {
    g_frames.Publish(CameraFrame{});
}

// A frame the hook could not service. Everything downstream is told so, the
// mark included: it is drawn from the last frame it was given, so leaving it
// alone parks it on screen pointing at nothing.
void HideFrame() {
    PublishNoFrame();
    UpdateReticle();
}

// The same, for a frame abandoned BEFORE the clean world transform was
// published. That publication is retired too: left standing, a render worker
// that has not consumed the previous token yet would restore last frame's clean
// transform onto a node this frame did not track. It must not be used after the
// publication, where retiring it would throw away a restore that is correct and
// hand the game a head-tracked transform to aim through.
void AbandonFrame() {
    g_cleanWorld.Publish(0, 0, NiMatrix44{});
    HideFrame();
}

// Whether the head pose reaches the picture, and by how much, is not something
// the screen can be read for: a mod that computes the right rotation and writes
// it somewhere the renderer ignores looks exactly like one that is switched off.
void LogApplyState(bool active, bool haveRotation, bool havePosition,
                   float yaw, float pitch, float roll,
                   const CameraBasis& clean, const CameraBasis& drawn,
                   const NiFrustum& frustum, float zoom) {
    static uint64_t s_lastMs = 0;
    static int s_lines = 0;
    if (s_lines >= kMaxApplyLines) return;
    const uint64_t now = GetTickCount64();
    if (now - s_lastMs < kApplyLogIntervalMs) return;
    s_lastMs = now;
    ++s_lines;
    const float cosAngle = Dot3(clean.f, drawn.f);
    Logger::Instance().Info(
        "apply: active=%d rot=%d pos=%d pose(%+.2f,%+.2f,%+.2f) turned %.2f deg "
        "lean(%+.2f,%+.2f,%+.2f) cleanFwd(%+.3f,%+.3f,%+.3f) drawnFwd(%+.3f,%+.3f,%+.3f) "
        "frustum r=%.4f t=%.4f zoom %.4f",
        active, haveRotation, havePosition, yaw, pitch, roll,
        acosf(cosAngle > 1.0f ? 1.0f : (cosAngle < -1.0f ? -1.0f : cosAngle)) * RAD_TO_DEG,
        drawn.e[0] - clean.e[0], drawn.e[1] - clean.e[1], drawn.e[2] - clean.e[2],
        clean.f[0], clean.f[1], clean.f[2], drawn.f[0], drawn.f[1], drawn.f[2],
        frustum.right, frustum.top, zoom);
}

// The three pieces of the camera node this hook works from, read in one go so a
// half-read frame is skipped rather than half-applied.
struct CameraReadout {
    NiMatrix44 storedLocal;
    NiMatrix44 rootWorld;
    NiFrustum  frustum;
};

bool ReadCamera(uintptr_t cameraRoot, uintptr_t niCamera, const SceneLayout& layout,
                CameraReadout& out) {
    return SafeRead(niCamera + layout.localTransformOffset, out.storedLocal)
        && SafeRead(cameraRoot + layout.worldTransformOffset, out.rootWorld)
        && SafeRead(niCamera + layout.frustumOffset, out.frustum);
}

// The clean camera, rebuilt the way the scene graph rebuilds it:
// world = local * parentWorld. Reading the stored world transform instead would
// hand back last frame's tracked camera as this frame's clean one.
CameraBasis BuildCleanBasis(const NiMatrix44& pristineLocal, const NiMatrix44& rootWorld,
                            const Mat3& rootRot) {
    float cleanEye[3] = {};
    MulRowVec(&pristineLocal.entry[3][0], rootRot, cleanEye);
    for (int i = 0; i < 3; ++i) cleanEye[i] += rootWorld.entry[3][i];

    CameraBasis basis{};
    BasisFromRotation(Mul(RotationOf(pristineLocal), rootRot), cleanEye, basis);
    return basis;
}

// The head pose as it will be applied to this frame: already scaled for
// whatever the game has done to the field of view, and already gated by what
// the sights mode allows.
struct HeadPose {
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool  haveRotation = false;
    bool  havePosition = false;
    float zoom = 1.0f;
};

HeadPose SampleHeadPose(Mod& mod, bool active, const NiFrustum& frustum) {
    HeadPose pose;
    pose.haveRotation = active && mod.GetProcessedRotation(pose.yaw, pose.pitch, pose.roll);
    pose.havePosition = active && mod.GetPositionOffset(pose.x, pose.y, pose.z);

    // A narrow field of view magnifies everything in the frame, head tracking
    // included: the head turns ten degrees, the camera turns ten degrees, and
    // the picture moves further by the ratio between the two fields of view.
    // Without this the mod's sensitivity appears to jump the moment the sights
    // come up. Yaw, pitch and the lean all translate the picture and scale with
    // it; roll turns the picture about the view axis by the same angle at every
    // field of view there is, so it does not.
    pose.zoom = PoseZoomFactor(frustum.right, frustum.top);
    if (pose.zoom != 1.0f) {
        pose.yaw = cameraunlock::camera::ScaleAngleForZoom(pose.yaw, pose.zoom);
        pose.pitch = cameraunlock::camera::ScaleAngleForZoom(pose.pitch, pose.zoom);
        pose.x *= pose.zoom;
        pose.y *= pose.zoom;
        pose.z *= pose.zoom;
    }

    // Roll turns the picture about the view axis and leaves the aim on the
    // centre of it. Yaw, pitch and a lean all move the aim off centre, so
    // dropping them is what makes the sight picture the stock one.
    if (mod.GetEffectiveAdsMode() == AdsMode::StockRollOnly) {
        pose.yaw = 0.0f;
        pose.pitch = 0.0f;
        pose.havePosition = false;
    }
    return pose;
}

// Back out of world space into the parent's frame, which is what the node
// stores: world = local * parentWorld, so local = world * parentWorld^-1, and a
// rotation's inverse is its transpose.
NiMatrix44 BuildTrackedLocal(const NiMatrix44& pristineLocal, const Mat3& rootRotInv,
                             const CameraBasis& drawn, const float leanWorld[3],
                             bool havePosition) {
    const Mat3 newLocalRot = Mul(RotationOfBasis(drawn), rootRotInv);
    NiMatrix44 local = pristineLocal;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) local.entry[i][j] = newLocalRot.m[i][j];
    }
    if (havePosition) {
        float leanLocal[3] = {};
        MulRowVec(leanWorld, rootRotInv, leanLocal);
        for (int i = 0; i < 3; ++i) {
            local.entry[3][i] = pristineLocal.entry[3][i] + leanLocal[i];
        }
    }
    return local;
}

// Hands the node its own transform back and tells every render-side consumer
// there is nothing to correct. Called on the frames tracking contributes
// nothing to, which is every frame while the mod is off or the player is in a
// menu.
void ReleaseTracking(uintptr_t niCamera, uintptr_t localOffset, const NiMatrix44& pristineLocal,
                     bool ourWriteStood) {
    if (ourWriteStood) {
        SafeWrite(niCamera + localOffset, pristineLocal);
    }
    g_local.have = false;
    g_cleanWorld.Publish(0, 0, NiMatrix44{});
    PublishNoFrame();
    UpdateReticle();
}

void ApplyTracking(uintptr_t cameraRoot, uintptr_t niCamera) {
    const SceneLayout& layout = GetSceneLayout();
    CameraReadout readout{};
    if (!ReadCamera(cameraRoot, niCamera, layout, readout)) {
        // Without this the mark keeps being drawn at the screen position of
        // the last frame that did read, welded there while the world moves
        // under it, for as long as the node cannot be read.
        AbandonFrame();
        return;
    }

    const bool ourWriteStood = g_local.Observe(readout.storedLocal);
    const NiMatrix44 pristine = g_local.pristine;

    const Mat3 rootRot = RotationOf(readout.rootWorld);
    const CameraBasis cleanBasis = BuildCleanBasis(pristine, readout.rootWorld, rootRot);

    AdsState::Update();

    Mod& mod = Mod::Instance();
    const bool active = mod.IsEnabled() && GameState::IsInGameplay();
    const HeadPose pose = SampleHeadPose(mod, active, readout.frustum);

    if (!pose.haveRotation && !pose.havePosition) {
        ReleaseTracking(niCamera, layout.localTransformOffset, pristine, ourWriteStood);
        return;
    }

    CameraBasis drawn = cleanBasis;
    if (pose.haveRotation) {
        ApplyHeadRotationToBasis(drawn, pose.yaw, pose.pitch, pose.roll, mod.IsWorldSpaceYaw());
    }
    float leanWorld[3] = {0.0f, 0.0f, 0.0f};
    if (pose.havePosition) {
        const NiPoint3 lean = CameraLocalLeanOffset(pose.x, pose.y, pose.z);
        // Tied to the same switch as yaw. Horizon-locking is right wherever the
        // player is a person standing on the ground, and wrong in the same
        // places yaw is: a camera that banks and rolls has no horizon worth
        // locking either axis to.
        const CameraBasis leanBasis = mod.IsWorldSpaceYaw()
            ? HorizonLockedBasis(cleanBasis)
            : cleanBasis;
        for (int i = 0; i < 3; ++i) {
            leanWorld[i] = lean.x * leanBasis.f[i]
                         + lean.y * leanBasis.u[i]
                         + lean.z * leanBasis.r[i];
            drawn.e[i] = cleanBasis.e[i] + leanWorld[i];
        }
    }

    const NiMatrix44 newLocal = BuildTrackedLocal(pristine, Transpose(rootRot), drawn,
                                                  leanWorld, pose.havePosition);

    // Publish the matching clean basis before the renderer can see the pose.
    PublishCameraFrame(niCamera, cleanBasis, drawn, readout.frustum, newLocal);
    if (!SafeWrite(niCamera + layout.localTransformOffset, newLocal)) {
        AbandonFrame();
        return;
    }
    g_local.written = newLocal;

    // Nothing is written to the node's world transform or its clip matrix here:
    // the rebuild described above discards both, and that address is also the
    // one the aim hook restores from render worker threads, so writing it would
    // race a torn matrix into the transform the game aims through.
    //
    // The clean world transform for the aim hook to restore once the renderer
    // has built its matrices from the tracked one.
    NiMatrix44 cleanWorld{};
    WriteBasis(cleanBasis, cleanWorld);
    g_cleanWorld.Publish(niCamera, layout.worldTransformOffset, cleanWorld);

    LogApplyState(active, pose.haveRotation, pose.havePosition, pose.yaw, pose.pitch, pose.roll,
                  cleanBasis, drawn, readout.frustum, pose.zoom);
    UpdateReticle();
}

// The structured-exception frame is kept in a function of its own so the work
// above can use ordinary C++ objects: MSVC refuses __try in any function that
// needs unwinding.
//
// The filter takes access violations only. Reading a scene-graph node the
// engine freed underneath us is ours to absorb; a stack overflow, a breakpoint
// or a C++ exception travelling through is not, and swallowing one of those
// here would turn a diagnosable fault into a session of skipped frames.
void ApplyTrackingGuarded(uintptr_t cameraRoot, uintptr_t niCamera) {
    __try {
        ApplyTracking(cameraRoot, niCamera);
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        ReportException(GetExceptionCode());
        // HideFrame, not AbandonFrame: the fault may have landed after the clean
        // world transform was published, and that publication is this frame's
        // and correct.
        HideFrame();
    }
}

void __fastcall CameraUpdateHook(void* thisCamera) {
    g_originalUpdate(thisCamera);

    // TESCamera::Update is shared with every other camera derived from it, so
    // only the object carrying the PlayerCamera vtable gets this far.
    if (!thisCamera || *reinterpret_cast<uintptr_t*>(thisCamera) != g_playerCameraVtable) return;

    Mod::Instance().LogTrackerConnection();

    uintptr_t state = 0;
    if (SafeRead(reinterpret_cast<uintptr_t>(thisCamera) + kCameraStateOffset, state)) {
        GameState::SetCameraState(state);
    }

    if (!ResolveSceneLayout(thisCamera)) return;

    uintptr_t cameraRoot = 0, niCamera = 0;
    if (!GetSceneGraph(thisCamera, cameraRoot, niCamera)) return;
    g_liveCamera.store(niCamera, std::memory_order_release);

    MaybeLogSurvey(cameraRoot, niCamera);
    ApplyTrackingGuarded(cameraRoot, niCamera);
}

} // namespace

bool GetCameraFrame(CameraFrame& out) {
    return g_frames.Latest(out);
}

bool FindLocalCameraFrame(uintptr_t camera, const NiMatrix44& local, CameraFrame& out) {
    return g_frames.FindLocal(camera, local, out);
}

bool ProjectAimDirection(const CameraFrame& frame, float& outNdcX, float& outNdcY) {
    if (frame.niCamera == 0 || frame.frustumRight <= 0.0f || frame.frustumTop <= 0.0f) return false;
    const float depth = Dot3(frame.clean.f, frame.drawn.f);
    if (depth < kMinAimDepth) return false;
    outNdcX = Dot3(frame.clean.f, frame.drawn.r) / depth / frame.frustumRight;
    outNdcY = Dot3(frame.clean.f, frame.drawn.u) / depth / frame.frustumTop;
    return true;
}

bool ProjectPlayerAim(const CameraFrame& frame, float& outNdcX, float& outNdcY) {
    uintptr_t player = 0;
    float target[4];
    if (!SafeRead(g_playerAddress, player) || !player || !SafeRead(player + g_aimPointOffset, target)) {
        Logger::Instance().Error("Cannot read the player's shooting aim point");
        return false;
    }
    float relative[3];
    // The fourth word identifies the floating origin. Use the same subtraction
    // as projectile launch so crossing an origin boundary does not move the mark.
    g_relativeAimPoint(relative, target, frame.clean.e);
    const float distance = Dot3(relative, frame.clean.f);
    return ProjectAimAtDistance(frame, distance, outNdcX, outNdcY);
}

uintptr_t GetLiveCameraAddress() {
    return g_liveCamera.load(std::memory_order_acquire);
}

bool GetCleanWorldTransform(uint32_t& token, uintptr_t& outCamera, uintptr_t& outOffset,
                            NiMatrix44& outWorld) {
    return g_cleanWorld.Read(token, outCamera, outOffset, outWorld);
}

bool InstallCameraHook() {
    Logger::Instance().Info("Installing camera hook...");

    GameState::Initialize();

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) {
        Logger::Instance().Error("Failed to get game module handle");
        return false;
    }

    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) {
        Logger::Instance().Error("GetModuleRange failed: %lu", GetLastError());
        return false;
    }

    const auto* profile = ResolveBuildProfile();
    if (!profile) return false;
    if (profile->relativeAimPointRva >= moduleSize || profile->playerSingletonRva >= moduleSize) {
        Logger::Instance().Error("Aim point profile RVA is outside the game module");
        return false;
    }
    g_playerAddress = moduleBase + profile->playerSingletonRva;
    g_aimPointOffset = profile->playerAimPointOffset;
    g_relativeAimPoint = reinterpret_cast<RelativeAimPoint>(moduleBase + profile->relativeAimPointRva);

    const uintptr_t vtable = FindVtableByRTTI(moduleBase, moduleSize, ".?AVPlayerCamera@@");
    if (vtable == 0) {
        Logger::Instance().Error("Could not find PlayerCamera vtable via RTTI");
        return false;
    }
    g_playerCameraVtable = vtable;

    // The vtable came out of a scan of the image, so the slot itself is read
    // through SafeRead: a match found in the last few bytes of the module would
    // otherwise have this read past the end of it.
    uintptr_t updateFunc = 0;
    if (!SafeRead(vtable + kTesCameraUpdateSlot * kPointerSize, updateFunc)) {
        Logger::Instance().Error("Could not read slot %d of the PlayerCamera vtable at 0x%llX",
                                 kTesCameraUpdateSlot, static_cast<unsigned long long>(vtable));
        return false;
    }
    if (updateFunc == 0 || updateFunc < moduleBase || updateFunc >= moduleBase + moduleSize) {
        Logger::Instance().Error("Invalid Update function pointer: 0x%llX", updateFunc);
        return false;
    }

    Logger::Instance().Info("TESCamera::Update at: 0x%llX (RVA 0x%llX)",
                            updateFunc, updateFunc - moduleBase);

    const MH_STATUS status = MH_CreateHook(
        reinterpret_cast<void*>(updateFunc),
        reinterpret_cast<LPVOID>(&CameraUpdateHook),
        reinterpret_cast<LPVOID*>(&g_originalUpdate));

    if (status != MH_OK) {
        Logger::Instance().Error("MH_CreateHook failed: %d", static_cast<int>(status));
        return false;
    }

    Logger::Instance().Info("Camera hook installed successfully");
    return true;
}

} // namespace StarfieldHT
