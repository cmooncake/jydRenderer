#include "llvm_support.hpp"
#include "shader_language.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Support/raw_ostream.h>
#if LLVM_VERSION_MAJOR >= 23
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

namespace jyd {
namespace {

struct CompiledShaderProgram {
    std::unique_ptr<llvm::orc::LLJIT> jit;
};

void copyText(char* destination, std::size_t capacity, std::string_view text) {
    if (capacity == 0) {
        return;
    }

    const std::size_t length = std::min(capacity - 1, text.size());
    std::memcpy(destination, text.data(), length);
    destination[length] = '\0';
}

int failWithMessage(LlvmSmokeResult& result, std::string_view error) {
    copyText(result.error, sizeof(result.error), error);
    return 0;
}

int failWithLlvmError(
    LlvmSmokeResult& result,
    llvm::Error llvmError) {
    return failWithMessage(
        result,
        llvm::toString(std::move(llvmError)));
}

int failShaderCompile(JydShaderProgram& program, std::string_view error) {
    copyText(program.error, sizeof(program.error), error);
    return 0;
}

int failShaderCompile(JydShaderProgram& program, llvm::Error llvmError) {
    return failShaderCompile(program, llvm::toString(std::move(llvmError)));
}

bool initializeNativeTarget(std::string& error) {
    if (llvm::InitializeNativeTarget()) {
        error = "LLVM failed to initialize the native target";
        return false;
    }
    if (llvm::InitializeNativeTargetAsmPrinter()) {
        error = "LLVM failed to initialize the native assembly printer";
        return false;
    }
    return true;
}

llvm::Value* loadFloat(
    llvm::IRBuilder<>& builder,
    llvm::Value* pointer,
    int index,
    const char* name = "") {
    llvm::Value* address = builder.CreateConstGEP1_32(
        builder.getFloatTy(), pointer, index);
    return builder.CreateLoad(builder.getFloatTy(), address, name);
}

void storeFloat(
    llvm::IRBuilder<>& builder,
    llvm::Value* pointer,
    int index,
    llvm::Value* value) {
    llvm::Value* address = builder.CreateConstGEP1_32(
        builder.getFloatTy(), pointer, index);
    builder.CreateStore(value, address);
}

llvm::Value* clampFloat(
    llvm::IRBuilder<>& builder,
    llvm::Value* value,
    float minimum,
    float maximum) {
    llvm::Value* minValue = llvm::ConstantFP::get(
        builder.getFloatTy(), minimum);
    llvm::Value* maxValue = llvm::ConstantFP::get(
        builder.getFloatTy(), maximum);
    llvm::Value* aboveMinimum = builder.CreateSelect(
        builder.CreateFCmpOLT(value, minValue), minValue, value);
    return builder.CreateSelect(
        builder.CreateFCmpOGT(aboveMinimum, maxValue),
        maxValue,
        aboveMinimum);
}

void buildCommonVertexFunction(
    llvm::Module& module,
    llvm::IRBuilder<>& builder) {
    llvm::Type* pointerType = builder.getPtrTy();
    llvm::FunctionType* type = llvm::FunctionType::get(
        builder.getVoidTy(),
        {pointerType, pointerType, pointerType},
        false);
    llvm::Function* function = llvm::Function::Create(
        type,
        llvm::Function::ExternalLinkage,
        "jyd_shader_vertex",
        module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(
        module.getContext(), "entry", function);
    builder.SetInsertPoint(entry);

    auto argument = function->arg_begin();
    llvm::Value* uniforms = argument++;
    llvm::Value* input = argument++;
    llvm::Value* output = argument;
    uniforms->setName("uniforms");
    input->setName("input");
    output->setName("output");

    llvm::Value* position[4] = {
        loadFloat(builder, input, 0),
        loadFloat(builder, input, 1),
        loadFloat(builder, input, 2),
        llvm::ConstantFP::get(builder.getFloatTy(), 1.0)
    };
    llvm::Value* normal[4] = {
        loadFloat(builder, input, 3),
        loadFloat(builder, input, 4),
        loadFloat(builder, input, 5),
        llvm::ConstantFP::get(builder.getFloatTy(), 0.0)
    };

    for (int row = 0; row < 4; ++row) {
        llvm::Value* transformed = llvm::ConstantFP::get(
            builder.getFloatTy(), 0.0);
        for (int column = 0; column < 4; ++column) {
            transformed = builder.CreateFAdd(
                transformed,
                builder.CreateFMul(
                    loadFloat(builder, uniforms, row * 4 + column),
                    position[column]));
        }
        storeFloat(builder, output, row, transformed);
    }

    llvm::Value* transformedNormal[3]{};
    llvm::Value* lengthSquared = llvm::ConstantFP::get(
        builder.getFloatTy(), 0.0);
    for (int row = 0; row < 3; ++row) {
        transformedNormal[row] = llvm::ConstantFP::get(
            builder.getFloatTy(), 0.0);
        for (int column = 0; column < 4; ++column) {
            transformedNormal[row] = builder.CreateFAdd(
                transformedNormal[row],
                builder.CreateFMul(
                    loadFloat(builder, uniforms, 16 + row * 4 + column),
                    normal[column]));
        }
        lengthSquared = builder.CreateFAdd(
            lengthSquared,
            builder.CreateFMul(
                transformedNormal[row], transformedNormal[row]));
    }
    llvm::Value* length = builder.CreateUnaryIntrinsic(
        llvm::Intrinsic::sqrt,
        clampFloat(builder, lengthSquared, 1.0e-16f, 1.0e30f));
    for (int component = 0; component < 3; ++component) {
        storeFloat(
            builder,
            output,
            4 + component,
            builder.CreateFDiv(transformedNormal[component], length));
    }
    storeFloat(builder, output, 7, loadFloat(builder, input, 6));
    storeFloat(builder, output, 8, loadFloat(builder, input, 7));
    builder.CreateRetVoid();
}

void buildCommonFragmentFunction(
    llvm::Module& module,
    llvm::IRBuilder<>& builder) {
    llvm::Type* pointerType = builder.getPtrTy();
    llvm::FunctionType* type = llvm::FunctionType::get(
        builder.getInt32Ty(),
        {pointerType, pointerType, pointerType, pointerType, pointerType},
        false);
    llvm::Function* function = llvm::Function::Create(
        type,
        llvm::Function::ExternalLinkage,
        "jyd_shader_fragment",
        module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(
        module.getContext(), "entry", function);
    builder.SetInsertPoint(entry);

    auto argument = function->arg_begin();
    llvm::Value* uniforms = argument++;
    llvm::Value* input = argument++;
    llvm::Value* texture = argument++;
    llvm::Value* sampler = argument++;
    llvm::Value* discard = argument;

    llvm::FunctionType* samplerType = llvm::FunctionType::get(
        builder.getInt32Ty(),
        {pointerType, builder.getFloatTy(), builder.getFloatTy()},
        false);
    llvm::Value* texel = builder.CreateCall(
        samplerType,
        sampler,
        {texture, loadFloat(builder, input, 7), loadFloat(builder, input, 8)});

    llvm::Value* diffuse = llvm::ConstantFP::get(
        builder.getFloatTy(), 0.0);
    for (int component = 0; component < 3; ++component) {
        diffuse = builder.CreateFAdd(
            diffuse,
            builder.CreateFMul(
                loadFloat(builder, uniforms, 38 + component),
                builder.CreateFNeg(loadFloat(builder, input, 4 + component))));
    }
    diffuse = clampFloat(builder, diffuse, 0.0f, 1.0f);
    llvm::Value* lighting = builder.CreateFAdd(
        builder.CreateFMul(
            diffuse,
            llvm::ConstantFP::get(builder.getFloatTy(), 0.8)),
        llvm::ConstantFP::get(builder.getFloatTy(), 0.2));

    llvm::Value* packedResult = llvm::ConstantInt::get(
        builder.getInt32Ty(), 0);
    for (int channel = 0; channel < 3; ++channel) {
        llvm::Value* channelBits = builder.CreateAnd(
            builder.CreateLShr(texel, channel * 8),
            llvm::ConstantInt::get(builder.getInt32Ty(), 0xff));
        llvm::Value* channelFloat = builder.CreateUIToFP(
            channelBits, builder.getFloatTy());
        llvm::Value* litChannel = builder.CreateFPToUI(
            builder.CreateFMul(channelFloat, lighting),
            builder.getInt32Ty());
        packedResult = builder.CreateOr(
            packedResult,
            builder.CreateShl(litChannel, channel * 8));
    }
    llvm::Value* alpha = builder.CreateAnd(
        texel,
        llvm::ConstantInt::get(builder.getInt32Ty(), 0xff000000u));
    packedResult = builder.CreateOr(packedResult, alpha);
    builder.CreateStore(
        llvm::ConstantInt::get(builder.getInt32Ty(), 0), discard);
    builder.CreateRet(packedResult);
}

int finishShaderCompile(
    JydShaderProgram& program,
    std::unique_ptr<CompiledShaderProgram> compiled,
    std::unique_ptr<llvm::LLVMContext> context,
    std::unique_ptr<llvm::Module> module,
    std::string_view shaderName) {
    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if (llvm::verifyModule(*module, &verifyStream)) {
        verifyStream.flush();
        return failShaderCompile(
            program,
            "Generated shader module is invalid: " + verifyError);
    }

    llvm::orc::ThreadSafeModule threadSafeModule(
        std::move(module),
        std::move(context));
    if (llvm::Error error =
            compiled->jit->addIRModule(std::move(threadSafeModule))) {
        return failShaderCompile(program, std::move(error));
    }

    auto vertexExpected = compiled->jit->lookup("jyd_shader_vertex");
    if (!vertexExpected) {
        return failShaderCompile(program, vertexExpected.takeError());
    }
    auto fragmentExpected = compiled->jit->lookup("jyd_shader_fragment");
    if (!fragmentExpected) {
        return failShaderCompile(program, fragmentExpected.takeError());
    }

#if LLVM_VERSION_MAJOR >= 17
    program.vertex = vertexExpected->toPtr<JydShaderVertexFunction>();
    program.fragment = fragmentExpected->toPtr<JydShaderFragmentFunction>();
#else
    program.vertex =
        vertexExpected->getAddress().toPtr<JydShaderVertexFunction>();
    program.fragment =
        fragmentExpected->getAddress().toPtr<JydShaderFragmentFunction>();
#endif
    copyText(program.name, sizeof(program.name), shaderName);
    program.handle = compiled.release();
    return 1;
}

} // namespace

extern "C" int jydCompileShaderSource(
    const char* source,
    std::size_t sourceLength,
    JydShaderProgram* programPointer) {
    if (source == nullptr || programPointer == nullptr) {
        return 0;
    }

    JydShaderProgram& program = *programPointer;
    program = {};
    std::string initializationError;
    if (!initializeNativeTarget(initializationError)) {
        return failShaderCompile(program, initializationError);
    }

    auto jitExpected = llvm::orc::LLJITBuilder().create();
    if (!jitExpected) {
        return failShaderCompile(program, jitExpected.takeError());
    }
    auto compiled = std::make_unique<CompiledShaderProgram>();
    compiled->jit = std::move(*jitExpected);

    auto context = std::make_unique<llvm::LLVMContext>();
    std::unique_ptr<llvm::Module> module;
    std::string shaderName;
    std::string compileError;
    if (!shader_language::buildModule(
            std::string_view(source, sourceLength),
            *context,
            compiled->jit->getDataLayout(),
            module,
            shaderName,
            compileError)) {
        return failShaderCompile(program, compileError);
    }
    module->setTargetTriple(
        llvm::Triple(llvm::sys::getDefaultTargetTriple()));
    return finishShaderCompile(
        program,
        std::move(compiled),
        std::move(context),
        std::move(module),
        shaderName);
}

extern "C" int jydCompileBuiltinCommonShader(JydShaderProgram* programPointer) {
    if (programPointer == nullptr) {
        return 0;
    }

    JydShaderProgram& program = *programPointer;
    program = {};
    std::string initializationError;
    if (!initializeNativeTarget(initializationError)) {
        return failShaderCompile(program, initializationError);
    }

    auto jitExpected = llvm::orc::LLJITBuilder().create();
    if (!jitExpected) {
        return failShaderCompile(program, jitExpected.takeError());
    }
    auto compiled = std::make_unique<CompiledShaderProgram>();
    compiled->jit = std::move(*jitExpected);

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>(
        "jyd_builtin_common_shader",
        *context);
    module->setDataLayout(compiled->jit->getDataLayout());
    module->setTargetTriple(
        llvm::Triple(llvm::sys::getDefaultTargetTriple()));

    llvm::IRBuilder<> builder(*context);
    buildCommonVertexFunction(*module, builder);
    buildCommonFragmentFunction(*module, builder);
    return finishShaderCompile(
        program,
        std::move(compiled),
        std::move(context),
        std::move(module),
        "BuiltinCommonTexturedLighting");
}

extern "C" void jydDestroyShaderProgram(void* handle) {
    delete static_cast<CompiledShaderProgram*>(handle);
}

extern "C" int jydRunLlvmSmokeTest(LlvmSmokeResult* resultPointer) {
    if (resultPointer == nullptr) {
        return 0;
    }

    LlvmSmokeResult& result = *resultPointer;
    result = {};

    if (llvm::InitializeNativeTarget()) {
        return failWithMessage(
            result,
            "LLVM failed to initialize the native target");
    }
    if (llvm::InitializeNativeTargetAsmPrinter()) {
        return failWithMessage(
            result,
            "LLVM failed to initialize the native assembly printer");
    }

    auto jitExpected = llvm::orc::LLJITBuilder().create();
    if (!jitExpected) {
        return failWithLlvmError(result, jitExpected.takeError());
    }
    auto jit = std::move(*jitExpected);

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>(
        "jyd_llvm_smoke_module",
        *context);
    module->setDataLayout(jit->getDataLayout());

    llvm::IRBuilder<> builder(*context);
    llvm::FunctionType* functionType = llvm::FunctionType::get(
        builder.getFloatTy(),
        {builder.getFloatTy()},
        false);
    llvm::Function* function = llvm::Function::Create(
        functionType,
        llvm::Function::ExternalLinkage,
        "jyd_llvm_smoke",
        module.get());

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(
        *context,
        "entry",
        function);
    builder.SetInsertPoint(entry);

    llvm::Value* input = function->getArg(0);
    llvm::Value* multiplier = llvm::ConstantFP::get(
        builder.getFloatTy(),
        2.0);
    builder.CreateRet(builder.CreateFMul(input, multiplier));

    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if (llvm::verifyModule(*module, &verifyStream)) {
        verifyStream.flush();
        return failWithMessage(
            result,
            "Generated LLVM module is invalid: " + verifyError);
    }

    llvm::orc::ThreadSafeModule threadSafeModule(
        std::move(module),
        std::move(context));
    if (llvm::Error llvmError =
            jit->addIRModule(std::move(threadSafeModule))) {
        return failWithLlvmError(result, std::move(llvmError));
    }

    auto symbolExpected = jit->lookup("jyd_llvm_smoke");
    if (!symbolExpected) {
        return failWithLlvmError(result, symbolExpected.takeError());
    }

    using SmokeFunction = float (*)(float);
#if LLVM_VERSION_MAJOR >= 17
    SmokeFunction smokeFunction =
        symbolExpected->toPtr<SmokeFunction>();
#else
    SmokeFunction smokeFunction =
        symbolExpected->getAddress().toPtr<SmokeFunction>();
#endif

    copyText(
        result.llvmVersion,
        sizeof(result.llvmVersion),
        LLVM_VERSION_STRING);
    const std::string targetTriple = llvm::sys::getDefaultTargetTriple();
    copyText(
        result.targetTriple,
        sizeof(result.targetTriple),
        targetTriple);
    result.value = smokeFunction(21.0f);
    return 1;
}

} // namespace jyd
