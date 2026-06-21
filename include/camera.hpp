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

    void rotate(double deltaX, double deltaY) {
        const double sensitivity = 0.005;
        const double yaw = deltaX * sensitivity;
        const double pitch = -deltaY * sensitivity;

        // 获取当前右向量（单位向量），用于俯仰旋转的轴
        vec3 r = right();
        r = normalize(r);  // 确保归一化

        // 罗德里格旋转公式：将向量 v 绕单位轴 axis 旋转 angle 弧度
        auto rotateVector = [](const vec3& v, const vec3& axis, double angle) -> vec3 {
            double c = cos(angle);
            double s = sin(angle);
            double k = 1.0 - c;
            return v * c + cross(axis, v) * s + axis * (axis*v) * k;
            };

        // 1. 偏航：绕世界 Y 轴旋转
        vec3 worldUp = vec3(0.0, 1.0, 0.0);
        lookAtDirection_ = rotateVector(lookAtDirection_, worldUp, yaw);
        upDirection_ = rotateVector(upDirection_, worldUp, yaw);  // 实际 up 不变，但保留通用性

        // 2. 俯仰：绕局部右向量旋转（重新计算 r，因为 lookAt 变了）
        r = right();
        r = normalize(r);
        lookAtDirection_ = rotateVector(lookAtDirection_, r, pitch);
        upDirection_ = rotateVector(upDirection_, r, pitch);

        // 3. 正交化与归一化（保证基向量相互垂直且单位长度）
        lookAtDirection_ = normalize(lookAtDirection_);
        // 用格拉姆-施密特将 up 投影到垂直于 lookAt 的方向
        upDirection_ = upDirection_ - (upDirection_* lookAtDirection_) * lookAtDirection_;
        upDirection_ = normalize(upDirection_);

        // 4. 防止俯仰过度导致“翻跟头”（限制视线与 Y 轴的夹角）
        // 若视线方向接近垂直，则裁剪到 ±89° 等效范围
        const double limit = 0.99;
        if (fabs(lookAtDirection_.y) > limit) {
            lookAtDirection_.y = (lookAtDirection_.y > 0) ? limit : -limit;
            lookAtDirection_ = normalize(lookAtDirection_);
            // 重新正交化 up
            upDirection_ = vec3(0.0, 1.0, 0.0);  // 或者重新计算，这里简单重置
            upDirection_ = upDirection_ - (upDirection_* lookAtDirection_) * lookAtDirection_;
            upDirection_ = normalize(upDirection_);
        }
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
