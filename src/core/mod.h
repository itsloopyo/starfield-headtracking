#pragma once

#include "config.h"
#include "game/ads_state.h"

#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

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
    void CycleAdsMode();
    AdsMode GetAdsMode() const { return m_adsMode.load(); }

    // Hip fire is fully tracked in every mode.
    AdsMode GetEffectiveAdsMode() const;
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    // F8 cycles axis isolation for diagnostic testing.
    // 0 = normal, 1 = pitch-only, 2 = yaw-only, 3 = roll-only.
    void CycleAxisIsolation();

    void DumpMatrices();

    Config& GetConfig() { return m_config; }
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

    bool LoadConfig();
    void ApplyRotationSettings();
    void ApplyPositionSettings();
    void StartReceiver();
    void AnnounceStartup();
    bool InitializeHooks();

    static void AnnounceMode(const char* label, const char* value);

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    // Shared per-frame pipeline (interpolation, processing, 6DOF, mode cycling).
    // Updated at most once per cache window from GetProcessedRotation;
    // hotkey-thread calls (CycleDofMode) follow the session's documented
    // threading model.
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    // Yaw mode: true = horizon-locked (world), false = camera-local
    std::atomic<bool> m_worldSpaceYaw{true};

    std::atomic<AdsMode> m_adsMode{AdsMode::TrackedWithMark};

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
