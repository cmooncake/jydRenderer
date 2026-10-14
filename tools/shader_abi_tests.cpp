#include "llvm_shader.hpp"
#include "test_support.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace jyd;

// Own only this unique directory and its one generated fixture. Never remove
// a shared temporary directory or any user texture.
struct TextureFixture {
    std::filesystem::path directory;
    TextureFixture() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("jyd-abi-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) {
                directory = candidate;
                return;
            }
        }
        throw std::runtime_error("Cannot create unique test fixture directory");
    }
    TextureFixture(const TextureFixture&) = delete;
    TextureFixture& operator=(const TextureFixture&) = delete;
    ~TextureFixture() {
        std::error_code error;
        std::filesystem::remove(directory / "white.ppm", error);
        std::filesystem::remove(directory, error);
    }
    std::filesystem::path writeTexture() const {
        const auto path = directory / "white.ppm";
        std::ofstream output(path, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const unsigned char rgb[] = {255, 255, 255};
        output.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
        output.close();
        check(static_cast<bool>(output), "write white texture fixture");
        return path;
    }
};

void checkVarying(const Commonv2f& actual, const Commonv2f& expected) {
    for (int c = 0; c < 4; ++c)
        check(near(actual.position[c], expected.position[c]), "host clip position packing");
    for (int c = 0; c < 3; ++c) {
        check(near(actual.normal[c], expected.normal[c]), "host raw normal packing");
        check(near(actual.worldPosition[c], expected.worldPosition[c]), "host world position packing");
    }
    for (int c = 0; c < 2; ++c)
        check(near(actual.texcoord[c], expected.texcoord[c]), "host UV packing");
}

void testWrappers(const Texture& texture) {
    NativeCommonShader native;
    LlvmCommonShader builtin;
    LlvmCommonShader scripted("shaders/common.jydshader");
    LlvmCommonShader unlit("shaders/unlit.jydshader");
    const float matrix[4][4] = {
        {1, 2, 0, 3}, {0, 4, 5, 6}, {7, 0, 8, 9}, {0, 0, 0, 1}
    };
    const Commona2v input{vec3(1, 2, 3), vec3(2, 3, 6), vec2(0.25f, 0.75f)};
    const Commonv2f expected{vec4(8, 29, 40, 1), input.normal, input.texcoord, input.position};
    CommonShader* shaders[] = {&native, &builtin, &scripted, &unlit};
    for (auto* shader : shaders) {
        shader->texture = &texture;
        shader->cameraPosition = vec3(11, 12, 13);
        shader->SpecularLightDirection = vec3(21, 22, 23);
        shader->AmbientLightColor = vec3(41, 42, 43);
        shader->DiffuseLightDirection = vec3(0, 0, 1);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                shader->mvp[r][c] = matrix[r][c];
        shader->vp = identity<4>();
        shader->prepare();
        checkVarying(shader->vertex(input), expected);
        // Non-identity View must not leak into either world-space field.
        shader->vp[0][1] = 2;
        shader->vp[1][2] = 3;
        shader->vp[2][0] = 4;
        shader->vp[0][3] = 11;
        shader->prepare();
        checkVarying(shader->vertex(input), expected);
    }

    const vec3 normals[] = {
        {0, 0, 2}, {0, 3, 4}, {0, 0, -2}, {0, 0, 0}, {0, 0, 1e-10f},
        {std::numeric_limits<float>::infinity(), 0, 1},
        {0, std::numeric_limits<float>::quiet_NaN(), 1}, {0, 0, 1e30f}
    };
    for (const auto& normal : normals) {
        auto f = expected;
        f.normal = normal;
        Color reference{};
        check(!native.fragment(f, reference), "Native fragment discard");
        const int expectedRgb = static_cast<int>(255 *
            (std::clamp(safeNormalize(normal).z, 0.0f, 1.0f) * 0.8f + 0.2f));
        check(std::abs(static_cast<int>(reference.r) - expectedRgb) <= 1,
              "Native Common normal contract");
        for (CommonShader* shader : {static_cast<CommonShader*>(&builtin),
                                     static_cast<CommonShader*>(&scripted)}) {
            Color actual{};
            check(!shader->fragment(f, actual), "JIT fragment discard");
            check(std::abs(int(actual.r) - int(reference.r)) <= 1 &&
                  std::abs(int(actual.g) - int(reference.g)) <= 1 &&
                  std::abs(int(actual.b) - int(reference.b)) <= 1,
                  "Native/JIT Common RGB agreement");
        }
    }

    Color unlitColor{};
    check(!unlit.fragment(expected, unlitColor) && unlitColor.r == 255 &&
          unlitColor.g == 255 && unlitColor.b == 255 && unlitColor.a == 255,
          "Unlit texture callback");
    for (auto* shader : shaders) {
        shader->texture = nullptr;
        Color color{};
        check(!shader->fragment(expected, color) && color.r == 255 &&
              color.g == 0 && color.b == 255 && color.a == 255,
              "missing texture host fallback");
    }

    LlvmCommonShader probe("tools/fixtures/world_position.jydshader");
    probe.texture = &texture;
    probe.prepare();
    auto f = expected;
    // All fields differ, so reading clip position/normal/UV cannot pass.
    f.worldPosition = vec3(0.125f, 0.375f, 0.625f);
    Color color{};
    check(!probe.fragment(f, color), "world position probe discard");
    check(color.r == 31 && color.g == 95 && color.b == 159 && color.a == 255,
          "worldPosition host pack -> fragment builtin -> output");
}
} // namespace

int main() {
    try {
        TextureFixture fixture;
        Texture texture(fixture.writeTexture());
        testWrappers(texture);
        std::cout << "Native/JIT host ABI integration: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
