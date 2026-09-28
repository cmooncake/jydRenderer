#include "llvm_support.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::uint32_t sampleTestTexture(const void*, float, float) {
    return 10u | (20u << 8u) | (30u << 16u) | (255u << 24u);
}

} // namespace

int main() {
    jyd::LlvmSmokeResult result;
    if (!jyd::jydRunLlvmSmokeTest(&result)) {
        std::cerr << "LLVM smoke test failed: " << result.error << '\n';
        return 1;
    }

    std::cout << "LLVM version: " << result.llvmVersion << '\n';
    std::cout << "Target: " << result.targetTriple << '\n';
    std::cout << "JIT result: " << result.value << '\n';

    if (std::abs(result.value - 42.0f) > 1e-6f) {
        std::cerr << "Expected the JIT function to return 42\n";
        return 2;
    }

    jyd::JydShaderProgram shader;
    if (!jyd::jydCompileBuiltinCommonShader(&shader)) {
        std::cerr << "Common shader compilation failed: "
                  << shader.error << '\n';
        return 3;
    }

    float uniforms[44]{};
    for (int component = 0; component < 4; ++component) {
        uniforms[component * 4 + component] = 1.0f;
        uniforms[16 + component * 4 + component] = 1.0f;
    }
    uniforms[40] = -1.0f;

    const float vertexInput[8] = {
        1.0f, 2.0f, 3.0f,
        0.0f, 0.0f, 1.0f,
        0.25f, 0.75f
    };
    float varying[9]{};
    shader.vertex(uniforms, vertexInput, varying);

    int discard = 0;
    const std::uint32_t color = shader.fragment(
        uniforms,
        varying,
        nullptr,
        &sampleTestTexture,
        &discard);
    jyd::jydDestroyShaderProgram(shader.handle);

    const bool vertexMatches =
        std::abs(varying[0] - 1.0f) < 1e-6f &&
        std::abs(varying[1] - 2.0f) < 1e-6f &&
        std::abs(varying[2] - 3.0f) < 1e-6f &&
        std::abs(varying[3] - 1.0f) < 1e-6f &&
        std::abs(varying[6] - 1.0f) < 1e-6f &&
        std::abs(varying[7] - 0.25f) < 1e-6f &&
        std::abs(varying[8] - 0.75f) < 1e-6f;
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
    jyd::JydShaderProgram scriptedShader;
    if (!jyd::jydCompileShaderSource(
            shaderSource.data(), shaderSource.size(), &scriptedShader)) {
        std::cerr << "Shader language compilation failed: "
                  << scriptedShader.error << '\n';
        return 6;
    }
    float scriptedVarying[9]{};
    scriptedShader.vertex(uniforms, vertexInput, scriptedVarying);
    discard = 0;
    const std::uint32_t scriptedColor = scriptedShader.fragment(
        uniforms,
        scriptedVarying,
        nullptr,
        &sampleTestTexture,
        &discard);
    std::cout << "Script shader: " << scriptedShader.name << '\n';
    jyd::jydDestroyShaderProgram(scriptedShader.handle);

    if (discard != 0 || scriptedColor != 0xff1e140au ||
        std::abs(scriptedVarying[0] - 1.0f) > 1e-6f ||
        std::abs(scriptedVarying[8] - 0.75f) > 1e-6f) {
        std::cerr << "Scripted Common shader returned unexpected output\n";
        return 7;
    }
    std::cout << "Shader language vertex/fragment: OK\n";

    return 0;
}
