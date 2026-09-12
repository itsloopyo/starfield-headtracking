// Behaviour lock for the rotation half of the camera boundary: which way the
// view turns for each tracker axis, and in what order the three are composed.
//
// The signs here are not derivable from the engine. The protocol says nothing
// about which way the tracker calls positive, so they were settled in game
// against the camera basis the renderer draws with - see camera_boundary.h. A
// change that flips one of them is a change a player reports as "yaw is
// backwards now", and nothing else in the build would catch it.
//
// The reference basis is the one the engine hands over: x=forward, y=up,
// z=right as the node's own axes, expressed in a Z-up world.

#include <cmath>
#include <cstdio>

#include "core/constants.h"
#include "hooks/camera_boundary.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckNear(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) <= 1e-4f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, expected, actual);
    ++g_failures;
}

StarfieldHT::CameraBasis LevelBasis() {
    // Looking north along +y, up along world +z, right along +x.
    return StarfieldHT::CameraBasis{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {5, -3, 2}};
}

float Dot(const float a[3], const float b[3]) {
    return StarfieldHT::Dot3(a, b);
}

// A pure yaw turns the view toward the camera's own right.
void PositiveYawTurnsRight() {
    const StarfieldHT::CameraBasis level = LevelBasis();
    StarfieldHT::CameraBasis turned = level;
    StarfieldHT::ApplyHeadRotationToBasis(turned, 10.0f, 0.0f, 0.0f, true);

    Check(Dot(turned.f, level.r) > 0.0f, "a positive tracker yaw turns the view to the right");
    CheckNear(Dot(turned.f, level.u), 0.0f, "a pure yaw does not raise or lower the view");
    // Without the sign, a 170 degree turn passes this too.
    CheckNear(Dot(turned.f, level.f), std::cos(10.0f * StarfieldHT::DEG_TO_RAD),
              "a 10 degree yaw turns the view by 10 degrees");
}

// On a level camera the world's up axis and the camera's own are the same
// vector, so the level test above cannot tell the two yaw modes apart: negating
// yaw in one branch alone passes it. Pitching first separates them, and both
// branches have to turn the view the same way.
void BothYawModesTurnRightOnAPitchedCamera() {
    const StarfieldHT::CameraBasis level = LevelBasis();
    StarfieldHT::CameraBasis pitched = level;
    StarfieldHT::ApplyHeadRotationToBasis(pitched, 0.0f, 40.0f, 0.0f, true);

    const bool yawModes[] = {true, false};
    for (const bool worldSpaceYaw : yawModes) {
        StarfieldHT::CameraBasis turned = pitched;
        StarfieldHT::ApplyHeadRotationToBasis(turned, 15.0f, 0.0f, 0.0f, worldSpaceYaw);
        Check(Dot(turned.f, pitched.r) > 0.0f,
              worldSpaceYaw ? "horizon-locked yaw turns a pitched view to the right"
                            : "camera-local yaw turns a pitched view to the right");
    }
}

// A pure pitch raises it, and pitch is the one axis that is NOT negated.
void PositivePitchLooksUp() {
    const StarfieldHT::CameraBasis level = LevelBasis();
    StarfieldHT::CameraBasis pitched = level;
    StarfieldHT::ApplyHeadRotationToBasis(pitched, 0.0f, 10.0f, 0.0f, true);

    Check(Dot(pitched.f, level.u) > 0.0f, "a positive tracker pitch raises the view");
    CheckNear(Dot(pitched.f, level.r), 0.0f, "a pure pitch does not turn the view sideways");
}

// A pure roll turns the picture about the view axis and leaves the aim on the
// centre of it - which is what makes the stock sight picture usable in the
// roll-only sights mode.
void PositiveRollLeavesForwardAlone() {
    const StarfieldHT::CameraBasis level = LevelBasis();
    StarfieldHT::CameraBasis rolled = level;
    StarfieldHT::ApplyHeadRotationToBasis(rolled, 0.0f, 0.0f, 10.0f, true);

    for (int i = 0; i < 3; ++i) {
        CheckNear(rolled.f[i], level.f[i], "roll does not move the forward axis");
    }
    Check(Dot(rolled.u, level.r) < 0.0f, "a positive tracker roll tilts the up axis to the left");
}

// The eye is never moved by a rotation. Position is the lean's job, applied by
// the caller from CameraLocalLeanOffset.
void RotationNeverMovesTheEye() {
    const StarfieldHT::CameraBasis level = LevelBasis();
    StarfieldHT::CameraBasis turned = level;
    StarfieldHT::ApplyHeadRotationToBasis(turned, 12.0f, -8.0f, 5.0f, true);
    for (int i = 0; i < 3; ++i) {
        CheckNear(turned.e[i], level.e[i], "the eye stays where the game put it");
    }
}

