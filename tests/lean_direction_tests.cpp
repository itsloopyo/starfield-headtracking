// Behaviour lock for the 6DOF lean boundary: which way the camera moves for a
// physical lean, and how much of the asymmetric budget each direction gets.
//
// Every expectation here was read off the running game, from the camera basis
// the renderer draws with, and is written in terms of the camera's own axes
// rather than the raw component order, so a reader can check it against what a
// player would feel.
//
// The bug this guards against has shipped elsewhere in the fleet: an inversion
// applied BEFORE the processor's [-LimitZ, +LimitZBack] clamp cuts a forward
// lean off at the 0.10m backward allowance and hands a backward lean the 0.40m
// forward one. Nothing in the render path notices; the camera just refuses to
// lean in. This mod has no inversion setting for exactly that reason, so what is
// locked here is that the conversion at the engine boundary lands the generous
// budget on leaning in.

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "core/constants.h"
#include "core/config.h"
#include "hooks/camera_boundary.h"

#include <cameraunlock/processing/position_processor.h>

namespace {

int g_failures = 0;

// CameraLocalLeanOffset returns the offset in the camera node's own axes, which
// are x=forward, y=up, z=right.
float Forward(const StarfieldHT::NiPoint3& p) { return p.x; }
float Up(const StarfieldHT::NiPoint3& p)      { return p.y; }
float Right(const StarfieldHT::NiPoint3& p)   { return p.z; }

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckNear(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) <= 1e-3f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, expected, actual);
    ++g_failures;
}

// Negative z is the forward lean throughout the library, and the camera's own
// forward axis points forward, so the sign flips at this boundary.
void ForwardLeanMovesCameraForward() {
    const StarfieldHT::NiPoint3 fwd = StarfieldHT::CameraLocalLeanOffset(0.0f, 0.0f, -0.25f);
    Check(Forward(fwd) > 0.0f, "leaning in moves the camera along its forward axis");

    const StarfieldHT::NiPoint3 back = StarfieldHT::CameraLocalLeanOffset(0.0f, 0.0f, 0.25f);
    Check(Forward(back) < 0.0f, "leaning back moves the camera against its forward axis");
}

void UpIsNotInvertedAndLateralIs() {
    const StarfieldHT::NiPoint3 up = StarfieldHT::CameraLocalLeanOffset(0.0f, 0.1f, 0.0f);
    CheckNear(Up(up), 0.1f * StarfieldHT::UNITS_PER_METER,
              "a positive vertical offset raises the camera");

    const StarfieldHT::NiPoint3 lateral = StarfieldHT::CameraLocalLeanOffset(0.1f, 0.0f, 0.0f);
    CheckNear(Right(lateral), -0.1f * StarfieldHT::UNITS_PER_METER,
              "the tracker's positive lateral offset moves the camera left");
}

// Drives the real processor with the position settings the mod hands it, the
// Config's own, rather than a second copy of them in the test.
StarfieldHT::NiPoint3 SaturatedLean(const StarfieldHT::Config& config,
                                    float rawX, float rawY, float rawZ) {
    cameraunlock::PositionProcessor processor;
    processor.SetSettings(config.position);
    const cameraunlock::PositionData raw(rawX, rawY, rawZ);
    // Two ticks so the exponential smoothing has settled on the clamped value.
    cameraunlock::math::Vec3 out = processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    out = processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    return StarfieldHT::CameraLocalLeanOffset(out.x, out.y, out.z);
}

void LeanBudgetsAreNotReversed() {
    // A metre of physical lean either way: far past both limits, so the output
    // is whichever budget that direction actually got.
    const StarfieldHT::Config defaults;
    CheckNear(Forward(SaturatedLean(defaults, 0.0f, 0.0f, -1.0f)), 0.40f * StarfieldHT::UNITS_PER_METER,
              "leaning in gets the 0.40m budget");
    CheckNear(Forward(SaturatedLean(defaults, 0.0f, 0.0f, 1.0f)), -0.10f * StarfieldHT::UNITS_PER_METER,
              "leaning back gets the 0.10m budget");
}

