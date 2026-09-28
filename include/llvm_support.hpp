#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#if defined(JYD_SHADER_COMPILER_BUILD)
#define JYD_SHADER_COMPILER_API __declspec(dllexport)
#else
#define JYD_SHADER_COMPILER_API __declspec(dllimport)
#endif
#else
#define JYD_SHADER_COMPILER_API
#endif

namespace jyd {

struct LlvmSmokeResult {
    char llvmVersion[32]{};
    char targetTriple[128]{};
    char error[512]{};
    float value = 0.0f;
};

// Stable, flat ABI used between the renderer and JIT-generated machine code.
// Keeping C++ containers and math classes out of this boundary makes compiled
// shader programs replaceable without depending on their object layout.
using JydTextureSampleFunction = std::uint32_t (*)(
    const void* texture,
    float u,
    float v);
using JydShaderVertexFunction = void (*)(
    const float* uniforms,
    const float* input,
    float* output);
using JydShaderFragmentFunction = std::uint32_t (*)(
    const float* uniforms,
    const float* input,
    const void* texture,
    JydTextureSampleFunction sampleTexture,
    int* discard);

struct JydShaderProgram {
    void* handle = nullptr;
    JydShaderVertexFunction vertex = nullptr;
    JydShaderFragmentFunction fragment = nullptr;
    char name[128]{};
    char error[512]{};
};

extern "C" JYD_SHADER_COMPILER_API int jydCompileShaderSource(
    const char* source,
    std::size_t sourceLength,
    JydShaderProgram* program);
extern "C" JYD_SHADER_COMPILER_API int jydCompileBuiltinCommonShader(
    JydShaderProgram* program);
extern "C" JYD_SHADER_COMPILER_API void jydDestroyShaderProgram(
    void* handle);

// Builds a tiny LLVM module, compiles it through ORC LLJIT, looks up the
// generated function and executes it. This is kept separate from Renderer so
// LLVM integration can be verified before the shader language is implemented.
extern "C" JYD_SHADER_COMPILER_API int jydRunLlvmSmokeTest(
    LlvmSmokeResult* result);

} // namespace jyd

#undef JYD_SHADER_COMPILER_API
