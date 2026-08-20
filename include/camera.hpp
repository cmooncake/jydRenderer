#pragma once

#include "vec.hpp"
#include <algorithm>

namespace jyd {

struct Camera {
    vec3 position_{0.0f, 0.0f, 0.0f};
    vec3 lookAtDirection_{0.0f, 0.0f, -1.0f};
    vec3 upDirection_{0.0f, 1.0f, 0.0f};

    float l_ = -1.0f;
    float r_ = 1.0f;
    float b_ = -1.0f;
    float t_ = 1.0f;
    float n_ = 1.0f;
    float f_ = 100.0f;

    float fovY_ = 1.0471976f; // 60 deg
    float aspect_ratio_ = 1.0f;

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    Camera(int width, int height, float fovYRad = 1.0471976f) {
        aspect_ratio_ = static_cast<float>(width) / static_cast<float>(height);
        fovY_ = fovYRad;
        updateFrustum();
    }

    void updateFrustum() {
        t_ = n_ * std::tan(fovY_ / 2.0f);
        b_ = -t_;
        r_ = t_ * aspect_ratio_;
        l_ = -r_;
    }

    void zoom(float wheelDelta) {
        constexpr float zoomSensitivity = 0.05f;
        constexpr float minFov = 0.261799f; // 15бу
        constexpr float maxFov = 1.570796f; // 90бу

        fovY_ = std::clamp(
            fovY_ - wheelDelta * zoomSensitivity,
            minFov,
            maxFov);

        updateFrustum();
    }

    vec3 right() const {
        return normalize(
            cross(lookAtDirection_, vec3(0.0f, 1.0f, 0.0f))
        );
    }

    void rotate(float deltaX, float deltaY) {
        constexpr float sensitivity = 0.005f;
        constexpr float pitchLimit = 1.553343f; // 89 degrees

        yaw_ += deltaX * sensitivity;
        pitch_ -= deltaY * sensitivity;
        pitch_ = std::clamp(pitch_, -pitchLimit, pitchLimit);

        lookAtDirection_ = normalize(vec3(
            std::sin(yaw_) * std::cos(pitch_),
            std::sin(pitch_),
            -std::cos(yaw_) * std::cos(pitch_)
        ));

        const vec3 worldUp(0.0f, 1.0f, 0.0f);
        const vec3 rightDirection =
            normalize(cross(lookAtDirection_, worldUp));

        upDirection_ =
            normalize(cross(rightDirection, lookAtDirection_));
    }

    void moveForward(float d) {
        position_ = position_ + lookAtDirection_ * d;
    }

    void moveBackward(float d) {
        position_ = position_ - lookAtDirection_ * d;
    }

    void strafeLeft(float d) {
        position_ = position_ - right() * d;
    }

    void strafeRight(float d) {
        position_ = position_ + right() * d;
    }

    void moveUp(float d) {
        position_ = position_ + upDirection_ * d;
    }

    void moveDown(float d) {
        position_ = position_ - upDirection_ * d;
    }

    mat4 viewTransformation() const {
        mat4 t_view = identity<4>();
        t_view[0][3] = -position_[0];
        t_view[1][3] = -position_[1];
        t_view[2][3] = -position_[2];

        const vec3 f = normalize(lookAtDirection_);
        const vec3 r = right();
        const vec3 u = normalize(upDirection_);

        mat4 r_view = identity<4>();
        r_view[0][0] = r[0];
        r_view[0][1] = r[1];
        r_view[0][2] = r[2];
        r_view[1][0] = u[0];
        r_view[1][1] = u[1];
        r_view[1][2] = u[2];
        r_view[2][0] = -f[0];
        r_view[2][1] = -f[1];
        r_view[2][2] = -f[2];

        return r_view * t_view;
    }

    mat4 orthoProjection() const {
        mat4 translate = identity<4>();
        translate[0][3] = -(r_ + l_) / 2.0f;
        translate[1][3] = -(t_ + b_) / 2.0f;
        translate[2][3] = -(n_ + f_) / 2.0f;

        mat4 scale = identity<4>();
        scale[0][0] = 2.0f / (r_ - l_);
        scale[1][1] = 2.0f / (t_ - b_);
        scale[2][2] = 2.0f / (n_ - f_);

        return scale * translate;
    }

    mat4 perspProjection() const {
        mat4 proj = identity<4>();
        proj[0][0] = 2.0f * n_ / (r_ - l_);
        proj[1][1] = 2.0f * n_ / (t_ - b_);
        proj[0][2] = (r_ + l_) / (r_ - l_);
        proj[1][2] = (t_ + b_) / (t_ - b_);
        proj[2][2] = -(f_ + n_) / (f_ - n_);
        proj[2][3] = -2.0f * f_ * n_ / (f_ - n_);
        proj[3][2] = -1.0f;
        return proj;
    }

    mat4 projectionMatrix() const {
        return perspProjection();
    }
};

} // namespace jyd
