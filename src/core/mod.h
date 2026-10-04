#pragma once

#include "config.h"

#include <cameraunlock/ads/aim_mode.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

#include <functional>
#include <optional>

namespace StarfieldHT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void CycleDofMode();
    void ToggleYawMode();
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    // Sights locked draws the weapon from the clean eye, so a lean never takes the eye off the
    // sights. The two free look modes draw it from the tracked eye, so the weapon stays put in
    // the world and the head moves around it; free look with a marker also draws the aim marker
    // while the sights are up. Stock sights eases yaw, pitch and the lean out while the sights
    // are up and draws the weapon as sights locked does.
    void CycleAimMode();
    cameraunlock::ads::AimMode GetAimMode() const { return m_aimMode.load(std::memory_order_relaxed); }
    bool IsFreeLook() const { return cameraunlock::ads::IsFreeLook(GetAimMode()); }

    // F8 cycles axis isolation for diagnostic testing.
    // 0 = normal, 1 = pitch-only, 2 = yaw-only, 3 = roll-only.
    void CycleAxisIsolation();

    void DumpMatrices();

    const Config& GetConfig() const { return m_config; }

    // Get processed (smoothed) rotation values for rendering
    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);

    // Latches the first tracker packet, then reports the connection dropping
    // and coming back. Called from an ungated point in the camera hook: the
    // answer to "did the tracker ever send anything, and was it still sending"
    // must not depend on tracking being enabled or the player being in gameplay.
    void LogTrackerConnection();

    // Get processed position offset (meters)
    bool GetPositionOffset(float& x, float& y, float& z);

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    void LoadConfig();
    void ApplyRotationSettings();
    void ApplyPositionSettings();
    void StartReceiver();
    void AnnounceStartup();
    bool InitializeHooks();

    static void AnnounceMode(const char* label, const char* value);

    // Writes a toggle's new state to CameraUnlock.ini, after the toggle has applied it.
    void SaveToggle(const std::function<void(Config&)>& change);

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    // The one reader and writer of CameraUnlock.ini. Empty only when the mod's own folder
    // could not be resolved, and then nothing is saved this session. The hotkey thread saves
    // through it after LoadConfig has built it on the init thread.
    std::optional<cameraunlock::config::ConfigOwner<Config>> m_configOwner;
    cameraunlock::UdpReceiver m_udpReceiver;
    // Shared per-frame pipeline (interpolation, processing, 6DOF, mode cycling).
    // Updated at most once per cache window from GetProcessedRotation;
    // hotkey-thread calls (CycleDofMode) follow the session's documented
    // threading model.
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    // Yaw mode: true = horizon-locked (world), false = camera-local
    std::atomic<bool> m_worldSpaceYaw{true};

    std::atomic<cameraunlock::ads::AimMode> m_aimMode{cameraunlock::ads::AimMode::SightsLocked};

    // Axis isolation for diagnostic testing (0=normal, 1=pitch, 2=yaw, 3=roll)
    std::atomic<int> m_axisIsolation{0};

    // Timing for frame-rate independent processing
    uint64_t m_lastProcessTime = 0;

    bool m_loggedFirstSample = false;
    bool m_trackerReceiving = false;
    int  m_connectionLines = 0;

    bool m_cameraHookInstalled = false;
    bool m_inputHookInstalled = false;
};

} // namespace StarfieldHT
