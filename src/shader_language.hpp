#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace llvm {
class DataLayout;
class LLVMContext;
class Module;
}

namespace jyd::shader_language {

bool buildModule(
    std::string_view source,
    llvm::LLVMContext& context,
    const llvm::DataLayout& dataLayout,
    std::unique_ptr<llvm::Module>& module,
    std::string& shaderName,
    std::string& error);

} // namespace jyd::shader_language
