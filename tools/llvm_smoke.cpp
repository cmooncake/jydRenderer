#include "llvm_support.hpp"
#include "shader_abi.hpp"
#include "render_math.hpp"
#include "test_support.hpp"
#include <algorithm>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {

namespace U = jyd::shader_abi::Uniform;
namespace I = jyd::shader_abi::VertexIn;
namespace V = jyd::shader_abi::Varying;

struct OwnedShader {
    jyd::JydShaderProgram program{};
    OwnedShader() = default;
    OwnedShader(const OwnedShader&) = delete;
    OwnedShader& operator=(const OwnedShader&) = delete;
    ~OwnedShader() {
        if (program.handle) jyd::jydDestroyShaderProgram(program.handle);
    }
};

std::uint32_t sampleTestTexture(const void*, float, float) {
    return 10u | (20u << 8u) | (30u << 16u) | (255u << 24u);
}

void testRowMajorMatrix(const jyd::JydShaderProgram& shader) {
    // Separate buffers keep this case independent of the identity regression.
    float uniforms[U::Count]{};
    float input[I::Count]{};
    float output[V::Count]{};
    const float matrix[16] = {
        1, 2, 0, 3,
        0, 4, 5, 6,
        7, 0, 8, 9,
        0, 0, 0, 1
    };
    std::copy(matrix, matrix + 16, uniforms + U::Mvp);
    for (int c = 0; c < 4; ++c)
        uniforms[U::View + c * 4 + c] = 1.0f;
    input[I::Position + 0] = 1;
    input[I::Position + 1] = 2;
    input[I::Position + 2] = 3;
    input[I::Normal + 0] = 2;
    input[I::Normal + 1] = 3;
    input[I::Normal + 2] = 6;
    input[I::Texcoord + 0] = 0.25f;
    input[I::Texcoord + 1] = 0.75f;

    shader.vertex(uniforms, input, output);
    check(near(output[V::Position + 0], 8) &&
          near(output[V::Position + 1], 29) &&
          near(output[V::Position + 2], 40) &&
          near(output[V::Position + 3], 1), "row-major MVP ABI");
    check(near(output[V::Normal + 0], 2.0f) &&
          near(output[V::Normal + 1], 3.0f) &&
          near(output[V::Normal + 2], 6.0f), "raw normal field ABI");
    check(near(output[V::WorldPosition + 0], 1.0f) &&
          near(output[V::WorldPosition + 1], 2.0f) &&
          near(output[V::WorldPosition + 2], 3.0f), "world position ABI");
    check(near(output[V::Texcoord + 0], 0.25f) &&
          near(output[V::Texcoord + 1], 0.75f), "texcoord field ABI");

    // Changing View must not rotate world-space attributes.
    const float view[16] = {
        1, 2, 0, 11,
        0, 1, 3, 13,
        4, 0, 1, 17,
        0, 0, 0, 1
    };
    std::copy(view, view + 16, uniforms + U::View);
    shader.vertex(uniforms, input, output);
    check(near(output[V::Normal + 0], 2.0f) &&
          near(output[V::Normal + 1], 3.0f) &&
          near(output[V::Normal + 2], 6.0f), "world normal must ignore View");
    check(near(output[V::WorldPosition + 0], 1.0f) &&
          near(output[V::WorldPosition + 1], 2.0f) &&
          near(output[V::WorldPosition + 2], 3.0f), "world position must ignore View");
    check(near(output[V::Position + 0], 8) &&
          near(output[V::Position + 1], 29) &&
          near(output[V::Position + 2], 40) &&
          near(output[V::Position + 3], 1), "View must not overwrite MVP");
    std::cout << shader.name << ": matrix/normal/UV/world position ABI: OK\n";
}