// The vertical clamp is [-limit_y_down, +limit_y], and PositionLimitY and
// PositionLimitYDown are separate rows, so each direction gets its own budget.
// Driven through two different raised limits: at the defaults both are 0.20,
// and the test would pass with the two swapped or one copied into the other.
void VerticalBudgetsAreTheirOwn() {
    StarfieldHT::Config raised;
    raised.position.limit_y = 0.35f;
    raised.position.limit_y_down = 0.15f;
    CheckNear(Up(SaturatedLean(raised, 0.0f, 1.0f, 0.0f)), 0.35f * StarfieldHT::UNITS_PER_METER,
              "standing up gets PositionLimitY");
    CheckNear(Up(SaturatedLean(raised, 0.0f, -1.0f, 0.0f)), -0.15f * StarfieldHT::UNITS_PER_METER,
              "ducking gets PositionLimitYDown");
}

// A zoom scales the lean across the aim and leaves the lean along it whole, so
// leaning in reaches PositionLimitZ at the hip and through any scope. Checked
// with the aim level and with it pitched, where a horizon-locked lean is no
// longer along the camera's own axes.
void ZoomLeavesTheLeanAlongTheAimWhole() {
    const StarfieldHT::Config defaults;
    const StarfieldHT::NiPoint3 lean = SaturatedLean(defaults, 1.0f, 1.0f, -1.0f);
    const float forward[3] = {0.0f, 1.0f, 0.0f}, up[3] = {0.0f, 0.0f, 1.0f}, right[3] = {1.0f, 0.0f, 0.0f};
    for (const float zoom : {1.0f, 0.5f, 0.25f}) {
        float world[3];
        for (int i = 0; i < 3; ++i) {
            world[i] = Forward(lean) * forward[i] + Up(lean) * up[i] + Right(lean) * right[i];
        }
        StarfieldHT::ScaleLeanForZoom(world, forward, zoom);
        CheckNear(StarfieldHT::Dot3(world, forward), 0.40f * StarfieldHT::UNITS_PER_METER,
                  "a forward lean of PositionLimitZ is applied in full at any zoom");
        CheckNear(StarfieldHT::Dot3(world, right), Right(lean) * zoom, "the sideways lean scales with the zoom");
        CheckNear(StarfieldHT::Dot3(world, up), Up(lean) * zoom, "the vertical lean scales with the zoom");

        const float aim[3] = {0.0f, 0.8f, -0.6f}, across[3] = {0.0f, 0.6f, 0.8f};
        float pitched[3];
        for (int i = 0; i < 3; ++i) {
            pitched[i] = Forward(lean) * forward[i] + Up(lean) * up[i] + Right(lean) * right[i];
        }
        const float along = StarfieldHT::Dot3(pitched, aim);
        const float lateral = StarfieldHT::Dot3(pitched, across);
        StarfieldHT::ScaleLeanForZoom(pitched, aim, zoom);
        CheckNear(StarfieldHT::Dot3(pitched, aim), along, "with the aim pitched, the lean along it is still whole");
        CheckNear(StarfieldHT::Dot3(pitched, across), lateral * zoom,
                  "with the aim pitched, the lean across it scales with the zoom");
    }
}

