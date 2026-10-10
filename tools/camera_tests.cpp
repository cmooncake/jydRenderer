#include "camera.hpp"
#include "test_support.hpp"
#include <iostream>

int main() {
    try {
        jyd::Camera c(800, 400, 1.57079632679f);
        const mat4 p = c.perspProjection();
        const vec4 nearClip = p * vec4(0, 0, -c.n_, 1);
        const vec4 farClip = p * vec4(0, 0, -c.f_, 1);
        check(near(nearClip.w, c.n_), "near clip w");
        check(near(nearClip.z / nearClip.w, -1), "near NDC z");
        check(near(farClip.z / farClip.w, 1), "far NDC z");
        const vec4 top = p * vec4(0, c.t_, -c.n_, 1);
        check(near(top.y / top.w, 1), "vertical FOV edge");

        // 从 NDC 反推方向，再把射线上的点投影回来。
        const float nx = 0.5f, ny = -0.25f;
        const float h = std::tan(c.fovY_ * 0.5f);
        const vec3 direction = normalize(vec3(
            nx * c.aspect_ratio_ * h, ny * h, -1));
        const vec4 q = p * vec4(direction * 4.0f, 1);
        check(near(q.x / q.w, nx), "ray/project round trip x");
        check(near(q.y / q.w, ny), "ray/project round trip y");
        std::cout << "camera tests passed\n";
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}