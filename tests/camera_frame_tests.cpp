#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include "game/camera_frame.h"
#include "game/ship_aim.h"

using namespace StarfieldHT;

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    ++failures;
}

CameraFrame Frame(int index) {
    CameraFrame frame{};
    frame.niCamera = 1;
    frame.clean = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {float(index), 0, 0}};
    frame.drawn = frame.clean;
    const float up[] = {0, 0, 1};
    RotateBasis(frame.drawn, up, float(index) * 0.05f);
    frame.frustumRight = float(index);
    frame.frustumTop = float(index);
    frame.frustumNear = float(index);
    return frame;
}

CameraBasis InParent(const CameraBasis& local, const Mat3& parent, const float position[3]) {
    CameraBasis world{};
    MulRowVec(local.f, parent, world.f);
    MulRowVec(local.u, parent, world.u);
    MulRowVec(local.r, parent, world.r);
    MulRowVec(local.e, parent, world.e);
    for (int i = 0; i < 3; ++i) world.e[i] += position[i];
    return world;
}

void CheckBasis(const CameraBasis& actual, const CameraBasis& expected) {
    for (int i = 0; i < 3; ++i) {
        Check(std::fabs(actual.f[i] - expected.f[i]) < 0.0002f, "submitted clean forward");
        Check(std::fabs(actual.u[i] - expected.u[i]) < 0.0002f, "submitted clean up");
        Check(std::fabs(actual.r[i] - expected.r[i]) < 0.0002f, "submitted clean right");
        Check(std::fabs(actual.e[i] - expected.e[i]) < 0.0002f, "submitted clean eye");
    }
}
}

// The acceptance window itself, checked from both sides, against the physical
// quantities the header documents rather than against the constants themselves.
// Every other rejection case here is around a thousand times the threshold, so
// the constants could be widened by two orders of magnitude - accepting eight
// degrees of mismatch, and with it a wrong clean basis under the reticle and the
// weapon - without a single check failing. Deriving the bounds from the
// constants would widen with them and catch nothing either.
void ToleranceBoundsAreLocked() {
    // 10 mm of eye travel: kMaxPositionErrorSq is a squared error summed over
    // the three components, so the window is its square root.
    constexpr float kPositionWindowMetres = 0.010f;
    // 0.81 degrees: kMaxRotationErrorSq is a squared Frobenius norm over the
    // three basis vectors, which for a rotation of angle t is 8*sin(t/2)^2.
    constexpr float kRotationWindowRadians = 0.01414f;

    CameraFrameHistory history;
    CameraFrame out{};
    const auto frame = Frame(3);
    history.Publish(frame);

    auto inside = frame.drawn;
    inside.e[0] += kPositionWindowMetres * 0.95f;
    Check(history.Find(inside, out), "an eye just inside the position window still matches");
    auto outside = frame.drawn;
    outside.e[0] += kPositionWindowMetres * 1.05f;
    Check(!history.Find(outside, out), "an eye just outside the position window does not match");

    const float up[] = {0, 0, 1};
    auto justTurned = frame.drawn;
    RotateBasis(justTurned, up, kRotationWindowRadians * 0.95f);
    Check(history.Find(justTurned, out), "a basis just inside the rotation window still matches");
    Check(!history.Find(justTurned, out, 1e-8f), "strict ship matching rejects nearby head poses");
    Check(history.Find(frame.drawn, out, 1e-8f), "strict ship matching accepts the exact pose");
    auto turnedTooFar = frame.drawn;
    RotateBasis(turnedTooFar, up, kRotationWindowRadians * 1.05f);
    Check(!history.Find(turnedTooFar, out), "a basis just outside the rotation window does not match");
}

void ShipAimKeepsProducedPose() {
    const CameraBasis clean{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, 0, 0}};
    const float up[] = {0, 0, 1};
    const float right[] = {1, 0, 0};
    auto ship = clean;
    RotateBasis(ship, up, 2.8f);
    RotateBasis(ship, right, -0.35f);
    const auto shipRotation = RotationOfBasis(ship);
    const float shipPosition[] = {-3, -22, -5};
    const auto expected = InParent(clean, shipRotation, shipPosition);
    CameraFrameHistory produced;
    for (float yaw : {-1.9f, -0.4f, 0.35f}) {
        CameraFrame source{};
        source.niCamera = 1;
        source.clean = clean;
        source.drawn = clean;
        RotateBasis(source.drawn, up, yaw);
        RotateBasis(source.drawn, right, 0.25f);
        source.drawn.e[0] = 0.12f;
        source.drawn.e[2] = -0.08f;
        const auto exterior = InParent(source.drawn, shipRotation, shipPosition);
        produced.Publish(MakeShipCameraFrame(source, shipRotation));
        RotateBasis(source.drawn, up, 0.1f);
        const auto newer = MakeShipCameraFrame(source, shipRotation);
        produced.Publish(newer);
        const auto wrong = RebaseCameraFrame(newer, exterior);
        Check(Dot3(wrong.clean.f, expected.f) < 0.999f,
              "latest head pose would change an already produced ship aim");
        CameraFrame matched{};
        auto lookup = exterior;
        for (float& coordinate : lookup.e) coordinate = 0;
        Check(produced.Find(lookup, matched, 1e-8f), "ship aim finds its converted pose after a newer pose arrives");
        CheckBasis(RebaseCameraFrame(matched, exterior).clean, expected);
        auto shifted = exterior;
        shifted.e[0] += 100;
        auto shiftedExpected = expected;
        shiftedExpected.e[0] += 100;
        CheckBasis(RebaseCameraFrame(matched, shifted).clean, shiftedExpected);
        RotateBasis(lookup, up, 0.001f);
        Check(!produced.Find(lookup, matched, 1e-8f), "nearby ship pose cannot replace the exact conversion");
    }
}