// With the sights up the eye stops short of the rear sight, and nothing else
// about the lean changes: the stop cuts the part along the aim alone, whatever
// the zoom, and at the hip the whole of PositionLimitZ is applied.
void LeaningInStopsOnlyAtTheRearSight() {
    const float kNear = 0.05f;
    // The Eon pistol's rear sight, read from its sight node with the sights up.
    const float kSightDepth = 0.394f;
    const float stop = StarfieldHT::ForwardStopForSight(kSightDepth, kNear);
    CheckNear(stop, kSightDepth - kNear - StarfieldHT::kSightStopMargin,
              "the stop is the rear sight less the near plane and the margin");
    CheckNear(StarfieldHT::ForwardStopForSight(0.03f, kNear), 0.0f,
              "a sight inside the near plane never pulls the eye back");
    Check(std::isinf(StarfieldHT::ForwardStopForSight(std::numeric_limits<float>::infinity(), kNear)),
          "a weapon whose sight is not known has no stop");

    const StarfieldHT::Config defaults;
    const StarfieldHT::NiPoint3 lean = SaturatedLean(defaults, 1.0f, 1.0f, -1.0f);
    const float forward[3] = {0.0f, 1.0f, 0.0f}, up[3] = {0.0f, 0.0f, 1.0f}, right[3] = {1.0f, 0.0f, 0.0f};
    for (const float zoom : {1.0f, 0.5f, 0.25f}) {
        const auto leanAt = [&](cameraunlock::ads::LeanHandover& handover, bool aiming, float forwardStop,
                                unsigned long long nowMs, float out[3]) {
            for (int i = 0; i < 3; ++i) {
                out[i] = Forward(lean) * forward[i] + Up(lean) * up[i] + Right(lean) * right[i];
            }
            StarfieldHT::ScaleLeanForZoom(out, forward, zoom);
            StarfieldHT::HoldLeanBehindSight(out, forward, handover, forwardStop, aiming, nowMs);
        };
        float world[3];

        cameraunlock::ads::LeanHandover hip;
        leanAt(hip, false, stop, 1000, world);
        CheckNear(StarfieldHT::Dot3(world, forward), 0.40f * StarfieldHT::UNITS_PER_METER,
                  "at the hip a forward lean of PositionLimitZ is applied in full, whatever the stop");

        cameraunlock::ads::LeanHandover sights;
        leanAt(sights, false, stop, 1000, world);
        leanAt(sights, true, stop, 1001, world);
        const float justRaised = StarfieldHT::Dot3(world, forward);
        Check(justRaised > 0.39f, "raising the sights does not step the eye back to the stop");
        leanAt(sights, true, stop, 1100, world);
        const float midway = StarfieldHT::Dot3(world, forward);
        Check(midway < justRaised && midway > stop, "the stop eases in as the sights come up");
        leanAt(sights, true, stop, 3000, world);
        CheckNear(StarfieldHT::Dot3(world, forward), stop,
                  "with the sights up the forward lean is cut at the rear sight stop, at any zoom");
        CheckNear(StarfieldHT::Dot3(world, right), Right(lean) * zoom,
                  "the stop leaves the sideways lean on the camera");
        CheckNear(StarfieldHT::Dot3(world, up), Up(lean) * zoom, "the stop leaves the vertical lean on the camera");

        leanAt(sights, false, stop, 3001, world);
        Check(StarfieldHT::Dot3(world, forward) < stop + 0.01f, "lowering the sights does not step the eye forward");
        leanAt(sights, false, stop, 6000, world);
        CheckNear(StarfieldHT::Dot3(world, forward), 0.40f * StarfieldHT::UNITS_PER_METER,
                  "with the sights back down the forward lean is whole again");

        cameraunlock::ads::LeanHandover unknown;
        leanAt(unknown, true, std::numeric_limits<float>::infinity(), 1000, world);
        leanAt(unknown, true, std::numeric_limits<float>::infinity(), 3000, world);
        CheckNear(StarfieldHT::Dot3(world, forward), 0.40f * StarfieldHT::UNITS_PER_METER,
                  "with no stop known the forward lean is whole with the sights up");
    }

    // Leaning back is never touched by the stop.
    cameraunlock::ads::LeanHandover back;
    float behind[3] = {0.0f, -0.10f, 0.0f};
    StarfieldHT::HoldLeanBehindSight(behind, forward, back, stop, true, 1000);
    StarfieldHT::HoldLeanBehindSight(behind, forward, back, stop, true, 3000);
    CheckNear(StarfieldHT::Dot3(behind, forward), -0.10f, "leaning back with the sights up is left alone");
}

} // namespace

int main() {
    ForwardLeanMovesCameraForward();
    UpIsNotInvertedAndLateralIs();
    LeanBudgetsAreNotReversed();
    VerticalBudgetsAreTheirOwn();
    ZoomLeavesTheLeanAlongTheAimWhole();
    LeaningInStopsOnlyAtTheRearSight();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all lean-direction checks passed\n");
    return 0;
}