// A lean travels along the body's axes, not the head's: the eye moves across
// the floor whatever the camera is pointing at. Without this, leaning in while
// looking down drives the eye into the ground.
void LeanBasisIgnoresPitchAndRoll() {
    const StarfieldHT::CameraBasis level = LevelBasis();

    StarfieldHT::CameraBasis steep = level;
    StarfieldHT::ApplyHeadRotationToBasis(steep, 0.0f, -60.0f, 0.0f, true);
    Check(steep.f[2] < -0.5f, "the reference camera really is pitched steeply down");

    const StarfieldHT::CameraBasis flat = StarfieldHT::HorizonLockedBasis(steep);
    CheckNear(flat.f[2], 0.0f, "the lean's forward axis is level however far the camera pitches");
    CheckNear(flat.u[2], 1.0f, "the lean's up axis is world up");
    CheckNear(flat.r[2], 0.0f, "the lean's right axis is level");
    CheckNear(Dot(flat.f, flat.r), 0.0f, "the lean basis stays orthogonal");
    Check(Dot(flat.f, level.f) > 0.99f, "flattening a pitched camera keeps the direction it faces");
    Check(Dot(flat.r, level.r) > 0.99f, "flattening a pitched camera keeps its handedness");

    // A rolled camera is the case that collapses if both axes are flattened and
    // then orthogonalised against each other.
    StarfieldHT::CameraBasis rolled = level;
    StarfieldHT::ApplyHeadRotationToBasis(rolled, 0.0f, 40.0f, 90.0f, true);
    const StarfieldHT::CameraBasis unrolled = StarfieldHT::HorizonLockedBasis(rolled);
    CheckNear(Dot(unrolled.f, unrolled.f), 1.0f, "a rolled camera still yields a unit forward");
    CheckNear(Dot(unrolled.r, unrolled.r), 1.0f, "a rolled camera still yields a unit right");
    CheckNear(Dot(unrolled.f, unrolled.r), 0.0f, "a rolled camera still yields an orthogonal basis");
    CheckNear(unrolled.f[2], 0.0f, "a rolled camera's lean forward is still level");
}

// The lean basis has to keep the handedness of the camera it came from at EVERY
// orientation, including past the point where the camera's own up axis stops
// agreeing with the world's. Reading handedness off that agreement inverted the
// lateral lean as a rolling camera passed its side - lean right, eye goes left -
// and snapped across in a single frame at 90 degrees, which is exactly where a
// ship spends time.
void LeanBasisKeepsHandednessUpsideDown() {
    const StarfieldHT::CameraBasis level = LevelBasis();

    const auto handedness = [](const StarfieldHT::CameraBasis& b) {
        const float cross[3] = {
            b.f[1] * b.u[2] - b.f[2] * b.u[1],
            b.f[2] * b.u[0] - b.f[0] * b.u[2],
            b.f[0] * b.u[1] - b.f[1] * b.u[0],
        };
        return Dot(cross, b.r) >= 0.0f ? 1.0f : -1.0f;
    };
    const float expected = handedness(level);

    const float rolls[] = {0.0f, 45.0f, 89.0f, 91.0f, 120.0f, 179.0f, -120.0f};
    const float pitches[] = {-40.0f, 0.0f, 40.0f};
    for (const float pitch : pitches) {
        for (const float roll : rolls) {
            StarfieldHT::CameraBasis camera = level;
            StarfieldHT::ApplyHeadRotationToBasis(camera, 0.0f, pitch, roll, true);
            const StarfieldHT::CameraBasis lean = StarfieldHT::HorizonLockedBasis(camera);
            CheckNear(handedness(lean), expected, "the lean basis keeps the camera's handedness");
            CheckNear(Dot(lean.f, lean.r), 0.0f, "the lean basis stays orthogonal");
            CheckNear(lean.f[2], 0.0f, "the lean basis forward stays level");
        }
    }

    // And no snap across the point where the old sign flipped.
    StarfieldHT::CameraBasis before = level, after = level;
    StarfieldHT::ApplyHeadRotationToBasis(before, 0.0f, 40.0f, 89.0f, true);
    StarfieldHT::ApplyHeadRotationToBasis(after, 0.0f, 40.0f, 91.0f, true);
    Check(Dot(StarfieldHT::HorizonLockedBasis(before).r,
              StarfieldHT::HorizonLockedBasis(after).r) > 0.9f,
          "the lean's right axis does not invert as the camera rolls past its side");
}

// The basis is built from two matrices read out of game memory, so it is not
// guaranteed orthonormal. Dividing by a zero length would put NaN into the
// camera's local transform, which the scene graph rebuilds everything from.
void DegenerateBasisIsPassedThroughUntouched() {
    const StarfieldHT::CameraBasis zeroed{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 2, 3}};
    const StarfieldHT::CameraBasis out = StarfieldHT::HorizonLockedBasis(zeroed);
    for (int i = 0; i < 3; ++i) {
        Check(out.f[i] == out.f[i] && out.u[i] == out.u[i] && out.r[i] == out.r[i],
              "a degenerate camera basis does not produce NaN");
    }
}

