// Behaviour lock for stock sights, the fourth aim mode: while the sights are up
// the head's yaw, pitch and lean ease out and roll stays, and at the hip, and in
// the other three modes, the pose is whole.

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "game/weapon_projection.h"
#include "hooks/camera_boundary.h"

#include <cameraunlock/ads/ads_fade.h>
#include <cameraunlock/ads/aim_mode.h>

using namespace StarfieldHT;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AimMode;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void Near(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) <= 1e-4f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, expected, actual);
    ++g_failures;
}

struct Pose {
    float yaw = 20.0f, pitch = -8.0f, roll = 12.0f;
    float x = 0.2f, y = 0.1f, z = -0.4f;
};

// One frame of the camera hook's pose path: the fade is fed the mode and the
// polled sights, and its output scales the pose before anything else reads it.
struct Frame {
    AdsFade fade;

    float Share(AimMode mode, bool aiming, unsigned long long nowMs) {
        return fade.Update(cameraunlock::ads::StockSightsEngaged(mode, aiming), nowMs);
    }

    Pose Apply(AimMode mode, bool aiming, unsigned long long nowMs, float* share = nullptr) {
        Pose pose;
        const float s = Share(mode, aiming, nowMs);
        if (share) *share = s;
        EaseOutForStockSights(s, pose.yaw, pose.pitch, pose.x, pose.y, pose.z);
        return pose;
    }
};

void CheckWhole(const Pose& pose, const char* what) {
    const Pose whole;
    Check(pose.yaw == whole.yaw && pose.pitch == whole.pitch && pose.roll == whole.roll && pose.x == whole.x &&
              pose.y == whole.y && pose.z == whole.z,
          what);
}

void HipPassesThrough() {
    Frame frame;
    for (const unsigned long long now : {1000ull, 1016ull, 5000ull}) {
        CheckWhole(frame.Apply(AimMode::StockSights, false, now), "at the hip stock sights leaves the pose whole");
    }
}

void SightsUpLeavesRollAlone() {
    Frame frame;
    frame.Apply(AimMode::StockSights, false, 1000);
    frame.Apply(AimMode::StockSights, true, 1016);
    const Pose pose = frame.Apply(AimMode::StockSights, true, 1016 + AdsFade::kLowerMs + 1);
    Near(pose.yaw, 0.0f, "sights up: yaw is eased out");
    Near(pose.pitch, 0.0f, "sights up: pitch is eased out");
    Near(pose.x, 0.0f, "sights up: the sideways lean is eased out");
    Near(pose.y, 0.0f, "sights up: the vertical lean is eased out");
    Near(pose.z, 0.0f, "sights up: the lean along the aim is eased out too");
    Check(pose.roll == Pose{}.roll, "sights up: roll is the tracker's roll");

    // What that pose draws: the view is on the aim, turned about it by the roll.
    CameraBasis clean{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {12, -7, 3}};
    CameraBasis drawn = clean;
    ApplyHeadRotationToBasis(drawn, pose.yaw, pose.pitch, pose.roll, true);
    Near(Dot3(drawn.f, clean.f), 1.0f, "sights up: the drawn view looks along the clean aim");
    Check(std::fabs(Dot3(drawn.u, clean.u) - cosf(pose.roll * DEG_TO_RAD)) < 1e-4f,
          "sights up: the drawn view is tilted by the head's roll");
}

void MidTransitionScalesByTheFade() {
    Frame frame;
    frame.Apply(AimMode::StockSights, false, 1000);
    frame.Apply(AimMode::StockSights, true, 1016);
    float share = 0.0f;
    const Pose pose = frame.Apply(AimMode::StockSights, true, 1016 + AdsFade::kLowerMs / 2, &share);
    const Pose whole;
    Check(share > 0.05f && share < 0.95f, "half way through the fade the share is between the ends");
    Near(pose.yaw, whole.yaw * share, "mid-transition yaw is scaled by the fade");
    Near(pose.pitch, whole.pitch * share, "mid-transition pitch is scaled by the fade");
    Near(pose.x, whole.x * share, "mid-transition the sideways lean is scaled by the fade");
    Near(pose.y, whole.y * share, "mid-transition the vertical lean is scaled by the fade");
    Near(pose.z, whole.z * share, "mid-transition the lean along the aim is scaled by the fade");
    Check(pose.roll == whole.roll, "mid-transition roll is untouched");
}