int main() {
    ToleranceBoundsAreLocked();
    ShipAimKeepsProducedPose();

    CameraFrameHistory history;
    CameraFrame out{};
    Check(!history.Latest(out), "no frame before first camera update");
    const auto rendered = Frame(1);
    history.Publish(rendered);
    for (int i = 2; i <= 8; ++i) history.Publish(Frame(i));
    Check(history.Find(rendered.drawn, out) && out.clean.e[0] == 1,
          "queued render uses its own clean basis after simulation advances");

    CameraFrame latched = out;
    for (int i = 9; i <= 80; ++i) history.Publish(Frame(i));
    Check(latched.clean.e[0] == 1 && latched.drawn.f[0] == rendered.drawn.f[0],
          "weapon pass keeps world pass snapshot while simulation publishes");
    Check(!history.Find(rendered.drawn, out), "expired render snapshot cannot select an unrelated frame");

    auto latest = Frame(80);
    history.Publish({});
    Check(!history.Latest(out), "tracking disabled for new HUD frames");
    Check(history.Find(latest.drawn, out) && out.clean.e[0] == 80,
          "already queued tracked render still gets its correction when tracking stops");
    auto unrelated = latest.drawn;
    unrelated.e[1] += 5;
    Check(!history.Find(unrelated, out), "other camera is rejected");
    unrelated = latest.drawn;
    const float up[] = {0, 0, 1};
    RotateBasis(unrelated, up, 0.2f);
    Check(!history.Find(unrelated, out), "other orientation at same eye is rejected");

    auto nearest = Frame(80);
    nearest.drawn.e[0] += 0.005f;
    nearest.clean.e[0] = 99;
    history.Publish(nearest);
    Check(history.Find(latest.drawn, out) && out.clean.e[0] == 80,
          "exact older match wins over merely nearby latest frame");
    latest.clean.e[0] = 100;
    history.Publish(latest);
    Check(history.Find(latest.drawn, out) && out.clean.e[0] == 100,
          "identical drawn poses select the most recent clean basis");

    CameraFrameHistory concurrent;
    concurrent.Publish(Frame(1));
    std::atomic<bool> start{false};
    std::atomic<bool> coherent{true};
    std::thread reader([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < 20000; ++i) {
            CameraFrame snapshot{};
            if (!concurrent.Latest(snapshot) || snapshot.clean.e[0] != snapshot.frustumRight
                || snapshot.frustumRight != snapshot.frustumTop || snapshot.frustumTop != snapshot.frustumNear)
                coherent.store(false, std::memory_order_relaxed);
        }
    });
    start.store(true, std::memory_order_release);
    for (int i = 2; i < 20000; ++i) concurrent.Publish(Frame(i));
    reader.join();
    Check(coherent.load(), "concurrent publication never drops or tears a valid snapshot");

    CameraBasis cleanLocal{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0.1f, 0.2f, -0.3f}};
    for (const float headAngle : {-0.7f, 0.0f, 0.5f}) {
        auto trackedLocal = cleanLocal;
        RotateBasis(trackedLocal, up, headAngle);
        float right[3] = {trackedLocal.r[0], trackedLocal.r[1], trackedLocal.r[2]};
        RotateBasis(trackedLocal, right, headAngle * 0.6f);
        float forward[3] = {trackedLocal.f[0], trackedLocal.f[1], trackedLocal.f[2]};
        RotateBasis(trackedLocal, forward, headAngle * 0.4f);
        trackedLocal.e[0] += 0.15f;
        trackedLocal.e[1] -= 0.08f;
        trackedLocal.e[2] += 0.04f;
        auto parent = cleanLocal;
        RotateBasis(parent, up, 0.3f);
        const Mat3 oldParent = RotationOfBasis(parent);
        const float oldPosition[] = {10, 20, 30};
        CameraFrame reference{};
        reference.niCamera = 7;
        reference.clean = InParent(cleanLocal, oldParent, oldPosition);
        reference.drawn = InParent(trackedLocal, oldParent, oldPosition);
        WriteBasis(trackedLocal, reference.local);
        CameraFrameHistory poses;
        poses.Publish(reference);
        auto nextPose = reference;
        nextPose.local.entry[0][0] += 0.01f;
        poses.Publish(nextPose);

        for (const float mouseAngle : {-1.0f, 0.2f, 0.8f}) {
            auto movedParent = parent;
            RotateBasis(movedParent, up, mouseAngle);
            const float pitchAxis[] = {1, 0, 0};
            RotateBasis(movedParent, pitchAxis, 0.2f);
            const Mat3 newParent = RotationOfBasis(movedParent);
            const float newPosition[] = {12, 24, 28};
            auto submitted = InParent(trackedLocal, newParent, newPosition);
            auto expected = InParent(cleanLocal, newParent, newPosition);
            Check(!poses.Find(submitted, out), "simulation world snapshot cannot match changed parent");
            Check(poses.FindLocal(7, reference.local, out), "submitted local pose survives next simulation update");
            Check(!poses.FindLocal(8, reference.local, out), "local pose from another camera is rejected");
            CheckBasis(RebaseCameraFrame(out, submitted).clean, expected);
        }
    }
    std::printf("Camera frame history: %d failures\n", failures);
    return failures ? 1 : 0;
}
