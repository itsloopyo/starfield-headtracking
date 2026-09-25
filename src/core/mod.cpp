#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "path_utils.h"
#include "hotkey_utils.h"
#include "legacy_config/legacy_config.h"
#include "hooks/hook_manager.h"
#include "hooks/camera_hook.h"
#include "hooks/input_hook.h"
#include "hooks/aim_hook.h"
#include "hooks/weapon_hook.h"
#include "game/build_profile.h"
#include "ui/stock_reticle.h"

namespace StarfieldHT {

namespace {

// Per-frame cache window - if GetProcessedRotation is called twice within this
// interval (e.g. when PlayerCamera::Update fires multiple times per frame for
// shadow/reflection cameras) the second call returns the cached result.
constexpr uint64_t kProcessCacheWindowMicros = 1000;

// Delta-time fallback and clamps. Clamps prevent huge dt after a load/stutter
// from slamming the smoothing filter, and tiny dt from numerical explosion.
constexpr float kDefaultDeltaTime = 0.016f;  // assume ~60Hz if no prior sample
constexpr float kMinDeltaTime     = 0.0001f;
constexpr float kMaxDeltaTime     = 0.1f;

// Diagnostic axis isolation, cycled by the dev-build F8 binding.
constexpr int kAxisIsolationOff       = 0;
constexpr int kAxisIsolationPitchOnly = 1;
constexpr int kAxisIsolationYawOnly   = 2;
constexpr int kAxisIsolationRollOnly  = 3;
constexpr int kAxisIsolationModes     = 4;

// A tracker-connection transition needs half a second of silence to happen at
// all, so the worst case is about a line a second; the cap bounds even that.
constexpr int kMaxConnectionLines = 40;

uint64_t GetTimeMicros() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // QuadPart * 1000000 overflows int64 after ~10 days of system uptime
    // (10 MHz QPC). Split into whole-second and remainder terms so the
    // product never exceeds 64 bits.
    const uint64_t q = static_cast<uint64_t>(now.QuadPart);
    const uint64_t f = static_cast<uint64_t>(freq.QuadPart);
    return (q / f) * 1000000ULL + ((q % f) * 1000000ULL) / f;
}

} // namespace

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Logger::Instance().Warning("Mod already initialized");
        return true;
    }

    Logger::Instance().Info("Starfield Head Tracking v%s initializing...", VERSION);

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    // An unrecognised build leaves the mod completely dormant. Only some of the
    // hooks are pinned to profile RVAs; the camera hook resolves PlayerCamera
    // by RTTI and would install on any build at all. Letting it install on its
    // own is worse than not loading: the head-tracked transform then stays in
    // the camera node all frame with no aim hook to hand the clean one back, so
    // the game's crosshair pick, projectile spawn and interaction ray follow
    // the head. ResolveBuildProfile has already logged which build was seen.
    if (ResolveBuildProfile() == nullptr) {
        Logger::Instance().Error(
            "Head tracking is off for this session. No hooks are installed and nothing in the "
            "game is modified, so it runs exactly as it would without the mod.");
        return false;
    }

    ApplyRotationSettings();
    ApplyPositionSettings();

    // False means the mod must not run, not that something is degraded.
    // InitializeHooks logs which of the two it was.
    if (!InitializeHooks()) return false;

    StartReceiver();

    const bool enabled = m_config.autoEnable;
    m_enabled.store(enabled);
    Logger::Instance().Info(enabled
        ? "Head tracking auto-enabled at startup"
        : "Head tracking disabled at startup (auto-enable is off)");

    m_initialized.store(true);
    AnnounceStartup();
    return true;
}

void Mod::ApplyRotationSettings() {
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    Logger::Instance().Info("TrackingProcessor initialized with sensitivity: yaw=%.2f pitch=%.2f roll=%.2f smoothing=local %.2f/remote %.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll,
                            m_config.localSmoothing, m_config.remoteSmoothing);

    m_worldSpaceYaw.store(m_config.worldSpaceYaw);
    Logger::Instance().Info("Yaw mode: %s", m_worldSpaceYaw.load() ? "horizon-locked (world)" : "camera-local");
}

