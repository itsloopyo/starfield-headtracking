#include "pch.h"
#include "aim_marker.h"

#include "core/logger.h"
#include "core/mod.h"
#include "hooks/camera_hook.h"

#include <cameraunlock/ads/aim_mode.h>

#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX12_IMPLEMENTATION
#include <cameraunlock/rendering/aim_marker_dx12.h>

namespace StarfieldHT {

namespace {

constexpr uint64_t kMarkerLogIntervalMs = 1000;

cameraunlock::rendering::AimMarkerDX12 g_marker;

void MarkerLog(const char* msg) {
    Logger::Instance().Info("%s", msg);
}

} // namespace

void UpdateAimMarker(const CameraFrame* frame) {
    static bool s_loggerSet = false;
    if (!s_loggerSet) {
        s_loggerSet = true;
        g_marker.SetLogger(&MarkerLog);
    }
    // The swap chain is hooked only once the player has chosen the marker mode.
    const cameraunlock::ads::AimMode mode = Mod::Instance().GetAimMode();
    if (mode != cameraunlock::ads::AimMode::FreeLookMarker && !g_marker.Ready()) return;
    const bool ready = g_marker.Ensure();

    const float opacity = frame ? cameraunlock::ads::AimMarkerOpacity(mode, frame->sightsUp) : 0.0f;
    float ndcX = 0.0f, ndcY = 0.0f;
    const bool visible = ready && opacity > 0.0f && ProjectPlayerAim(*frame, ndcX, ndcY);
    g_marker.Publish(visible, ndcX, ndcY, opacity);

    static uint64_t s_lastLogMs = 0;
    const uint64_t now = GetTickCount64();
    if (visible && now - s_lastLogMs >= kMarkerLogIntervalMs) {
        s_lastLogMs = now;
        Logger::Instance().Info("aim marker: ndc(%+.4f,%+.4f) opacity %.2f", ndcX, ndcY, opacity);
    }
}

} // namespace StarfieldHT
