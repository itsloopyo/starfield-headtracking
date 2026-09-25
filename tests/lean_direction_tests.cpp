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

} // namespace

int main() {
    ForwardLeanMovesCameraForward();
    UpIsNotInvertedAndLateralIs();
    LeanBudgetsAreNotReversed();
    VerticalBudgetsAreTheirOwn();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all lean-direction checks passed\n");
    return 0;
}