void Mod::ApplyPositionSettings() {
    // Tracking mode seeds from the legacy positionEnabled config: true -> full
    // 6DOF, false -> rotation only. Position-only is reachable from either
    // start via the cycle hotkey.
    m_session.SetMode(m_config.positionEnabled
        ? cameraunlock::TrackingMode::RotationAndPosition
        : cameraunlock::TrackingMode::RotationOnly);

    const cameraunlock::PositionSettings posSettings = ToPositionSettings(m_config);
    m_session.GetPositionProcessor().SetSettings(posSettings);

    // Smoothing goes in after SetSettings, which would otherwise overwrite it.
    // The session feeds both the rotation and the position processor and picks
    // between the two values per connection from the receiver's
    // IsRemoteConnection(), re-read on every Update().
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);

    Logger::Instance().Info("Position processor initialized (%s, sens=%.1f/%.1f/%.1f, limits=%.2f/%.2f/%.2f)",
                            m_session.GetMode() == cameraunlock::TrackingMode::RotationAndPosition ? "6DOF" : "3DOF rotation",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z,
                            posSettings.limit_x, posSettings.limit_y, posSettings.limit_z);
}

void Mod::StartReceiver() {
    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });
    if (m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
        return;
    }
    // Not a word here about WHY the bind failed. The receiver has already
    // logged the reason the OS itself gave, and a port refuses a bind for
    // reasons other than another program holding it: a port inside a range
    // Windows has reserved answers WSAEACCES (10013) with nothing running
    // at all. Naming a cause here sent the user hunting an app that was
    // never there, over the top of a line that had already said otherwise.
    Logger::Instance().Warning("UDP port %d is not available yet - the bind error above says why. "
                               "The mod stays loaded and retries twice a second, so tracking starts "
                               "within about half a second of the port coming free, with no restart",
                               m_config.udpPort);
}

void Mod::AnnounceStartup() {
    Logger::Instance().Info("Initialization complete (camera:%s, input:%s)",
                            m_cameraHookInstalled ? "OK" : "FAILED",
                            m_inputHookInstalled ? "OK" : "FAILED");
    // Every binding, not just the toggle. The nav-cluster keys are unlabelled
    // in game and the log is the only place a user can read back what this
    // build is bound to.
    Logger::Instance().Info("Hotkeys: %s=toggle, %s=cycle tracking mode, %s=yaw mode",
                            VirtualKeyToString(m_config.toggleKey),
                            VirtualKeyToString(m_config.positionToggleKey),
                            VirtualKeyToString(m_config.yawModeKey));
}

bool Mod::LoadConfig() {
    std::string configPath = GetModulePath("HeadTracking.ini");
    if (configPath.empty()) {
        // Module directory lookup failed - refuse to fall back to a CWD-relative
        // config, since that would silently read/write the wrong file.
        Logger::Instance().Error("Could not resolve module directory for HeadTracking.ini - using built-in defaults");
        m_config.SetDefaults();
        return false;
    }

    legacy::Config read;
    const legacy::ReadStatus status = legacy::Read(configPath.c_str(), read);
    m_config = MapLegacyConfig(read);
    // Seed a config only when there is genuinely no file. An antivirus scanning a
    // freshly launched game directory or an editor holding the file open both
    // leave it unopenable, and writing defaults there destroys settings the user
    // tuned.
    if (status == legacy::ReadStatus::Absent) {
        m_config.Save(configPath.c_str());
        return false;
    }
    if (status == legacy::ReadStatus::OpenFailed) {
        Logger::Instance().Warning(
            "HeadTracking.ini exists but could not be read (error %lu) - running on defaults "
            "for this session and leaving the file alone", GetLastError());
        return false;
    }

    return true;
}

