#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace ShipFlyCamera {
struct FrameInput {
    int mouseX;
    int mouseY;
    int forward;
    int right;
    int up;
    bool boost;
};

struct Pose {
    std::array<float, 3> eye{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 1.0f;
};

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kMouseScale = 0.08f * kPi / 180.0f;
constexpr float kPitchLimit = 89.0f * kPi / 180.0f;
constexpr float kFlightSpeed = 240.0f;

inline bool Seed(Pose& pose, const float* eye, const float* at) {
    const float dx = at[0] - eye[0];
    const float dy = at[1] - eye[1];
    const float dz = at[2] - eye[2];
    const float horizontal = std::hypot(dx, dz);
    const float distance = std::hypot(horizontal, dy);
    if (!std::isfinite(distance) || distance <= 0.01f) {
        return false;
    }
    pose = { { eye[0], eye[1], eye[2] }, std::atan2(dx, dz), std::atan2(dy, horizontal), distance };
    return true;
}

inline void Update(Pose& pose, const FrameInput& input, float seconds, bool mirrored, float* at) {
    const float horizontalSign = mirrored ? -1.0f : 1.0f;
    pose.yaw = std::remainder(pose.yaw - input.mouseX * kMouseScale * horizontalSign, 2.0f * kPi);
    if (input.mouseY != 0) {
        pose.pitch = std::clamp(pose.pitch - input.mouseY * kMouseScale, -kPitchLimit, kPitchLimit);
    }

    const float sinYaw = std::sin(pose.yaw);
    const float cosYaw = std::cos(pose.yaw);
    const float sinPitch = std::sin(pose.pitch);
    const float cosPitch = std::cos(pose.pitch);

    // Match the game's rendered view: right = look direction cross world up.
    const float right = input.right * horizontalSign;
    const std::array<float, 3> velocity = {
        input.forward * sinYaw * cosPitch - right * cosYaw,
        input.forward * sinPitch + input.up,
        input.forward * cosYaw * cosPitch + right * sinYaw,
    };
    const float magnitude = std::hypot(velocity[0], velocity[1], velocity[2]);
    const float step = std::clamp(seconds, 0.0f, 0.1f) * kFlightSpeed * (input.boost ? 3.0f : 1.0f) /
                       std::max(1.0f, magnitude);
    for (size_t axis = 0; axis < pose.eye.size(); ++axis) {
        pose.eye[axis] += velocity[axis] * step;
    }

    at[0] = pose.eye[0] + sinYaw * cosPitch * pose.distance;
    at[1] = pose.eye[1] + sinPitch * pose.distance;
    at[2] = pose.eye[2] + cosYaw * cosPitch * pose.distance;
}
} // namespace ShipFlyCamera

