#include <cmath>
#include <cstdio>
#include "game/aim_projection.h"

using namespace StarfieldHT;

int main() {
    int failures = 0;
    const auto check = [&](bool passed, const char* message) {
        if (!passed) { std::printf("FAIL: %s\n", message); ++failures; }
    };
    const auto near = [&](float actual, float expected, const char* message) {
        check(std::fabs(actual - expected) < 0.00001f, message);
    };
    CameraFrame frame{};
    frame.niCamera = 1;
    frame.clean = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, 0, 0}};
    frame.drawn = frame.clean;
    frame.frustumRight = 1;
    frame.frustumTop = 0.5f;
    float x = 0, y = 0;
    check(ProjectAimAtDistance(frame, 2, x, y), "neutral target visible");
    near(x, 0, "neutral horizontal aim unchanged");
    near(y, 0, "neutral vertical aim unchanged");

    frame.drawn.e[0] = 0.2f;
    frame.drawn.e[2] = 0.1f;
    check(ProjectAimAtDistance(frame, 2, x, y), "leaned target visible");
    near(x * 960, -96, "20 cm lean at 2 m shifts reticle 96 pixels left");
    near(y, -0.1f, "upward lean shifts reticle down");
    check(ProjectAimAtDistance(frame, 4, x, y), "farther target visible");
    near(x * 960, -48, "doubling target distance halves parallax");

    frame.drawn.e[1] = 1;
    check(ProjectAimAtDistance(frame, 2, x, y), "forward lean shortens target distance");
    near(x, -0.2f, "forward lean increases horizontal parallax");
    frame.drawn.e[1] = 3;
    check(!ProjectAimAtDistance(frame, 2, x, y), "target behind leaned eye is hidden");

    frame.drawn = frame.clean;
    const float up[] = {0, 0, 1};
    RotateBasis(frame.drawn, up, 0.3f);
    check(ProjectAimAtDistance(frame, 2, x, y), "rotated target visible");
    const float rotatedX = x;
    check(ProjectAimAtDistance(frame, 2000, x, y), "distant rotated target visible");
    near(x, rotatedX, "rotation without lean remains independent of target distance");

    frame.drawn = frame.clean;
    frame.clean.e[0] = 100000;
    frame.drawn.e[0] = 100000.25f;
    check(ProjectAimAtDistance(frame, 2, x, y), "translated world origin visible");
    near(x, -0.125f, "parallax depends on relative eyes, not world origin");
    check(!ProjectAimAtDistance(frame, 0, x, y), "zero target distance rejected");
    frame.niCamera = 0;
    check(!ProjectAimAtDistance(frame, 2, x, y), "inactive frame rejected");
    std::printf("Aim projection: %d failures\n", failures);
    return failures ? 1 : 0;
}