// False means the mod must not run at all. Only two things reach that: MinHook
// failing outright, and the aim hook failing, because head tracking without aim
// decoupling points the game's own aim wherever the head is looking. Everything
// else here degrades what is drawn and logs a line.
bool Mod::InitializeHooks() {
    if (!HookManager::Instance().Initialize()) {
        Logger::Instance().Error("MinHook initialization failed");
        return false;
    }

    if (!InstallCameraHook()) {
        Logger::Instance().Warning("Camera hook failed - head tracking disabled");
        m_cameraHookInstalled = false;
    } else {
        m_cameraHookInstalled = true;
        Logger::Instance().Info("Camera hook installed");
    }

    // The one hook whose absence changes what the GAME does rather than what the
    // player sees. Without it the head-tracked transform stays in the camera node
    // all frame and the crosshair pick, projectile spawn and interaction ray all
    // follow the head, so the mod is refused rather than run in that state - the
    // same reason an unrecognised build is refused outright. The profile has
    // already matched by this point, so the only way here is MinHook failing to
    // patch the prologue.
    if (!InstallAimHook()) {
        Logger::Instance().Error(
            "Aim decoupling could not be installed, so head tracking is off for this session. "
            "Running without it would point the game's own aim wherever the head is looking.");
        return false;
    }

    if (!InstallWeaponHook()) {
        Logger::Instance().Error("Camera submission hooks are required for ship aim; head tracking is off");
        return false;
    }
    if (!InstallStockReticleHook()) {
        Logger::Instance().Error("Stock reticle positioning is unavailable");
    }
    if (!InstallShipReticleHook()) {
        Logger::Instance().Error("Ship aim UI positioning is unavailable");
    }

    // MH_EnableHook(MH_ALL_HOOKS) reports the first failure and may have enabled
    // some of the rest, and "some" could be the camera hook without the aim hook,
    // which is the head-coupled aim this whole path exists to avoid. Switch them
    // all back off and refuse. Disabling is not the unsafe half of teardown: the
    // trampolines stay allocated and every detour keeps a valid original to call,
    // so a thread already inside one simply finishes.
    if (!HookManager::Instance().EnableAllHooks()) {
        MH_DisableHook(MH_ALL_HOOKS);
        Logger::Instance().Error(
            "Some hooks could not be enabled, so head tracking is off for this session. "
            "Everything installed has been switched back off.");
        return false;
    }

    // Last, and only once nothing above can still refuse. It starts a polling
    // thread rather than patching anything, and every teardown path was deleted
    // with the rest of the shutdown code, so a thread started before a refusal
    // would outlive it: the log would say the mod is off for the session and the
    // hotkeys would go on answering.
    //
    // Losing the hotkeys is worth saying and not worth refusing to run over:
    // tracking still works and the config still decides what it starts as.
    if (!InstallInputHook()) {
        m_inputHookInstalled = false;
        Logger::Instance().Warning(
            "Input hook failed - no hotkeys this session, so tracking runs on whatever "
            "HeadTracking.ini starts it with");
    } else {
        m_inputHookInstalled = true;
        Logger::Instance().Info("Input hook installed");
    }
    return true;
}

void Mod::SetEnabled(bool enabled) {
    const bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled == enabled) return;

    Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::DumpMatrices() {
    CameraFrame frame;
    if (!GetCameraFrame(frame)) {
        Logger::Instance().Info("DumpMatrices: no tracked frame available");
        return;
    }
    auto& L = Logger::Instance();
    L.Info("=== CAMERA DUMP ===");
    L.Info("clean fwd %+.4f %+.4f %+.4f  up %+.4f %+.4f %+.4f  right %+.4f %+.4f %+.4f",
           frame.clean.f[0], frame.clean.f[1], frame.clean.f[2],
           frame.clean.u[0], frame.clean.u[1], frame.clean.u[2],
           frame.clean.r[0], frame.clean.r[1], frame.clean.r[2]);
    L.Info("clean eye %+.3f %+.3f %+.3f", frame.clean.e[0], frame.clean.e[1], frame.clean.e[2]);
    L.Info("drawn fwd %+.4f %+.4f %+.4f  up %+.4f %+.4f %+.4f  right %+.4f %+.4f %+.4f",
           frame.drawn.f[0], frame.drawn.f[1], frame.drawn.f[2],
           frame.drawn.u[0], frame.drawn.u[1], frame.drawn.u[2],
           frame.drawn.r[0], frame.drawn.r[1], frame.drawn.r[2]);
    L.Info("drawn eye %+.3f %+.3f %+.3f (lean %+.3f %+.3f %+.3f units)",
           frame.drawn.e[0], frame.drawn.e[1], frame.drawn.e[2],
           frame.drawn.e[0] - frame.clean.e[0],
           frame.drawn.e[1] - frame.clean.e[1],
           frame.drawn.e[2] - frame.clean.e[2]);
    float y, p, r;
    if (GetProcessedRotation(y, p, r)) {
        L.Info("tracker yaw=%+.3f pitch=%+.3f roll=%+.3f (deg)", y, p, r);
    }
    float px, py, pz;
    if (GetPositionOffset(px, py, pz)) {
        L.Info("tracker position x=%+.4f y=%+.4f z=%+.4f (m)", px, py, pz);
    }
    L.Info("frustum right=%.4f top=%.4f near=%.3f", frame.frustumRight, frame.frustumTop, frame.frustumNear);
    L.Info("YawMode: %s", IsWorldSpaceYaw() ? "WORLD" : "LOCAL");
    L.Info("=== END CAMERA DUMP ===");
}

