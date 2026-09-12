#include "pch.h"
#include "reticle.h"

#include "core/logger.h"
#include "core/mod.h"
#include "game/ads_state.h"
#include "game/game_state.h"
#include "hooks/camera_hook.h"

#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX12_IMPLEMENTATION
#include <cameraunlock/rendering/aim_marker_dx12.h>

namespace StarfieldHT {

namespace {

cameraunlock::rendering::AimMarkerDX12 g_marker;

void MarkerLog(const char* msg) {
    Logger::Instance().Info("%s", msg);
}

} // namespace

void UpdateReticle() {
    static bool s_loggerSet = false;
    if (!s_loggerSet) {
        s_loggerSet = true;
        g_marker.SetLogger(&MarkerLog);
    }
    const bool markerMode = Mod::Instance().GetAdsMode() == AdsMode::TrackedWithMark;
    const bool ready = markerMode && g_marker.Ensure();

    CameraFrame frame;
    float ndcX = 0.0f, ndcY = 0.0f;
    const bool visible = ready
                      && Mod::Instance().IsEnabled()
                      && AdsState::IsAiming()
                      && GameState::IsInGameplay()
                      && GetCameraFrame(frame)
                      && ProjectPlayerAim(frame, ndcX, ndcY);
    g_marker.Publish(visible, ndcX, ndcY);
}

} // namespace StarfieldHT
