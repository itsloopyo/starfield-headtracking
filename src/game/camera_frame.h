#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "camera_math.h"

namespace StarfieldHT {

struct CameraFrame {
    uintptr_t niCamera;
    CameraBasis clean;
    CameraBasis drawn;
    float frustumRight;
    float frustumTop;
    float frustumNear;
    NiMatrix44 local;
};

inline CameraFrame RebaseCameraFrame(const CameraFrame& reference, const CameraBasis& drawn) {
    CameraFrame result = reference;
    // The local head transform is unchanged when the scene graph moves its
    // parent. Carry the clean basis and lean through that same parent change.
    const Mat3 oldDrawnInverse = Transpose(RotationOfBasis(reference.drawn));
    const Mat3 parentDelta = Mul(oldDrawnInverse, RotationOfBasis(drawn));
    float lean[3], transformedLean[3], eye[3];
    for (int i = 0; i < 3; ++i) lean[i] = reference.drawn.e[i] - reference.clean.e[i];
    MulRowVec(lean, parentDelta, transformedLean);
    for (int i = 0; i < 3; ++i) eye[i] = drawn.e[i] - transformedLean[i];
    BasisFromRotation(Mul(RotationOfBasis(reference.clean), parentDelta), eye, result.clean);
    result.drawn = drawn;
    return result;
}

// How closely a render-side basis has to match a published one to be the same
// camera. Both are squared errors: the position one admits 10 mm of eye travel,
// and the rotation one is a squared Frobenius norm over the three basis
// vectors, which for a rotation of angle t is 8*sin(t/2)^2 and so admits 0.81
// degrees. Wide enough that two consecutive frames of ordinary mouse movement
// can both fall inside, which is why Find takes the strictly closest candidate
// and breaks ties toward the newest rather than accepting the first match.
// Widening them further was measured and rejected: rejected combined-motion
// frames sat at 0.00047, so the next notch up starts choosing an approximate
// clean aim over none.
inline constexpr float kMaxPositionErrorSq = 0.0001f;
inline constexpr float kMaxRotationErrorSq = 0.0004f;

// Larger than any error a candidate inside those bounds can reach, so the first
// match always wins the "closest so far" comparison.
inline constexpr float kNoCandidateError = 1.0f;

class CameraFrameHistory {
public:
    void Publish(const CameraFrame& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        frames_[next_] = frame;
        next_ = (next_ + 1) % frames_.size();
    }

    bool Latest(CameraFrame& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const CameraFrame& frame = frames_[(next_ + frames_.size() - 1) % frames_.size()];
        if (frame.niCamera == 0) return false;
        out = frame;
        return true;
    }

    bool FindLocal(uintptr_t camera, const NiMatrix44& local, CameraFrame& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t age = 0; age < frames_.size(); ++age) {
            const auto& frame = frames_[(next_ + frames_.size() - 1 - age) % frames_.size()];
            if (frame.niCamera == camera && std::memcmp(&frame.local, &local, sizeof(local)) == 0) {
                out = frame;
                return true;
            }
        }
        return false;
    }

    bool Find(const CameraBasis& drawn, CameraFrame& out,
              float maxRotationErrorSq = kMaxRotationErrorSq) const {
        std::lock_guard<std::mutex> lock(mutex_);
        bool found = false;
        float bestError = kNoCandidateError;
        for (size_t age = 0; age < frames_.size(); ++age) {
            const auto& frame = frames_[(next_ + frames_.size() - 1 - age) % frames_.size()];
            if (!frame.niCamera) continue;
            float positionError = 0, rotationError = 0;
            for (int i = 0; i < 3; ++i) {
                const float e = drawn.e[i] - frame.drawn.e[i];
                const float r = drawn.r[i] - frame.drawn.r[i];
                const float u = drawn.u[i] - frame.drawn.u[i];
                const float f = drawn.f[i] - frame.drawn.f[i];
                positionError += e * e;
                rotationError += r * r + u * u + f * f;
            }
            const float error = positionError + rotationError;
            if (positionError < kMaxPositionErrorSq && rotationError < maxRotationErrorSq
                && error < bestError) {
                out = frame;
                bestError = error;
                found = true;
            }
        }
        return found;
    }

private:
    // Rendering can consume an older simulation snapshot. Keep its clean basis
    // until it ages out, including while a newer update disables tracking.
    std::array<CameraFrame, 32> frames_{};
    size_t next_ = 0;
    mutable std::mutex mutex_;
};

}
