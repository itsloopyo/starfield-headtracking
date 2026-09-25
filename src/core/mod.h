#pragma once

#include "config.h"

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

    // Writes a toggle's new state to HeadTracking.ini, after the toggle has applied it.
    void SaveToggle(const std::function<void(Config&)>& change);

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    // The one reader and writer of HeadTracking.ini. Empty only when the mod's own folder
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