void Mod::AnnounceMode(const char* label, const char* value) {
    Logger::Instance().Info("%s: %s", label, value);
}

void Mod::CycleDofMode() {
    const cameraunlock::TrackingMode mode = m_session.CycleMode();
    AnnounceMode("Mode",
        mode == cameraunlock::TrackingMode::RotationAndPosition ? "6DOF (rotation + position)" :
        mode == cameraunlock::TrackingMode::RotationOnly ? "3DOF rotation only" :
        "3DOF position only");
}

void Mod::CycleAxisIsolation() {
    const int next = (m_axisIsolation.load() + 1) % kAxisIsolationModes;
    m_axisIsolation.store(next);
    AnnounceMode("Axis isolation",
        next == kAxisIsolationOff       ? "normal" :
        next == kAxisIsolationPitchOnly ? "PITCH only" :
        next == kAxisIsolationYawOnly   ? "YAW only" :
                                          "ROLL only");
}

void Mod::ToggleYawMode() {
    const bool worldSpace = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(worldSpace);
    AnnounceMode("Yaw mode", worldSpace ? "horizon-locked (world)" : "camera-local");
}

void Mod::LogTrackerConnection() {
    if (!m_loggedFirstSample) {
        // The RAW receiver sample, not the session output: centering, deadzone,
        // smoothing and sensitivity can all render the processed value 0/0/0 on the
        // settle frame, which reads as a tracker sending zeros.
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        if (!m_udpReceiver.GetRotation(yaw, pitch, roll)) return;

        m_loggedFirstSample = true;
        m_trackerReceiving = true;
        Logger::Instance().Info("First tracker sample received: raw yaw=%.2f pitch=%.2f roll=%.2f (connection is %s)",
                                yaw, pitch, roll,
                                m_udpReceiver.IsRemoteConnection() ? "remote" : "local");
        return;
    }

    // "The view stopped following my head" is the report this answers, and it
    // cannot be answered without knowing whether packets were still arriving.
    const bool receiving = m_udpReceiver.IsReceiving();
    if (receiving == m_trackerReceiving) return;
    m_trackerReceiving = receiving;
    if (m_connectionLines >= kMaxConnectionLines) return;
    ++m_connectionLines;
    Logger::Instance().Info(receiving
        ? "Tracker packets resumed"
        : "Tracker stopped sending - no packet for over half a second");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    // Run the shared pipeline at most once per cache window. PlayerCamera::Update
    // can fire multiple times per frame (shadow/reflection cameras); calls inside
    // the window read the session's cached outputs.
    const uint64_t now = GetTimeMicros();
    if (m_lastProcessTime == 0 || (now - m_lastProcessTime) >= kProcessCacheWindowMicros) {
        float deltaTime = kDefaultDeltaTime;
        if (m_lastProcessTime > 0) {
            deltaTime = (now - m_lastProcessTime) / 1000000.0f;
            if (deltaTime > kMaxDeltaTime) deltaTime = kMaxDeltaTime;
            if (deltaTime < kMinDeltaTime) deltaTime = kMinDeltaTime;
        }
        m_lastProcessTime = now;
        m_session.Update(deltaTime);
    }

    if (!m_session.GetRotation(yaw, pitch, roll)) {
        return false;
    }

    switch (m_axisIsolation.load(std::memory_order_relaxed)) {
        case kAxisIsolationPitchOnly: yaw = 0.0f;   roll = 0.0f;  break;
        case kAxisIsolationYawOnly:   pitch = 0.0f; roll = 0.0f;  break;
        case kAxisIsolationRollOnly:  yaw = 0.0f;   pitch = 0.0f; break;
        default: break;
    }

    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

} // namespace StarfieldHT