void testUniformFields() {
    float uniforms[U::Count]{};
    for (int c = 0; c < 4; ++c) {
        uniforms[U::Mvp + c * 4 + c] = 1.0f;
        uniforms[U::View + c * 4 + c] = 1.0f;
    }
    struct Field { const char* name; int offset; float values[3]; };
    const Field fields[] = {
        {"cameraPosition", U::Camera, {11, 12, 13}},
        {"specularLightDirection", U::SpecularLight, {21, 22, 23}},
        {"diffuseLightDirection", U::DiffuseLight, {31, 32, 33}},
        {"ambientLightColor", U::Ambient, {41, 42, 43}}
    };
    for (const auto& field : fields)
        std::copy(field.values, field.values + 3, uniforms + field.offset);

    // Diagnostic shaders expose each uniform through the vec3 normal output.
    // Common does not use all these fields, so its image alone cannot test them.
    for (const auto& field : fields) {
        const std::string source =
            std::string("shader UniformProbe { vertex { ") +
            "position = transform_point(mvp, in.position); normal = " + field.name +
            "; texcoord = in.texcoord; worldPosition = in.position; } "
            "fragment { color = vec4(1,1,1,1); } }";
        OwnedShader probe;
        check(jyd::jydCompileShaderSource(source.data(), source.size(), &probe.program) != 0,
              probe.program.error);
        float input[I::Count]{};
        float output[V::Count]{};
        probe.program.vertex(uniforms, input, output);
        for (int c = 0; c < 3; ++c)
            check(near(output[V::Normal + c], field.values[c]), field.name);
    }
    std::cout << "Shader uniform field offsets: OK\n";
}

void testNormalContract(const jyd::JydShaderProgram& builtin,
                        const jyd::JydShaderProgram& scripted) {
    // Expose the compiler builtin directly, without lighting/byte clamping
    // masking a wrong normalize implementation.
    const std::string source =
        "shader NormalizeProbe { vertex { "
        "position = transform_point(mvp, in.position); "
        "normal = normalize(in.normal); texcoord = in.texcoord; "
        "worldPosition = in.position; } fragment { color = vec4(1,1,1,1); } }";
    OwnedShader probe;
    check(jyd::jydCompileShaderSource(source.data(), source.size(), &probe.program) != 0,
          probe.program.error);
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const vec3 normals[] = {
        {0, 0, 2}, {0, 3, 4}, {0, 0, -2}, {0, 0, 0},
        {0, 0, 1e-10f}, {0, 0, 1e-8f}, {0, 0, 2e-8f},
        {inf, 0, 1}, {0, nan, 1}, {0, 0, 1e30f}
    };
    float uniforms[U::Count]{};
    uniforms[U::DiffuseLight + 2] = 1;
    for (const auto& normal : normals) {
        float input[I::Count]{};
        float output[V::Count]{};
        for (int c = 0; c < 3; ++c) input[I::Normal + c] = normal[c];
        probe.program.vertex(uniforms, input, output);
        const auto expectedNormal = jyd::safeNormalize(normal);
        for (int c = 0; c < 3; ++c)
            check(near(output[V::Normal + c], expectedNormal[c]), "safe normalize contract");

        const float lighting = std::clamp(expectedNormal.z, 0.0f, 1.0f) * 0.8f + 0.2f;
        for (const auto* shader : {&builtin, &scripted}) {
            for (int c = 0; c < 3; ++c) output[V::Normal + c] = normal[c];
            int discard = -1;
            const auto color = shader->fragment(uniforms, output, nullptr,
                                                &sampleTestTexture, &discard);
            check(discard == 0, "Common must not discard");
            for (int c = 0; c < 3; ++c) {
                const int actual = (color >> (c * 8)) & 0xffu;
                const int expected = static_cast<int>((c + 1) * 10 * lighting);
                check(std::abs(actual - expected) <= 1, "Common fragment normalization RGB");
            }
            // Until the float-color migration, the script scales alpha too.
            const int expectedAlpha = shader == &builtin ? 255 : static_cast<int>(255 * lighting);
            check(std::abs(static_cast<int>(color >> 24) - expectedAlpha) <= 1,
                  "legacy Common alpha behavior");
        }
    }
    std::cout << "Normal length/zero/tiny/NaN/Inf/overflow contract: OK\n";
}

void testRequiredWorldPosition() {
    const std::string source =
        "shader MissingWorld { vertex { position = transform_point(mvp, in.position); "
        "normal = in.normal; texcoord = in.texcoord; } "
        "fragment { color = vec4(1,1,1,1); } }";
    OwnedShader probe;
    check(jyd::jydCompileShaderSource(source.data(), source.size(), &probe.program) == 0,
          "ABI v2 must reject a missing worldPosition output");
    check(std::string(probe.program.error).find("worldPosition") != std::string::npos,
          "missing output diagnostic must name worldPosition");
}

} // namespace

