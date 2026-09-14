#include <cmath>
#include <cstdio>
#include "game/impact_projection.h"

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
    const float impact[] = {2, 10, 1};
    float x = 0, y = 0;
    check(ProjectImpact(frame, impact, x, y), "off-axis impact visible");
    near(x, 0.2f, "impact retains its horizontal offset from aim");
    near(y, 0.2f, "impact retains its vertical offset from aim");

    const float up[] = {0, 0, 1};
    RotateBasis(frame.clean, up, 0.5f);
    check(ProjectImpact(frame, impact, x, y), "impact survives a new aim direction");
    near(x, 0.2f, "moving aim does not drag an existing impact");
    near(y, 0.2f, "moving aim does not move impact height");

    const float angle = 0.3f;
    RotateBasis(frame.drawn, up, angle);
    check(ProjectImpact(frame, impact, x, y), "head yaw keeps world impact visible");
    near(x, (2 * std::cos(angle) + 10 * std::sin(angle)) /
            (10 * std::cos(angle) - 2 * std::sin(angle)), "head yaw projects the fixed impact");

    frame.drawn = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0.2f, 1, 0.1f}};
    float relative[3];
    for (int i = 0; i < 3; ++i) relative[i] = impact[i] - frame.drawn.e[i];
    check(ProjectImpact(frame, relative, x, y), "leaned impact visible");
    near(x, 1.8f / 9, "lateral and forward lean change impact parallax");
    near(y, 0.9f / 9 / 0.5f, "vertical lean changes impact parallax");

    frame.drawn = {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 0}};
    check(ProjectImpact(frame, impact, x, y), "rolled impact visible");
    near(x, -0.1f, "roll moves impact height into screen horizontal");
    near(y, 0.4f, "roll moves lateral impact into screen vertical");

    const float behind[] = {2, -10, 1};
    check(!ProjectImpact(frame, behind, x, y), "impact behind the eye is hidden");
    frame.niCamera = 0;
    check(!ProjectImpact(frame, impact, x, y), "inactive camera rejected");
    std::printf("Impact projection: %d failures\n", failures);
    return failures ? 1 : 0;
}
