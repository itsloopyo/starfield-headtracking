#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "game/weapon_projection.h"
#include "game/aim_projection.h"
#include "game/laser_beam.h"

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
                    // Sights locked draws the weapon from the clean eye under any lean
                    // at the hip; true free look from the tracked eye, with the same
                    // rotation, sights up or down.
                    float freeEye[3];
                    NiMatrix44 freeView{}, freeInverse{};
                    for (const float sightsUp : {0.0f, 0.5f, 1.0f}) {
                        CompensateWeaponProjection(clean, drawn, scaleX, scaleY, 0.0f, sightsUp, freeEye, freeView, freeInverse);
                        for (int i = 0; i < 3; ++i) Near(freeEye[i], drawn.e[i], "true free look draws from the tracked eye");
                    }
                    float eye[3];
                    NiMatrix44 view{}, inverse{};
                    // Sights up, sights locked keeps the lean along the aim and takes
                    // out the rest, so leaning in brings the sights closer while the
                    // eye stays on the sight line.
                    NiMatrix44 upView{}, upInverse{};
                    for (const float sightsUp : {0.5f, 1.0f}) {
                        CompensateWeaponProjection(clean, drawn, scaleX, scaleY, 1.0f, sightsUp, eye, upView, upInverse);
                        float fromClean[3];
                        for (int i = 0; i < 3; ++i) fromClean[i] = eye[i] - clean.e[i];
                        Near(Dot3(fromClean, clean.f), lean * sightsUp,
                             "sights locked, sights up: the weapon's eye keeps the lean along the aim");
                        Near(Dot3(fromClean, clean.r), 0.0f, "sights locked, sights up: no sideways lean reaches the weapon's eye");
                        Near(Dot3(fromClean, clean.u), 0.0f, "sights locked, sights up: no vertical lean reaches the weapon's eye");
                    }
                    CompensateWeaponProjection(clean, drawn, scaleX, scaleY, 1.0f, 0.0f, eye, view, inverse);
                    for (int i = 0; i < 3; ++i) Near(eye[i], clean.e[i], "sights locked draws from the clean eye at the hip");
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 4; ++j) {
                            Near(upView.entry[i][j], view.entry[i][j], "raising the sights leaves the weapon view rotation alone");
                        }
                    }
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 4; ++j) {
                            Near(freeView.entry[i][j], view.entry[i][j], "true free look keeps the weapon view rotation");
                            Near(freeInverse.entry[i][j], inverse.entry[i][j], "true free look keeps the inverse view");
                        }
                    }
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

    for (const float zoom : {1.0f, 0.7f}) {
        for (const float distance : {2.0f, 20.0f, 200.0f}) {
            CameraFrame frame{};
            frame.niCamera = 1;
            frame.clean = clean;
            frame.drawn = clean;
            RotateBasis(frame.drawn, up, 0.25f);
            RotateBasis(frame.drawn, frame.drawn.f, 0.15f);
            for (int i = 0; i < 3; ++i)
                frame.drawn.e[i] += 0.06f * clean.r[i] + 0.07f * clean.u[i] - 0.02f * clean.f[i];
            frame.frustumRight = 1.8f * zoom;
            frame.frustumTop = 0.5f * zoom;
            const float weaponRight = 0.8f, weaponTop = 0.4f;
            float eye[3];
            NiMatrix44 view{}, inverse{};
            CompensateWeaponProjection(clean, frame.drawn,
                frame.frustumRight / weaponRight, frame.frustumTop / weaponTop, 1.0f, 0.0f, eye, view, inverse);
            WeaponPassView pass{frame.frustumRight / weaponRight, frame.frustumTop / weaponTop, 1.0f};
            if (!AlignWeaponAim(frame, distance, weaponRight, weaponTop, pass, view, inverse)) {
                std::printf("FAIL: finite target alignment rejected\n");
                ++failures;
            }
            // A point of the weapon, handed to the world pass, lands on the pixel
            // the weapon pass draws it on.
            for (const float side : {-0.12f, 0.09f}) {
                float part[3], fromWeaponEye[3], inWeaponPass[3], inWorld[3], fromDrawnEye[3];
                for (int i = 0; i < 3; ++i) {
                    part[i] = clean.e[i] + 0.42f * clean.f[i] + side * clean.r[i] - 0.1f * clean.u[i];
                    fromWeaponEye[i] = part[i] - eye[i];
                }
                MulRowVec(fromWeaponEye, RotationOf(view), inWeaponPass);
                WeaponPointInWorldPass(clean, frame.drawn, frame.frustumRight, frame.frustumTop, 0.0f, pass, part, inWorld);
                for (int i = 0; i < 3; ++i) fromDrawnEye[i] = inWorld[i] - frame.drawn.e[i];
                const float depth = Dot3(fromDrawnEye, frame.drawn.f);
                Near(Dot3(fromDrawnEye, frame.drawn.r) / depth / frame.frustumRight,
                     inWeaponPass[0] / inWeaponPass[2] / weaponRight,
                     "a weapon point in the world pass meets the weapon pass, horizontal");
                Near(Dot3(fromDrawnEye, frame.drawn.u) / depth / frame.frustumTop,
                     inWeaponPass[1] / inWeaponPass[2] / weaponTop,
                     "a weapon point in the world pass meets the weapon pass, vertical");
            }
            const Mat3 identity = Mul(RotationOf(view), RotationOf(inverse));
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    Near(identity.m[i][j], i == j ? 1.0f : 0.0f, "aligned weapon inverse");
            float sight[3], target[3];
            for (int i = 0; i < 3; ++i) {
                sight[i] = clean.e[i] + 0.35f * clean.f[i] - eye[i];
                target[i] = clean.e[i] + distance * clean.f[i] - frame.drawn.e[i];
            }
            float projected[3];
            MulRowVec(sight, RotationOf(view), projected);
            const float targetDepth = Dot3(target, frame.drawn.f);
            Near(projected[0] / projected[2] / weaponRight,
                Dot3(target, frame.drawn.r) / targetDepth / frame.frustumRight,
                "locked ADS sight follows finite-distance horizontal aim under lean");
            Near(projected[1] / projected[2] / weaponTop,
                Dot3(target, frame.drawn.u) / targetDepth / frame.frustumTop,
                "locked ADS sight follows finite-distance vertical aim under lean");
            const auto unchangedView = view;
            const auto unchangedInverse = inverse;
            if (AlignWeaponAim(frame, -1.0f, weaponRight, weaponTop, pass, view, inverse)) {
                std::printf("FAIL: invalid target depth accepted\n");
                ++failures;
            }
            WeaponPassView freeLook{pass.scaleX, pass.scaleY, 0.0f};
            AlignWeaponAim(frame, distance, weaponRight, weaponTop, freeLook, view, inverse);
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    Near(view.entry[i][j], unchangedView.entry[i][j], "rejection and free look leave view unchanged");
                    Near(inverse.entry[i][j], unchangedInverse.entry[i][j], "rejection and free look leave inverse unchanged");
                }
            }
        }
    }

    // The same holds in free look and with the sights part way up, where the
    // weapon pass has an eye of its own and no alignment shift.
    for (const float share : {0.0f, 0.4f, 1.0f}) {
        for (const float sightsUp : {0.0f, 0.6f, 1.0f}) {
            CameraBasis drawn = clean;
            RotateBasis(drawn, up, -0.3f);
            for (int i = 0; i < 3; ++i) drawn.e[i] += 0.2f * clean.r[i] - 0.05f * clean.u[i] + 0.1f * clean.f[i];
            const float worldRight = 0.9f, worldTop = 0.5f, weaponRight = 0.64f, weaponTop = 0.36f;
            const WeaponPassView pass{worldRight / weaponRight, worldTop / weaponTop, share};
            float eye[3], part[3], fromWeaponEye[3], inWeaponPass[3], inWorld[3], fromDrawnEye[3];
            NiMatrix44 view{}, inverse{};
            CompensateWeaponProjection(clean, drawn, pass.scaleX, pass.scaleY, share, sightsUp, eye, view, inverse);
            for (int i = 0; i < 3; ++i) {
                part[i] = clean.e[i] + 0.42f * clean.f[i] + 0.09f * clean.r[i] - 0.1f * clean.u[i];
                fromWeaponEye[i] = part[i] - eye[i];
            }
            MulRowVec(fromWeaponEye, RotationOf(view), inWeaponPass);
            WeaponPointInWorldPass(clean, drawn, worldRight, worldTop, sightsUp, pass, part, inWorld);
            for (int i = 0; i < 3; ++i) fromDrawnEye[i] = inWorld[i] - drawn.e[i];
            const float depth = Dot3(fromDrawnEye, drawn.f);
            Near(Dot3(fromDrawnEye, drawn.r) / depth / worldRight, inWeaponPass[0] / inWeaponPass[2] / weaponRight,
                 "world pass meets weapon pass in every aim mode, horizontal");
            Near(Dot3(fromDrawnEye, drawn.u) / depth / worldTop, inWeaponPass[1] / inWeaponPass[2] / weaponTop,
                 "world pass meets weapon pass in every aim mode, vertical");
        }
    }

    // A beam moved to a new start still ends where it ended, keeps its width and
    // stays a beam: its side axes are unit length and square to its run.
    {
        NiMatrix44 beam{};
        for (int i = 0; i < 3; ++i) {
            beam.entry[0][i] = clean.r[i];
            beam.entry[1][i] = clean.f[i] * 0.6f;
            beam.entry[2][i] = clean.u[i];
            beam.entry[3][i] = clean.e[i] + 0.4f * clean.f[i];
        }
        beam.entry[3][3] = 1.0f;
        float start[3], oldEnd[3];
        for (int i = 0; i < 3; ++i) {
            start[i] = beam.entry[3][i] + 0.2f * clean.r[i] - 0.03f * clean.u[i];
            oldEnd[i] = beam.entry[3][i] + kLaserBeamLength * beam.entry[1][i];
        }
        const NiMatrix44 moved = BeamFromStart(beam, start);
        for (int i = 0; i < 3; ++i) {
            Near(moved.entry[3][i], start[i], "the moved beam starts at the new start");
            Near(moved.entry[3][i] + kLaserBeamLength * moved.entry[1][i], oldEnd[i], "the moved beam ends where it ended");
        }
        Near(Dot3(moved.entry[0], moved.entry[0]), 1.0f, "the beam keeps its width");
        Near(Dot3(moved.entry[2], moved.entry[2]), 1.0f, "the beam keeps its height");
        Near(Dot3(moved.entry[0], moved.entry[1]), 0.0f, "the beam's side axis is square to its run");
        Near(Dot3(moved.entry[2], moved.entry[1]), 0.0f, "the beam's other side axis is square to its run");
        Near(Dot3(moved.entry[0], moved.entry[2]), 0.0f, "the beam's side axes stay square to each other");
        const NiMatrix44 same = BeamFromStart(beam, beam.entry[3]);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) Near(same.entry[i][j], beam.entry[i][j], "a beam moved nowhere is unchanged");
    }

    // The toggle slides the weapon's eye between the clean and the tracked eye
    // instead of stepping it by the whole lean, and a press mid-slide turns back
    // from where the eye is.
    {
        const float lean = 0.25f;
        CameraBasis drawn = clean;
        for (int i = 0; i < 3; ++i) drawn.e[i] += lean * clean.r[i];
        const auto eyeOffset = [&](float cleanEyeShare) {
            float eye[3];
            NiMatrix44 view{}, inverse{};
            CompensateWeaponProjection(clean, drawn, 1.0f, 1.0f, cleanEyeShare, 1.0f, eye, view, inverse);
            const float offset[3] = {eye[0] - clean.e[0], eye[1] - clean.e[1], eye[2] - clean.e[2]};
            return Dot3(offset, clean.r);
        };
        // Smoothstep's steepest slope is 1.5 over the leg, and the shorter leg is 150 ms.
        const unsigned long long kFrameMs = 16;
        const float kMaxFrameStep = lean * 1.5f * kFrameMs / 150.0f + 0.001f;
        WeaponEye weaponEye;
        unsigned long long now = 1000;
        float last = eyeOffset(weaponEye.CleanEyeShare(false, now));
        Near(last, 0.0f, "sights locked starts on the clean eye");
        const auto run = [&](bool trueFreeLook, int frames) {
            for (int frame = 0; frame < frames; ++frame) {
                now += kFrameMs;
                const float offset = eyeOffset(weaponEye.CleanEyeShare(trueFreeLook, now));
                if (std::fabs(offset - last) > kMaxFrameStep) {
                    std::printf("FAIL: the toggle stepped the weapon eye %.4f m in one frame\n", offset - last);
                    ++failures;
                }
                last = offset;
            }
        };
        run(true, 4);
        if (!(last > 0.01f && last < lean - 0.01f)) {
            std::printf("FAIL: the weapon eye is not mid-slide before the reversal: %.4f\n", last);
            ++failures;
        }
        run(false, 1);
        const float atReversal = last;
        run(false, 3);
        if (!(last < atReversal && last > 0.0f)) {
            std::printf("FAIL: the reversal did not turn back from where the eye was: %.4f -> %.4f\n",
                        atReversal, last);
            ++failures;
        }
        run(true, 60);
        Near(last, lean, "true free look settles on the tracked eye");
        run(false, 30);
        Near(last, 0.0f, "sights locked settles back on the clean eye");
    }

    std::printf("Weapon projection: %d failures\n", failures);
    return failures ? 1 : 0;
}