int runSmokeTests() {
    check(jyd::jydShaderAbiVersion() == jyd::shader_abi::Version,
          "shader compiler ABI mismatch; rebuild EXE and DLL");
    jyd::LlvmSmokeResult result;
    if (!jyd::jydRunLlvmSmokeTest(&result)) {
        std::cerr << "LLVM smoke test failed: " << result.error << '\n';
        return 1;
    }

    std::cout << "LLVM version: " << result.llvmVersion << '\n';
    std::cout << "Target: " << result.targetTriple << '\n';
    std::cout << "JIT result: " << result.value << '\n';

    if (!near(result.value, 42.0f)) {
        std::cerr << "Expected the JIT function to return 42\n";
        return 2;
    }

    OwnedShader builtin;
    auto& shader = builtin.program;
    if (!jyd::jydCompileBuiltinCommonShader(&shader)) {
        std::cerr << "Common shader compilation failed: "
                  << shader.error << '\n';
        return 3;
    }

    testRowMajorMatrix(shader);

    float uniforms[U::Count]{};
    for (int component = 0; component < 4; ++component) {
        uniforms[U::Mvp + component * 4 + component] = 1.0f;
        uniforms[U::View + component * 4 + component] = 1.0f;
    }
    uniforms[U::DiffuseLight + 2] = 1.0f;

    float vertexInput[I::Count]{};
    vertexInput[I::Position + 0] = 1.0f;
    vertexInput[I::Position + 1] = 2.0f;
    vertexInput[I::Position + 2] = 3.0f;
    vertexInput[I::Normal + 2] = 1.0f;
    vertexInput[I::Texcoord + 0] = 0.25f;
    vertexInput[I::Texcoord + 1] = 0.75f;
    float varying[V::Count]{};
    shader.vertex(uniforms, vertexInput, varying);

    int discard = 0;
    const std::uint32_t color = shader.fragment(
        uniforms,
        varying,
        nullptr,
        &sampleTestTexture,
        &discard);

    const bool vertexMatches =
        near(varying[V::Position + 0], 1.0f) &&
        near(varying[V::Position + 1], 2.0f) &&
        near(varying[V::Position + 2], 3.0f) &&
        near(varying[V::Position + 3], 1.0f) &&
        near(varying[V::Normal + 0], 0.0f) &&
        near(varying[V::Normal + 1], 0.0f) &&
        near(varying[V::Normal + 2], 1.0f) &&
        near(varying[V::Texcoord + 0], 0.25f) &&
        near(varying[V::Texcoord + 1], 0.75f) &&
        near(varying[V::WorldPosition + 0], 1.0f) &&
        near(varying[V::WorldPosition + 1], 2.0f) &&
        near(varying[V::WorldPosition + 2], 3.0f);
    if (!vertexMatches || discard != 0 || color != 0xff1e140au) {
        std::cerr << "Compiled Common shader returned unexpected output\n";
        return 4;
    }

    std::cout << "Common vertex/fragment shader: OK\n";

    std::ifstream shaderInputFile(
        "shaders/common.jydshader",
        std::ios::binary);
    if (!shaderInputFile) {
        std::cerr << "Cannot open shaders/common.jydshader\n";
        return 5;
    }
    const std::string shaderSource{
        std::istreambuf_iterator<char>(shaderInputFile),
        std::istreambuf_iterator<char>()};
    OwnedShader scripted;
    auto& scriptedShader = scripted.program;
    if (!jyd::jydCompileShaderSource(
            shaderSource.data(), shaderSource.size(), &scriptedShader)) {
        std::cerr << "Shader language compilation failed: "
                  << scriptedShader.error << '\n';
        return 6;
    }
    testRowMajorMatrix(scriptedShader);
    float scriptedVarying[V::Count]{};
    scriptedShader.vertex(uniforms, vertexInput, scriptedVarying);
    discard = 0;
    const std::uint32_t scriptedColor = scriptedShader.fragment(
        uniforms,
        scriptedVarying,
        nullptr,
        &sampleTestTexture,
        &discard);
    std::cout << "Script shader: " << scriptedShader.name << '\n';

    bool scriptedVertexMatches = true;
    for (int c = 0; c < V::Count; ++c)
        scriptedVertexMatches = scriptedVertexMatches && near(scriptedVarying[c], varying[c]);
    if (discard != 0 || scriptedColor != 0xff1e140au || !scriptedVertexMatches) {
        std::cerr << "Scripted Common shader returned unexpected output\n";
        return 7;
    }
    std::cout << "Shader language vertex/fragment: OK\n";

    testUniformFields();
    testNormalContract(shader, scriptedShader);
    testRequiredWorldPosition();

    return 0;
}

int main() {
    try {
        return runSmokeTests();
    } catch (const std::exception& e) {
        std::cerr << "LLVM smoke test failed: " << e.what() << '\n';
        return 8;
    }
}
