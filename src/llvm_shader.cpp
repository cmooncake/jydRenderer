#include "llvm_shader.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace jyd {
namespace {

std::uint32_t sampleTexture(
    const void* texturePointer,
    float u,
    float v) {
    if (texturePointer == nullptr) {
        return 0xffff00ffu;
    }

    const auto& texture = *static_cast<const Texture*>(texturePointer);
    const Color color = texture.sampleNearest(vec2(u, v));
    return static_cast<std::uint32_t>(color.r) |
        (static_cast<std::uint32_t>(color.g) << 8u) |
        (static_cast<std::uint32_t>(color.b) << 16u) |
        (static_cast<std::uint32_t>(color.a) << 24u);
}

void writeMatrix(float* destination, const mat4& matrix) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            destination[row * 4 + column] = matrix[row][column];
        }
    }
}

void writeVector(float* destination, const vec3& vector) {
    destination[0] = vector.x;
    destination[1] = vector.y;
    destination[2] = vector.z;
}

} // namespace

LlvmCommonShader::LlvmCommonShader() {
    if (!jydCompileBuiltinCommonShader(&program_)) {
        throw std::runtime_error(
            std::string("Cannot compile LLVM Common shader: ") +
            program_.error);
    }
    name_ = program_.name;
}

LlvmCommonShader::LlvmCommonShader(
    const std::filesystem::path& sourcePath) {
    std::ifstream input(sourcePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Cannot open shader source: " + sourcePath.string());
    }
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (!jydCompileShaderSource(source.data(), source.size(), &program_)) {
        throw std::runtime_error(
            "Cannot compile shader '" + sourcePath.string() + "': " +
            program_.error);
    }
    name_ = program_.name;
}

LlvmCommonShader::~LlvmCommonShader() {
    jydDestroyShaderProgram(program_.handle);
}

const char* LlvmCommonShader::name() const {
    return name_.c_str();
}

void LlvmCommonShader::prepare() {
    writeMatrix(uniforms_.data(), mvp);
    writeMatrix(uniforms_.data() + 16, vp);
    writeVector(uniforms_.data() + 32, cameraPosition);
    writeVector(uniforms_.data() + 35, SpecularLightDirection);
    writeVector(uniforms_.data() + 38, DiffuseLightDirection);
    writeVector(uniforms_.data() + 41, AmbientLightColor);
}

Commonv2f LlvmCommonShader::vertex(const Commona2v& input) const {
    const float shaderInput[8] = {
        input.position.x, input.position.y, input.position.z,
        input.normal.x, input.normal.y, input.normal.z,
        input.texcoord.x, input.texcoord.y
    };
    float shaderOutput[9]{};
    program_.vertex(uniforms_.data(), shaderInput, shaderOutput);

    return {
        vec4(shaderOutput[0], shaderOutput[1], shaderOutput[2], shaderOutput[3]),
        vec3(shaderOutput[4], shaderOutput[5], shaderOutput[6]),
        vec2(shaderOutput[7], shaderOutput[8])
    };
}

bool LlvmCommonShader::fragment(const Commonv2f& input, Color& color) const {
    if (texture == nullptr) {
        color = {255, 0, 255, 255};
        return false;
    }

    const float shaderInput[9] = {
        input.position.x, input.position.y, input.position.z, input.position.w,
        input.normal.x, input.normal.y, input.normal.z,
        input.texcoord.x, input.texcoord.y
    };
    int discard = 0;
    const std::uint32_t packed = program_.fragment(
        uniforms_.data(),
        shaderInput,
        texture,
        &sampleTexture,
        &discard);
    color = {
        static_cast<std::uint8_t>(packed & 0xffu),
        static_cast<std::uint8_t>((packed >> 8u) & 0xffu),
        static_cast<std::uint8_t>((packed >> 16u) & 0xffu),
        static_cast<std::uint8_t>((packed >> 24u) & 0xffu)
    };
    return discard != 0;
}

} // namespace jyd
