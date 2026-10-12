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

void requireShaderAbi() {
    if (jyd::jydShaderAbiVersion() != jyd::shader_abi::Version)
        throw std::runtime_error("shader compiler ABI mismatch; rebuild EXE and DLL");
}

} // namespace

LlvmCommonShader::LlvmCommonShader() {
    requireShaderAbi();
    if (!jydCompileBuiltinCommonShader(&program_)) {
        throw std::runtime_error(
            std::string("Cannot compile LLVM Common shader: ") +
            program_.error);
    }
    name_ = program_.name;
}

LlvmCommonShader::LlvmCommonShader(
    const std::filesystem::path& sourcePath) {
    requireShaderAbi();
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
    namespace U = shader_abi::Uniform;
    writeMatrix(uniforms_.data() + U::Mvp, mvp);
    writeMatrix(uniforms_.data() + U::View, vp);
    writeVector(uniforms_.data() + U::Camera, cameraPosition);
    writeVector(uniforms_.data() + U::SpecularLight, SpecularLightDirection);
    writeVector(uniforms_.data() + U::DiffuseLight, DiffuseLightDirection);
    writeVector(uniforms_.data() + U::Ambient, AmbientLightColor);
}

Commonv2f LlvmCommonShader::vertex(const Commona2v& input) const {
	namespace I = shader_abi::VertexIn;
	namespace V = shader_abi::Varying;

	float shaderInput[I::Count]{};

    for (int i = 0; i < 3; ++i) {
        shaderInput[I::Position + i] = input.position[i];
        shaderInput[I::Normal + i] = input.normal[i];
	}
    for(int i = 0; i < 2; ++i) {
        shaderInput[I::Texcoord + i] = input.texcoord[i];
	}

    float shaderOutput[V::Count]{};
    program_.vertex(uniforms_.data(), shaderInput, shaderOutput);

    // 解包：数组 → 结构体
    Commonv2f result{};
    for (int c = 0; c < 4; ++c) {
        result.position[c] = shaderOutput[V::Position + c];
    }
    for (int c = 0; c < 3; ++c) {
        result.normal[c] = shaderOutput[V::Normal + c];
    }
    for (int c = 0; c < 2; ++c) {
        result.texcoord[c] = shaderOutput[V::Texcoord + c];
    }
    return result;
}

bool LlvmCommonShader::fragment(const Commonv2f& input, Color& color) const {
    if (texture == nullptr) {
        color = {255, 0, 255, 255};
        return false;
    }

	namespace V = shader_abi::Varying;

	float shaderInput[V::Count]{};
    for(int i = 0; i < 4; ++i) {
        shaderInput[V::Position + i] = input.position[i];
	}
    for(int i = 0; i < 3; ++i)
		shaderInput[V::Normal + i] = input.normal[i];
	for (int i = 0; i < 2; ++i) 
		shaderInput[V::Texcoord + i] = input.texcoord[i];

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
