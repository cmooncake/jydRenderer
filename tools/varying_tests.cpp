#include "shader.hpp"
#include "test_support.hpp"
#include <iostream>

int main() {
    try {
        using namespace jyd;
        Commonv2f v[3]{};
        v[0].position = vec4(0, 0, 0, 1);
        v[1].position = vec4(0, 0, 0, 2);
        v[2].position = vec4(0, 0, 0, 4);
        v[0].worldPosition = vec3(0, 0, 0);
        v[1].worldPosition = vec3(2, 0, 0);
        v[2].worldPosition = vec3(0, 4, 0);
        v[0].texcoord = vec2(0, 0);
        v[1].texcoord = vec2(1, 0);
        v[2].texcoord = vec2(0, 1);
        for (auto& x : v) x.normal = vec3(0, 0, 2);
        const auto f = VaryingTraits<Commonv2f>::interpolateFragment(
            v, 1.0f / 3, 1.0f / 3, 1.0f / 3);
        check(near(f.worldPosition.x, 4.0f / 7) &&
              near(f.worldPosition.y, 4.0f / 7) &&
              near(f.worldPosition.z, 0), "perspective world position");
        check(near(f.texcoord.x, 2.0f / 7) && near(f.texcoord.y, 1.0f / 7),
              "perspective UV");
        check(near(f.normal.z, 2), "do not normalize during interpolation");
        const auto edge = VaryingTraits<Commonv2f>::interpolateEdge(v[0], v[1], 0.25f);
        check(near(edge.worldPosition.x, 0.5f) &&
              near(edge.position.w, 1.25f) && near(edge.texcoord.x, 0.25f) &&
              near(edge.normal.z, 2), "clip edge interpolation");

        // Unequal normals catch an omitted normal weight, even with equal UVs.
        v[0].normal = vec3(2, 0, 0);
        v[1].normal = vec3(0, 3, 0);
        v[2].normal = vec3(0, 0, 4);
        const auto g = VaryingTraits<Commonv2f>::interpolateFragment(
            v, 1.0f / 3, 1.0f / 3, 1.0f / 3);
        check(near(g.normal.x, 8.0f / 7) && near(g.normal.y, 6.0f / 7) &&
              near(g.normal.z, 4.0f / 7), "perspective raw normal");
        const auto e = VaryingTraits<Commonv2f>::interpolateEdge(v[0], v[1], 0.25f);
        check(near(e.normal.x, 1.5f) && near(e.normal.y, 0.75f), "edge raw normal");
        std::cout << "varying tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