// Horizon-locked yaw turns about the world's up axis, so it cannot change how
// far above or below the horizon the view is pointing. Camera-local yaw turns
// about the camera's own up axis, which on a pitched camera does.
void WorldYawKeepsTheHorizon() {
    StarfieldHT::CameraBasis pitched = LevelBasis();
    StarfieldHT::ApplyHeadRotationToBasis(pitched, 0.0f, 40.0f, 0.0f, true);

    StarfieldHT::CameraBasis world = pitched;
    StarfieldHT::ApplyHeadRotationToBasis(world, 30.0f, 0.0f, 0.0f, true);
    CheckNear(world.f[2], pitched.f[2], "horizon-locked yaw keeps the view at the same elevation");

    StarfieldHT::CameraBasis local = pitched;
    StarfieldHT::ApplyHeadRotationToBasis(local, 30.0f, 0.0f, 0.0f, false);
    Check(std::fabs(local.f[2] - pitched.f[2]) > 1e-3f,
          "camera-local yaw on a pitched camera does change the elevation");
}

// Yaw, then pitch, then roll - each about the axes the previous turn left
// behind. A reordering here is invisible on any single axis and shows up only
// on a combined pose.
void CompositionIsYawThenPitchThenRoll() {
    StarfieldHT::CameraBasis combined = LevelBasis();
    StarfieldHT::ApplyHeadRotationToBasis(combined, 20.0f, 15.0f, 25.0f, false);

    StarfieldHT::CameraBasis stepwise = LevelBasis();
    StarfieldHT::ApplyHeadRotationToBasis(stepwise, 20.0f, 0.0f, 0.0f, false);
    StarfieldHT::ApplyHeadRotationToBasis(stepwise, 0.0f, 15.0f, 0.0f, false);
    StarfieldHT::ApplyHeadRotationToBasis(stepwise, 0.0f, 0.0f, 25.0f, false);

    for (int i = 0; i < 3; ++i) {
        CheckNear(combined.f[i], stepwise.f[i], "combined pose composes yaw, then pitch, then roll");
        CheckNear(combined.u[i], stepwise.u[i], "combined pose composes yaw, then pitch, then roll");
        CheckNear(combined.r[i], stepwise.r[i], "combined pose composes yaw, then pitch, then roll");
    }
}

// A basis that has stopped being orthonormal skews the picture rather than
// turning it, and the drift accumulates over a session's worth of frames.
void RepeatedApplicationStaysOrthonormal() {
    StarfieldHT::CameraBasis basis = LevelBasis();
    for (int i = 0; i < 5000; ++i) {
        StarfieldHT::ApplyHeadRotationToBasis(basis, 1.3f, -0.7f, 0.9f, i % 2 == 0);
    }
    CheckNear(Dot(basis.f, basis.f), 1.0f, "forward stays unit length");
    CheckNear(Dot(basis.u, basis.u), 1.0f, "up stays unit length");
    CheckNear(Dot(basis.r, basis.r), 1.0f, "right stays unit length");
    CheckNear(Dot(basis.f, basis.u), 0.0f, "forward stays perpendicular to up");
    CheckNear(Dot(basis.f, basis.r), 0.0f, "forward stays perpendicular to right");
    CheckNear(Dot(basis.u, basis.r), 0.0f, "up stays perpendicular to right");

    // The engine's handedness is carried over from the live basis rather than
    // assumed, so right must stay on the same side it started on.
    const float cross[3] = {
        basis.f[1] * basis.u[2] - basis.f[2] * basis.u[1],
        basis.f[2] * basis.u[0] - basis.f[0] * basis.u[2],
        basis.f[0] * basis.u[1] - basis.f[1] * basis.u[0],
    };
    Check(Dot(cross, basis.r) > 0.0f, "the frame keeps the handedness it started with");
}

// A zero pose is the identity: nothing is written, nothing drifts, and the
// frames where the tracker is centred look exactly like the frames where the
// mod is switched off.
void ZeroPoseChangesNothing() {
    const StarfieldHT::CameraBasis level = LevelBasis();
    StarfieldHT::CameraBasis basis = level;
    StarfieldHT::ApplyHeadRotationToBasis(basis, 0.0f, 0.0f, 0.0f, true);
    for (int i = 0; i < 3; ++i) {
        CheckNear(basis.f[i], level.f[i], "a zero pose leaves the forward axis alone");
        CheckNear(basis.u[i], level.u[i], "a zero pose leaves the up axis alone");
        CheckNear(basis.r[i], level.r[i], "a zero pose leaves the right axis alone");
    }
}

} // namespace

int main() {
    PositiveYawTurnsRight();
    BothYawModesTurnRightOnAPitchedCamera();
    LeanBasisIgnoresPitchAndRoll();
    LeanBasisKeepsHandednessUpsideDown();
    DegenerateBasisIsPassedThroughUntouched();
    PositivePitchLooksUp();
    PositiveRollLeavesForwardAlone();
    RotationNeverMovesTheEye();
    WorldYawKeepsTheHorizon();
    CompositionIsYawThenPitchThenRoll();
    RepeatedApplicationStaysOrthonormal();
    ZeroPoseChangesNothing();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all head-rotation checks passed\n");
    return 0;
}
