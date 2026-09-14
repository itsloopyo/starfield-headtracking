#pragma once

#include "game/camera_math.h"

namespace StarfieldHT {

inline Mat3 RotationAboutAxis(const float axis[3], float angle) {
    Mat3 out{};
    for (int r = 0; r < 3; ++r) {
        float row[3] = {0.0f, 0.0f, 0.0f};
        row[r] = 1.0f;
        RotateAboutAxis(row, axis, angle);
        for (int c = 0; c < 3; ++c) out.m[r][c] = row[c];
    }
    return out;
}

// The same turn about the same axis, through `scale` times the angle.
inline Mat3 ScaleTurn(const Mat3& turn, float scale) {
    float axis[3] = {
        turn.m[1][2] - turn.m[2][1],
        turn.m[2][0] - turn.m[0][2],
        turn.m[0][1] - turn.m[1][0],
    };
    const float skew = sqrtf(Dot3(axis, axis));
    if (skew < 1e-6f) return turn;
    for (int i = 0; i < 3; ++i) axis[i] /= skew;
    const float angle = atan2f(0.5f * skew, 0.5f * (turn.m[0][0] + turn.m[1][1] + turn.m[2][2] - 1.0f));
    return RotationAboutAxis(axis, angle * scale);
}

// The local transform that carries a node rigidly with the head: whatever turn
// and lean took the clean camera to the drawn one is applied to the node's
// clean world transform about the eye, then expressed back in its parent's
// frame, since world = local * parentWorld and that is what the node stores.
// `turnScale` multiplies the head's turn; the lean is carried at 1:1.
inline NiMatrix44 HeadAttachedLocal(const NiMatrix44& cleanLocal, const NiMatrix44& parentWorld,
                                    const CameraBasis& clean, const CameraBasis& drawn,
                                    float turnScale) {
    const Mat3 parentRot = RotationOf(parentWorld);
    const Mat3 parentInv = Transpose(parentRot);
    const Mat3 headTurn =
        ScaleTurn(Mul(Transpose(RotationOfBasis(clean)), RotationOfBasis(drawn)), turnScale);

    float cleanPos[3];
    MulRowVec(cleanLocal.entry[3], parentRot, cleanPos);
    float fromEye[3];
    for (int i = 0; i < 3; ++i) fromEye[i] = cleanPos[i] + parentWorld.entry[3][i] - clean.e[i];
    float turnedFromEye[3];
    MulRowVec(fromEye, headTurn, turnedFromEye);
    float fromParent[3];
    for (int i = 0; i < 3; ++i) fromParent[i] = drawn.e[i] + turnedFromEye[i] - parentWorld.entry[3][i];

    const Mat3 localRot = Mul(Mul(Mul(RotationOf(cleanLocal), parentRot), headTurn), parentInv);
    NiMatrix44 local = cleanLocal;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) local.entry[r][c] = localRot.m[r][c];
    MulRowVec(fromParent, parentInv, local.entry[3]);
    return local;
}

} // namespace StarfieldHT
