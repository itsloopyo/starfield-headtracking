#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "game/weapon_projection.h"

using namespace StarfieldHT;

namespace {
int failures = 0;
void Near(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) < 0.0002f) return;
    std::printf("FAIL: %s: %.6f != %.6f\n", message, actual, expected);
    ++failures;
}
}

int main() {
    CameraBasis clean{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {12, -7, 3}};
    const float up[3] = {0, 0, 1};
    RotateBasis(clean, up, 0.6f);
    float right[3] = {clean.r[0], clean.r[1], clean.r[2]};
    RotateBasis(clean, right, -0.3f);
    for (const float scaleX : {0.7f, 1.0f, 1.5f}) {
        for (const float scaleY : {0.8f, 1.0f, 1.5f}) {
            for (const float angle : {-0.5f, 0.0f, 0.5f}) {
                for (const float lean : {-0.2f, 0.0f, 0.2f}) {
                    CameraBasis drawn = clean;
                    RotateBasis(drawn, up, angle);
                    float axis[3] = {drawn.r[0], drawn.r[1], drawn.r[2]};
                    RotateBasis(drawn, axis, angle * 0.4f);
                    for (int i = 0; i < 3; ++i) axis[i] = drawn.f[i];
                    RotateBasis(drawn, axis, angle * 0.7f);
                    for (int i = 0; i < 3; ++i) drawn.e[i] += lean * (clean.r[i] + clean.u[i] + clean.f[i]);
                    float eye[3];
                    NiMatrix44 view{}, inverse{};
                    CompensateWeaponProjection(clean, drawn, scaleX, scaleY, eye, view, inverse);
                    for (int i = 0; i < 3; ++i) Near(eye[i], clean.e[i], "weapon is drawn from the clean eye");
                    // The caller memcpys all 64 bytes of each matrix over the
                    // game's own, so the row and column the rotation does not
                    // write are part of the contract: a translation left in
                    // either would be a second, uncorrected eye offset applied
                    // on top of the one in the camera record.
                    for (int i = 0; i < 3; ++i) {
                        Near(view.entry[3][i], 0.0f, "corrected view has no translation row");
                        Near(view.entry[i][3], 0.0f, "corrected view has no projection column");
                        Near(inverse.entry[3][i], 0.0f, "inverse view has no translation row");
                        Near(inverse.entry[i][3], 0.0f, "inverse view has no projection column");
                    }
                    Near(view.entry[3][3], 1.0f, "corrected view is affine");
                    Near(inverse.entry[3][3], 1.0f, "inverse view is affine");

                    const Mat3 identity = Mul(RotationOf(view), RotationOf(inverse));
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            Near(identity.m[i][j], i == j ? 1.0f : 0.0f, "view inverse");
                    for (const float x : {-0.2f, 0.0f, 0.2f}) {
                        for (const float y : {-0.15f, 0.0f, 0.15f}) {
                            float relative[3], physical[3];
                            for (int i = 0; i < 3; ++i) {
                                relative[i] = clean.e[i] + x * clean.r[i] + y * clean.u[i] + 2 * clean.f[i] - eye[i];
                                physical[i] = x * scaleX * clean.r[i] + y * scaleY * clean.u[i]
                                            + 2 * clean.f[i];
                            }
                            float actual[3];
                            MulRowVec(relative, RotationOf(view), actual);
                            const float worldRight = 1.8f, worldTop = 0.5f;
                            Near(actual[0] / actual[2] / (worldRight / scaleX),
                                 Dot3(physical, drawn.r) / Dot3(physical, drawn.f) / worldRight,
                                 "weapon horizontal projection is the world's, seen from the clean eye");
                            Near(actual[1] / actual[2] / (worldTop / scaleY),
                                 Dot3(physical, drawn.u) / Dot3(physical, drawn.f) / worldTop,
                                 "weapon vertical projection is the world's, seen from the clean eye");
                            if (angle == 0 && lean == 0) {
                                Near(actual[0] / actual[2], x / 2, "neutral sight picture X");
                                Near(actual[1] / actual[2], y / 2, "neutral sight picture Y");
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("Weapon projection: %d failures\n", failures);
    return failures ? 1 : 0;
}
