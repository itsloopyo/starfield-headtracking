#include <cmath>
#include <cstdio>
#include "game/head_attached.h"

using namespace StarfieldHT;

namespace {

NiMatrix44 Compose(const NiMatrix44& local, const NiMatrix44& parentWorld) {
    NiMatrix44 world{};
    const Mat3 rot = Mul(RotationOf(local), RotationOf(parentWorld));
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) world.entry[r][c] = rot.m[r][c];
    MulRowVec(local.entry[3], RotationOf(parentWorld), world.entry[3]);
    for (int i = 0; i < 3; ++i) world.entry[3][i] += parentWorld.entry[3][i];
    world.entry[3][3] = 1.0f;
    return world;
}

} // namespace

int main() {
    int failures = 0;
    const auto near = [&](float actual, float expected, const char* message) {
        if (std::fabs(actual - expected) >= 0.0001f) {
            std::printf("FAIL: %s (got %f, expected %f)\n", message, actual, expected);
            ++failures;
        }
    };

    CameraBasis clean{};
    const float up[] = {0, 0, 1};
    clean = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {10, 20, 30}};
    RotateBasis(clean, up, 0.7f);

    // A bone at the eye, turned an arbitrary way, carrying a light 0.1 m up and
    // 0.15 m to one side of it.
    NiMatrix44 parent{};
    CameraBasis bone = clean;
    const float tilt[] = {1, 0, 0};
    RotateBasis(bone, tilt, 1.2f);
    WriteBasis(bone, parent);
    NiMatrix44 light{};
    light.entry[0][1] = 1;
    light.entry[1][2] = 1;
    light.entry[2][0] = 1;
    light.entry[3][1] = 0.10f;
    light.entry[3][2] = -0.15f;
    light.entry[3][3] = 1;
    const NiMatrix44 cleanWorld = Compose(light, parent);

    const NiMatrix44 still = HeadAttachedLocal(light, parent, clean, clean, 1.5f);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) near(still.entry[r][c], light.entry[r][c], "no head pose leaves the node alone");

    CameraBasis drawn = clean;
    RotateBasis(drawn, up, 0.5f);
    const float pitchAxis[] = {drawn.r[0], drawn.r[1], drawn.r[2]};
    RotateBasis(drawn, pitchAxis, -0.3f);
    drawn.e[0] += 0.2f;
    drawn.e[2] -= 0.05f;

    const NiMatrix44 world = Compose(HeadAttachedLocal(light, parent, clean, drawn, 1.0f), parent);
    const Mat3 turn = Mul(Transpose(RotationOfBasis(clean)), RotationOfBasis(drawn));
    const Mat3 expectedRot = Mul(RotationOf(cleanWorld), turn);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) near(world.entry[r][c], expectedRot.m[r][c], "node turns with the head");

    // Measured in the camera's own axes, the node sits where it sat before.
    for (int i = 0; i < 3; ++i) {
        float cleanOffset[3], drawnOffset[3];
        for (int k = 0; k < 3; ++k) {
            cleanOffset[k] = cleanWorld.entry[3][k] - clean.e[k];
            drawnOffset[k] = world.entry[3][k] - drawn.e[k];
        }
        const float* cleanAxis = i == 0 ? clean.f : i == 1 ? clean.u : clean.r;
        const float* drawnAxis = i == 0 ? drawn.f : i == 1 ? drawn.u : drawn.r;
        near(Dot3(drawnOffset, drawnAxis), Dot3(cleanOffset, cleanAxis), "node keeps its place relative to the eye");
    }

    // A pure yaw of 0.4 rad scaled by 1.5 lands the node where a 0.6 rad yaw would.
    CameraBasis yawed = clean;
    RotateBasis(yawed, up, 0.4f);
    CameraBasis yawedFurther = clean;
    RotateBasis(yawedFurther, up, 0.6f);
    const NiMatrix44 scaled = HeadAttachedLocal(light, parent, clean, yawed, 1.5f);
    const NiMatrix44 reference = HeadAttachedLocal(light, parent, clean, yawedFurther, 1.0f);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) near(scaled.entry[r][c], reference.entry[r][c], "1.5x turn rotates like the larger turn");

    // Scaling leaves the axis alone, so a combined turn scaled by 1.5 and then by
    // 2/3 comes back to itself.
    const Mat3 combined = Mul(Transpose(RotationOfBasis(clean)), RotationOfBasis(drawn));
    const Mat3 roundTrip = ScaleTurn(ScaleTurn(combined, 1.5f), 1.0f / 1.5f);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) near(roundTrip.m[r][c], combined.m[r][c], "scaling keeps the turn's axis");

    // The lean is not scaled: with no turn, the node moves exactly as far as the eye.
    CameraBasis leaned = clean;
    leaned.e[1] += 0.3f;
    const NiMatrix44 leanWorld = Compose(HeadAttachedLocal(light, parent, clean, leaned, 1.5f), parent);
    near(leanWorld.entry[3][1] - cleanWorld.entry[3][1], 0.3f, "lean carried at 1:1");

    // LightMultiplier=0 keeps the node on the aim and still carries it with the eye.
    const NiMatrix44 pinned = Compose(HeadAttachedLocal(light, parent, clean, drawn, 0.0f), parent);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) near(pinned.entry[r][c], cleanWorld.entry[r][c], "0x turn keeps the node's rotation");
    for (int i = 0; i < 3; ++i) {
        near(pinned.entry[3][i] - cleanWorld.entry[3][i], drawn.e[i] - clean.e[i], "0x turn still carries the lean");
    }

    if (failures == 0) std::printf("head_attached: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
