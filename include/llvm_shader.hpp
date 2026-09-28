#pragma once

#include "llvm_support.hpp"
#include "shader.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace jyd {

class LlvmCommonShader final : public CommonShader {
public:
    LlvmCommonShader();
    explicit LlvmCommonShader(const std::filesystem::path& sourcePath);
    ~LlvmCommonShader() override;

    LlvmCommonShader(const LlvmCommonShader&) = delete;
    LlvmCommonShader& operator=(const LlvmCommonShader&) = delete;

    const char* name() const override;
    void prepare() override;
    Commonv2f vertex(const Commona2v& input) const override;
    bool fragment(const Commonv2f& input, Color& color) const override;

private:
    JydShaderProgram program_{};
    std::array<float, 44> uniforms_{};
    std::string name_;
};

} // namespace jyd
