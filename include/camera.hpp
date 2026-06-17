#pragma once

#include "vec.hpp"

namespace jyd {

struct Camera {
    vec3 position_{0.0, 0.0, 0.0};
    vec3 lookAtDirection_{0.0, 0.0, -1.0};
    vec3 upDirection_{0.0, 1.0, 0.0};

    double l_ = -1.0;
    double r_ = 1.0;
    double b_ = -1.0;
    double t_ = 1.0;
    double n_ = 1.0;
    double f_ = 100.0;

    double fovY_ = 1.0471975511965976; // 60 deg
    double aspect_ratio_ = 1.0;

    Camera(int width, int height, double fovYRad = 1.0471975511965976) {
        aspect_ratio_ = static_cast<double>(width) / static_cast<double>(height);
        fovY_ = fovYRad;
        updateFrustum();
    }

    void updateFrustum() {
        t_ = n_ * std::tan(fovY_ / 2.0);
        b_ = -t_;
        r_ = t_ * aspect_ratio_;
        l_ = -r_;
    }

    vec3 right() const {
        return normalize(cross(lookAtDirection_, upDirection_));
    }

    void moveForward(double d) {
        position_ = position_ + lookAtDirection_ * d;
    }

    void moveBackward(double d) {
        position_ = position_ - lookAtDirection_ * d;
    }

    void strafeLeft(double d) {
        position_ = position_ - right() * d;
    }

    void strafeRight(double d) {
        position_ = position_ + right() * d;
    }

    void moveUp(double d) {
        position_ = position_ + upDirection_ * d;
    }

    void moveDown(double d) {
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
        translate[0][3] = -(r_ + l_) / 2.0;
        translate[1][3] = -(t_ + b_) / 2.0;
        translate[2][3] = -(n_ + f_) / 2.0;

        mat4 scale = identity<4>();
        scale[0][0] = 2.0 / (r_ - l_);
        scale[1][1] = 2.0 / (t_ - b_);
        scale[2][2] = 2.0 / (n_ - f_);

        return scale * translate;
    }

    mat4 perspProjection() const {
        mat4 proj = identity<4>();
        proj[0][0] = 2.0 * n_ / (r_ - l_);
        proj[1][1] = 2.0 * n_ / (t_ - b_);
        proj[0][2] = (r_ + l_) / (r_ - l_);
        proj[1][2] = (t_ + b_) / (t_ - b_);
        proj[2][2] = -(f_ + n_) / (f_ - n_);
        proj[2][3] = -2.0 * f_ * n_ / (f_ - n_);
        proj[3][2] = -1.0;
        return proj;
    }

    mat4 projectionMatrix() const {
        return perspProjection();
    }
};

} // namespace jyd