// One frame is 16 ms, and the whole fade is 150 ms down and 250 ms back, so no
// frame may move the share by more than a fraction of it.
constexpr float kLargestStep = 0.2f;

void ReversalContinues(bool byModeKey) {
    Frame frame;
    unsigned long long now = 1000;
    AimMode mode = AimMode::StockSights;
    bool aiming = false;
    float last = frame.Share(mode, aiming, now);
    const auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            now += 16;
            const float share = frame.Share(mode, aiming, now);
            Check(std::fabs(share - last) <= kLargestStep,
                  byModeKey ? "the mode key mid-aim never steps the pose" : "the aim button never steps the pose");
            last = share;
        }
    };
    // Into the aim, back out part way through, in again, and all the way out.
    aiming = true;
    run(4);
    Check(last > 0.0f && last < 1.0f, "the reversal starts part way through the transition");
    if (byModeKey) mode = AimMode::SightsLocked; else aiming = false;
    run(3);
    if (byModeKey) mode = AimMode::StockSights; else aiming = true;
    run(40);
    Near(last, 0.0f, "the pose is eased out once the sights are up in stock sights");
    if (byModeKey) mode = AimMode::SightsLocked; else aiming = false;
    run(40);
    Near(last, 1.0f, "and whole again afterwards");
}

void OtherModesLeaveThePoseAlone() {
    for (const AimMode mode : {AimMode::SightsLocked, AimMode::FreeLookMarker, AimMode::TrueFreeLook}) {
        Frame frame;
        for (const bool aiming : {false, true, true, false}) {
            for (const unsigned long long now : {1000ull, 1100ull, 2000ull}) {
                CheckWhole(frame.Apply(mode, aiming, now), "outside stock sights the pose is whole, sights up or down");
            }
        }
    }
}

// The weapon's eye, and with it the lean the weapon pass hands on, is that of
// sights locked: a lean on its way out never swings the weapon across the frame.
void WeaponEyeIsThatOfSightsLocked() {
    using cameraunlock::ads::DecodeAimMode;
    using cameraunlock::ads::IsFreeLook;
    Check(!IsFreeLook(AimMode::StockSights) && !IsFreeLook(DecodeAimMode(true, false, true)) &&
              !IsFreeLook(DecodeAimMode(true, true, true)),
          "stock sights is not free look, with TrueFreeLook true beside StockSights as well");

    const CameraBasis clean{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {12, -7, 3}};
    CameraBasis drawn = clean;
    drawn.e[0] += 0.2f;
    drawn.e[1] += 0.3f;
    drawn.e[2] += 0.1f;
    for (const float sightsUp : {0.0f, 0.5f, 1.0f}) {
        WeaponEye locked, stock;
        float lockedEye[3], stockEye[3];
        const float lockedShare = locked.CleanEyeShare(IsFreeLook(AimMode::SightsLocked), 1000);
        const float stockShare = stock.CleanEyeShare(IsFreeLook(DecodeAimMode(true, false, true)), 1000);
        WeaponPassEye(clean, drawn, lockedShare, sightsUp, lockedEye);
        WeaponPassEye(clean, drawn, stockShare, sightsUp, stockEye);
        for (int i = 0; i < 3; ++i) {
            Near(stockEye[i], lockedEye[i], "the weapon pass's eye in stock sights is that of sights locked");
        }
    }
}

}  // namespace

int main() {
    HipPassesThrough();
    SightsUpLeavesRollAlone();
    MidTransitionScalesByTheFade();
    ReversalContinues(false);
    ReversalContinues(true);
    OtherModesLeaveThePoseAlone();
    WeaponEyeIsThatOfSightsLocked();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all stock sights checks passed\n");
    return 0;
}
