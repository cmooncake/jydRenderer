#include "shader_language.hpp"

#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace jyd::shader_language {
namespace {

enum class TokenKind {
    End,
    Identifier,
    Number,
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    Comma,
    Semicolon,
    Equal,
    Plus,
    Minus,
    Star,
    Slash
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    float number = 0.0f;
    int line = 1;
    int column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Token next() {
        skipTrivia();
        Token token;
        token.line = line_;
        token.column = column_;
        if (atEnd()) {
            return token;
        }

        const char character = peek();
        if (std::isalpha(static_cast<unsigned char>(character)) ||
            character == '_') {
            while (!atEnd()) {
                const char current = peek();
                if (!std::isalnum(static_cast<unsigned char>(current)) &&
                    current != '_' && current != '.') {
                    break;
                }
                token.text.push_back(advance());
            }
            token.kind = TokenKind::Identifier;
            return token;
        }

        if (std::isdigit(static_cast<unsigned char>(character)) ||
            (character == '.' &&
             position_ + 1 < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_ + 1])))) {
            const std::size_t start = position_;
            while (!atEnd() &&
                   std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
            if (!atEnd() && peek() == '.') {
                advance();
                while (!atEnd() &&
                       std::isdigit(static_cast<unsigned char>(peek()))) {
                    advance();
                }
            }
            token.text = std::string(source_.substr(start, position_ - start));
            token.number = std::strtof(token.text.c_str(), nullptr);
            token.kind = TokenKind::Number;
            return token;
        }

        advance();
        switch (character) {
        case '{': token.kind = TokenKind::LeftBrace; break;
        case '}': token.kind = TokenKind::RightBrace; break;
        case '(': token.kind = TokenKind::LeftParen; break;
        case ')': token.kind = TokenKind::RightParen; break;
        case ',': token.kind = TokenKind::Comma; break;
        case ';': token.kind = TokenKind::Semicolon; break;
        case '=': token.kind = TokenKind::Equal; break;
        case '+': token.kind = TokenKind::Plus; break;
        case '-': token.kind = TokenKind::Minus; break;
        case '*': token.kind = TokenKind::Star; break;
        case '/': token.kind = TokenKind::Slash; break;
        default:
            token.text.assign(1, character);
            break;
        }
        return token;
    }

private:
    bool atEnd() const { return position_ >= source_.size(); }
    char peek() const { return atEnd() ? '\0' : source_[position_]; }

    char advance() {
        const char result = source_[position_++];
        if (result == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return result;
    }

    void skipTrivia() {
        while (!atEnd()) {
            if (std::isspace(static_cast<unsigned char>(peek()))) {
                advance();
                continue;
            }
            if (peek() == '/' &&
                position_ + 1 < source_.size() &&
                source_[position_ + 1] == '/') {
                while (!atEnd() && peek() != '\n') {
                    advance();
                }
                continue;
            }
            break;
        }
    }

    std::string_view source_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
};

struct Expression {
    enum class Kind { Number, Name, Unary, Binary, Call } kind;
    float number = 0.0f;
    std::string text;
    std::vector<std::unique_ptr<Expression>> children;
};

struct Statement {
    std::string name;
    std::unique_ptr<Expression> expression;
};

struct ShaderSyntax {
    std::string name;
    std::vector<Statement> vertex;
    std::vector<Statement> fragment;
};

class Parser {
public:
    explicit Parser(std::string_view source) : lexer_(source) {
        advance();
    }

    bool parse(ShaderSyntax& shader, std::string& error) {
        error_ = &error;
        if (!consumeIdentifier("shader")) {
            return fail("expected 'shader'");
        }
        if (current_.kind != TokenKind::Identifier) {
            return fail("expected a shader name");
        }
        shader.name = current_.text;
        advance();
        if (!consume(TokenKind::LeftBrace, "expected '{' after shader name")) {
            return false;
        }
        if (!parseStage("vertex", shader.vertex) ||
            !parseStage("fragment", shader.fragment)) {
            return false;
        }
        if (!consume(TokenKind::RightBrace, "expected '}' after shader")) {
            return false;
        }
        if (current_.kind != TokenKind::End) {
            return fail("unexpected input after shader declaration");
        }
        return true;
    }

private:
    void advance() { current_ = lexer_.next(); }

    bool consume(TokenKind kind, const char* message) {
        if (current_.kind != kind) {
            return fail(message);
        }
        advance();
        return true;
    }

    bool consumeIdentifier(const char* name) {
        if (current_.kind != TokenKind::Identifier || current_.text != name) {
            return false;
        }
        advance();
        return true;
    }

    bool parseStage(const char* stageName, std::vector<Statement>& statements) {
        if (!consumeIdentifier(stageName)) {
            return fail(std::string("expected '") + stageName + "' stage");
        }
        if (!consume(TokenKind::LeftBrace, "expected '{' after stage name")) {
            return false;
        }
        while (current_.kind != TokenKind::RightBrace &&
               current_.kind != TokenKind::End) {
            consumeIdentifier("let");
            if (current_.kind != TokenKind::Identifier) {
                return fail("expected an output or local variable name");
            }
            Statement statement;
            statement.name = current_.text;
            advance();
            if (!consume(TokenKind::Equal, "expected '=' after variable name")) {
                return false;
            }
            statement.expression = parseExpression();
            if (!statement.expression) {
                return false;
            }
            if (!consume(TokenKind::Semicolon, "expected ';' after expression")) {
                return false;
            }
            statements.push_back(std::move(statement));
        }
        return consume(TokenKind::RightBrace, "expected '}' after stage");
    }

    std::unique_ptr<Expression> parseExpression() { return parseAdditive(); }

    std::unique_ptr<Expression> parseAdditive() {
        auto expression = parseMultiplicative();
        while (expression &&
               (current_.kind == TokenKind::Plus ||
                current_.kind == TokenKind::Minus)) {
            const std::string operation =
                current_.kind == TokenKind::Plus ? "+" : "-";
            advance();
            auto right = parseMultiplicative();
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Expression>();
            binary->kind = Expression::Kind::Binary;
            binary->text = operation;
            binary->children.push_back(std::move(expression));
            binary->children.push_back(std::move(right));
            expression = std::move(binary);
        }
        return expression;
    }

    std::unique_ptr<Expression> parseMultiplicative() {
        auto expression = parseUnary();
        while (expression &&
               (current_.kind == TokenKind::Star ||
                current_.kind == TokenKind::Slash)) {
            const std::string operation =
                current_.kind == TokenKind::Star ? "*" : "/";
            advance();
            auto right = parseUnary();
            if (!right) {
                return nullptr;
            }
            auto binary = std::make_unique<Expression>();
            binary->kind = Expression::Kind::Binary;
            binary->text = operation;
            binary->children.push_back(std::move(expression));
            binary->children.push_back(std::move(right));
            expression = std::move(binary);
        }
        return expression;
    }

    std::unique_ptr<Expression> parseUnary() {
        if (current_.kind == TokenKind::Minus) {
            advance();
            auto operand = parseUnary();
            if (!operand) {
                return nullptr;
            }
            auto unary = std::make_unique<Expression>();
            unary->kind = Expression::Kind::Unary;
            unary->text = "-";
            unary->children.push_back(std::move(operand));
            return unary;
        }
        return parsePrimary();
    }

    std::unique_ptr<Expression> parsePrimary() {
        if (current_.kind == TokenKind::Number) {
            auto expression = std::make_unique<Expression>();
            expression->kind = Expression::Kind::Number;
            expression->number = current_.number;
            advance();
            return expression;
        }
        if (current_.kind == TokenKind::Identifier) {
            auto expression = std::make_unique<Expression>();
            expression->text = current_.text;
            advance();
            if (current_.kind != TokenKind::LeftParen) {
                expression->kind = Expression::Kind::Name;
                return expression;
            }
            expression->kind = Expression::Kind::Call;
            advance();
            if (current_.kind != TokenKind::RightParen) {
                while (true) {
                    auto argument = parseExpression();
                    if (!argument) {
                        return nullptr;
                    }
                    expression->children.push_back(std::move(argument));
                    if (current_.kind != TokenKind::Comma) {
                        break;
                    }
                    advance();
                }
            }
            if (!consume(TokenKind::RightParen, "expected ')' after arguments")) {
                return nullptr;
            }
            return expression;
        }
        if (current_.kind == TokenKind::LeftParen) {
            advance();
            auto expression = parseExpression();
            if (!expression ||
                !consume(TokenKind::RightParen, "expected ')' after expression")) {
                return nullptr;
            }
            return expression;
        }
        fail("expected an expression");
        return nullptr;
    }

    bool fail(const std::string& message) {
        if (error_ && error_->empty()) {
            *error_ = "line " + std::to_string(current_.line) + ", column " +
                std::to_string(current_.column) + ": " + message;
            if (!current_.text.empty()) {
                *error_ += " near '" + current_.text + "'";
            }
        }
        return false;
    }

    Lexer lexer_;
    Token current_;
    std::string* error_ = nullptr;
};

enum class ValueKind { Scalar, Vec2, Vec3, Vec4, Mat4, Invalid };

struct CodeValue {
    ValueKind kind = ValueKind::Invalid;
    std::vector<llvm::Value*> components;
    int matrixOffset = 0;
};

int vectorSize(ValueKind kind) {
    switch (kind) {
    case ValueKind::Vec2: return 2;
    case ValueKind::Vec3: return 3;
    case ValueKind::Vec4: return 4;
    default: return 0;
    }
}

ValueKind vectorKind(int size) {
    switch (size) {
    case 2: return ValueKind::Vec2;
    case 3: return ValueKind::Vec3;
    case 4: return ValueKind::Vec4;
    default: return ValueKind::Invalid;
    }
}

class StageCompiler {
public:
    enum class Stage { Vertex, Fragment };

    StageCompiler(llvm::Module& module, Stage stage, std::string& error)
        : module_(module), builder_(module.getContext()), stage_(stage),
          error_(error) {}

    bool compile(const std::vector<Statement>& statements) {
        createFunction();
        createBuiltins();
        for (const Statement& statement : statements) {
            CodeValue value = emit(*statement.expression);
            if (value.kind == ValueKind::Invalid) {
                return false;
            }
            variables_[statement.name] = std::move(value);
        }
        return stage_ == Stage::Vertex ? finishVertex() : finishFragment();
    }

private:
    llvm::Value* loadFloat(llvm::Value* pointer, int index) {
        llvm::Value* address = builder_.CreateConstGEP1_32(
            builder_.getFloatTy(), pointer, index);
        return builder_.CreateLoad(builder_.getFloatTy(), address);
    }

    void storeFloat(llvm::Value* pointer, int index, llvm::Value* value) {
        llvm::Value* address = builder_.CreateConstGEP1_32(
            builder_.getFloatTy(), pointer, index);
        builder_.CreateStore(value, address);
    }

    CodeValue vectorFromPointer(
        llvm::Value* pointer, int offset, int size) {
        CodeValue result;
        result.kind = vectorKind(size);
        for (int component = 0; component < size; ++component) {
            result.components.push_back(loadFloat(pointer, offset + component));
        }
        return result;
    }

    CodeValue scalar(float value) {
        return {
            ValueKind::Scalar,
            {llvm::ConstantFP::get(builder_.getFloatTy(), value)},
            0
        };
    }

    void createFunction() {
        llvm::Type* pointerType = builder_.getPtrTy();
        if (stage_ == Stage::Vertex) {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder_.getVoidTy(),
                {pointerType, pointerType, pointerType},
                false);
            function_ = llvm::Function::Create(
                type, llvm::Function::ExternalLinkage,
                "jyd_shader_vertex", module_);
        } else {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder_.getInt32Ty(),
                {pointerType, pointerType, pointerType, pointerType, pointerType},
                false);
            function_ = llvm::Function::Create(
                type, llvm::Function::ExternalLinkage,
                "jyd_shader_fragment", module_);
        }
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(
            module_.getContext(), "entry", function_);
        builder_.SetInsertPoint(entry);

        auto argument = function_->arg_begin();
        uniforms_ = argument++;
        input_ = argument++;
        if (stage_ == Stage::Vertex) {
            output_ = argument;
        } else {
            texture_ = argument++;
            sampler_ = argument++;
            discard_ = argument;
        }
    }

    void createBuiltins() {
        variables_["mvp"] = {ValueKind::Mat4, {}, 0};
        variables_["vp"] = {ValueKind::Mat4, {}, 16};
        variables_["cameraPosition"] = vectorFromPointer(uniforms_, 32, 3);
        variables_["specularLightDirection"] =
            vectorFromPointer(uniforms_, 35, 3);
        variables_["diffuseLightDirection"] =
            vectorFromPointer(uniforms_, 38, 3);
        variables_["ambientLightColor"] =
            vectorFromPointer(uniforms_, 41, 3);

        variables_["in.position"] = vectorFromPointer(
            input_, 0, stage_ == Stage::Vertex ? 3 : 4);
        variables_["in.normal"] = vectorFromPointer(
            input_, stage_ == Stage::Vertex ? 3 : 4, 3);
        variables_["in.texcoord"] = vectorFromPointer(
            input_, stage_ == Stage::Vertex ? 6 : 7, 2);
    }

    CodeValue emit(const Expression& expression) {
        switch (expression.kind) {
        case Expression::Kind::Number:
            return scalar(expression.number);
        case Expression::Kind::Name: {
            const auto found = variables_.find(expression.text);
            if (found == variables_.end()) {
                fail("unknown variable '" + expression.text + "'");
                return {};
            }
            return found->second;
        }
        case Expression::Kind::Unary:
            return emitUnary(expression);
        case Expression::Kind::Binary:
            return emitBinary(expression);
        case Expression::Kind::Call:
            return emitCall(expression);
        }
        return {};
    }

    CodeValue emitUnary(const Expression& expression) {
        CodeValue operand = emit(*expression.children[0]);
        if (operand.kind == ValueKind::Invalid ||
            operand.kind == ValueKind::Mat4) {
            fail("unary '-' requires a scalar or vector");
            return {};
        }
        for (llvm::Value*& component : operand.components) {
            component = builder_.CreateFNeg(component);
        }
        return operand;
    }

    CodeValue emitBinary(const Expression& expression) {
        CodeValue left = emit(*expression.children[0]);
        CodeValue right = emit(*expression.children[1]);
        if (left.kind == ValueKind::Invalid ||
            right.kind == ValueKind::Invalid ||
            left.kind == ValueKind::Mat4 ||
            right.kind == ValueKind::Mat4) {
            fail("binary operators require scalar or vector operands");
            return {};
        }

        const int leftSize = left.kind == ValueKind::Scalar
            ? 1 : vectorSize(left.kind);
        const int rightSize = right.kind == ValueKind::Scalar
            ? 1 : vectorSize(right.kind);
        if (leftSize != rightSize && leftSize != 1 && rightSize != 1) {
            fail("vector sizes do not match");
            return {};
        }
        const int resultSize = leftSize > rightSize ? leftSize : rightSize;
        CodeValue result;
        result.kind = resultSize == 1
            ? ValueKind::Scalar : vectorKind(resultSize);
        for (int component = 0; component < resultSize; ++component) {
            llvm::Value* a = left.components[leftSize == 1 ? 0 : component];
            llvm::Value* b = right.components[rightSize == 1 ? 0 : component];
            if (expression.text == "+") {
                result.components.push_back(builder_.CreateFAdd(a, b));
            } else if (expression.text == "-") {
                result.components.push_back(builder_.CreateFSub(a, b));
            } else if (expression.text == "*") {
                result.components.push_back(builder_.CreateFMul(a, b));
            } else {
                result.components.push_back(builder_.CreateFDiv(a, b));
            }
        }
        return result;
    }

    CodeValue emitCall(const Expression& expression) {
        std::vector<CodeValue> arguments;
        arguments.reserve(expression.children.size());
        for (const auto& child : expression.children) {
            arguments.push_back(emit(*child));
            if (arguments.back().kind == ValueKind::Invalid) {
                return {};
            }
        }

        if (expression.text == "transform_point") {
            return transform(arguments, true);
        }
        if (expression.text == "transform_direction") {
            return transform(arguments, false);
        }
        if (expression.text == "normalize") {
            return normalize(arguments);
        }
        if (expression.text == "dot") {
            return dot(arguments);
        }
        if (expression.text == "clamp") {
            return clamp(arguments);
        }
        if (expression.text == "sample") {
            return sample(arguments);
        }
        if (expression.text == "vec2" ||
            expression.text == "vec3" ||
            expression.text == "vec4") {
            const int expected = expression.text.back() - '0';
            if (static_cast<int>(arguments.size()) != expected) {
                fail(expression.text + " requires " +
                     std::to_string(expected) + " scalar arguments");
                return {};
            }
            CodeValue result;
            result.kind = vectorKind(expected);
            for (const CodeValue& argument : arguments) {
                if (argument.kind != ValueKind::Scalar) {
                    fail(expression.text + " arguments must be scalar");
                    return {};
                }
                result.components.push_back(argument.components[0]);
            }
            return result;
        }

        fail("unknown function '" + expression.text + "'");
        return {};
    }

    CodeValue transform(const std::vector<CodeValue>& arguments, bool point) {
        if (arguments.size() != 2 ||
            arguments[0].kind != ValueKind::Mat4 ||
            arguments[1].kind != ValueKind::Vec3) {
            fail(point
                ? "transform_point requires (mat4, vec3)"
                : "transform_direction requires (mat4, vec3)");
            return {};
        }
        std::vector<llvm::Value*> input = arguments[1].components;
        input.push_back(llvm::ConstantFP::get(
            builder_.getFloatTy(), point ? 1.0 : 0.0));
        CodeValue result;
        result.kind = point ? ValueKind::Vec4 : ValueKind::Vec3;
        const int outputSize = point ? 4 : 3;
        for (int row = 0; row < outputSize; ++row) {
            llvm::Value* value = llvm::ConstantFP::get(
                builder_.getFloatTy(), 0.0);
            for (int column = 0; column < 4; ++column) {
                value = builder_.CreateFAdd(
                    value,
                    builder_.CreateFMul(
                        loadFloat(
                            uniforms_,
                            arguments[0].matrixOffset + row * 4 + column),
                        input[column]));
            }
            result.components.push_back(value);
        }
        return result;
    }

    CodeValue normalize(const std::vector<CodeValue>& arguments) {
        if (arguments.size() != 1 || vectorSize(arguments[0].kind) == 0) {
            fail("normalize requires one vector argument");
            return {};
        }
        llvm::Value* lengthSquared = llvm::ConstantFP::get(
            builder_.getFloatTy(), 0.0);
        for (llvm::Value* component : arguments[0].components) {
            lengthSquared = builder_.CreateFAdd(
                lengthSquared, builder_.CreateFMul(component, component));
        }
        llvm::Value* minimum = llvm::ConstantFP::get(
            builder_.getFloatTy(), 1.0e-16);
        lengthSquared = builder_.CreateSelect(
            builder_.CreateFCmpOLT(lengthSquared, minimum),
            minimum,
            lengthSquared);
        llvm::Value* length = builder_.CreateUnaryIntrinsic(
            llvm::Intrinsic::sqrt, lengthSquared);
        CodeValue result = arguments[0];
        for (llvm::Value*& component : result.components) {
            component = builder_.CreateFDiv(component, length);
        }
        return result;
    }

    CodeValue dot(const std::vector<CodeValue>& arguments) {
        if (arguments.size() != 2 ||
            vectorSize(arguments[0].kind) == 0 ||
            arguments[0].kind != arguments[1].kind) {
            fail("dot requires two vectors of the same size");
            return {};
        }
        llvm::Value* value = llvm::ConstantFP::get(
            builder_.getFloatTy(), 0.0);
        for (std::size_t component = 0;
             component < arguments[0].components.size(); ++component) {
            value = builder_.CreateFAdd(
                value,
                builder_.CreateFMul(
                    arguments[0].components[component],
                    arguments[1].components[component]));
        }
        return {ValueKind::Scalar, {value}, 0};
    }

    CodeValue clamp(const std::vector<CodeValue>& arguments) {
        if (arguments.size() != 3 ||
            arguments[1].kind != ValueKind::Scalar ||
            arguments[2].kind != ValueKind::Scalar ||
            arguments[0].kind == ValueKind::Mat4) {
            fail("clamp requires (scalar-or-vector, scalar, scalar)");
            return {};
        }
        CodeValue result = arguments[0];
        for (llvm::Value*& component : result.components) {
            component = builder_.CreateSelect(
                builder_.CreateFCmpOLT(component, arguments[1].components[0]),
                arguments[1].components[0], component);
            component = builder_.CreateSelect(
                builder_.CreateFCmpOGT(component, arguments[2].components[0]),
                arguments[2].components[0], component);
        }
        return result;
    }

    CodeValue sample(const std::vector<CodeValue>& arguments) {
        if (stage_ != Stage::Fragment ||
            arguments.size() != 1 ||
            arguments[0].kind != ValueKind::Vec2) {
            fail("sample requires one vec2 argument in the fragment stage");
            return {};
        }
        llvm::FunctionType* samplerType = llvm::FunctionType::get(
            builder_.getInt32Ty(),
            {builder_.getPtrTy(), builder_.getFloatTy(), builder_.getFloatTy()},
            false);
        llvm::Value* texel = builder_.CreateCall(
            samplerType,
            sampler_,
            {texture_, arguments[0].components[0], arguments[0].components[1]});
        CodeValue result;
        result.kind = ValueKind::Vec4;
        for (int channel = 0; channel < 4; ++channel) {
            llvm::Value* bits = builder_.CreateAnd(
                builder_.CreateLShr(texel, channel * 8),
                llvm::ConstantInt::get(builder_.getInt32Ty(), 0xff));
            result.components.push_back(builder_.CreateFDiv(
                builder_.CreateUIToFP(bits, builder_.getFloatTy()),
                llvm::ConstantFP::get(builder_.getFloatTy(), 255.0)));
        }
        return result;
    }

    bool finishVertex() {
        return storeRequired("position", ValueKind::Vec4, 0) &&
            storeRequired("normal", ValueKind::Vec3, 4) &&
            storeRequired("texcoord", ValueKind::Vec2, 7) &&
            (builder_.CreateRetVoid(), true);
    }

    bool storeRequired(const char* name, ValueKind kind, int offset) {
        const auto found = variables_.find(name);
        if (found == variables_.end()) {
            return fail(std::string("missing '") + name + "' output");
        }
        if (found->second.kind != kind) {
            return fail(std::string("output '") + name + "' has the wrong type");
        }
        for (std::size_t component = 0;
             component < found->second.components.size(); ++component) {
            storeFloat(
                output_, offset + static_cast<int>(component),
                found->second.components[component]);
        }
        return true;
    }

    bool finishFragment() {
        const auto color = variables_.find("color");
        if (color == variables_.end() || color->second.kind != ValueKind::Vec4) {
            return fail("fragment stage must produce a vec4 'color'");
        }

        llvm::Value* packed = llvm::ConstantInt::get(builder_.getInt32Ty(), 0);
        for (int channel = 0; channel < 4; ++channel) {
            llvm::Value* component = color->second.components[channel];
            llvm::Value* zero = llvm::ConstantFP::get(builder_.getFloatTy(), 0.0);
            llvm::Value* one = llvm::ConstantFP::get(builder_.getFloatTy(), 1.0);
            component = builder_.CreateSelect(
                builder_.CreateFCmpOLT(component, zero), zero, component);
            component = builder_.CreateSelect(
                builder_.CreateFCmpOGT(component, one), one, component);
            llvm::Value* byte = builder_.CreateFPToUI(
                builder_.CreateFMul(
                    component,
                    llvm::ConstantFP::get(builder_.getFloatTy(), 255.0)),
                builder_.getInt32Ty());
            packed = builder_.CreateOr(
                packed, builder_.CreateShl(byte, channel * 8));
        }

        llvm::Value* discardValue = llvm::ConstantInt::get(
            builder_.getInt32Ty(), 0);
        const auto discard = variables_.find("discard");
        if (discard != variables_.end()) {
            if (discard->second.kind != ValueKind::Scalar) {
                return fail("fragment 'discard' output must be scalar");
            }
            discardValue = builder_.CreateZExt(
                builder_.CreateFCmpONE(
                    discard->second.components[0],
                    llvm::ConstantFP::get(builder_.getFloatTy(), 0.0)),
                builder_.getInt32Ty());
        }
        builder_.CreateStore(discardValue, discard_);
        builder_.CreateRet(packed);
        return true;
    }

    bool fail(const std::string& message) {
        if (error_.empty()) {
            error_ = (stage_ == Stage::Vertex ? "vertex: " : "fragment: ") +
                message;
        }
        return false;
    }

    llvm::Module& module_;
    llvm::IRBuilder<> builder_;
    Stage stage_;
    std::string& error_;
    llvm::Function* function_ = nullptr;
    llvm::Value* uniforms_ = nullptr;
    llvm::Value* input_ = nullptr;
    llvm::Value* output_ = nullptr;
    llvm::Value* texture_ = nullptr;
    llvm::Value* sampler_ = nullptr;
    llvm::Value* discard_ = nullptr;
    std::unordered_map<std::string, CodeValue> variables_;
};

} // namespace

bool buildModule(
    std::string_view source,
    llvm::LLVMContext& context,
    const llvm::DataLayout& dataLayout,
    std::unique_ptr<llvm::Module>& module,
    std::string& shaderName,
    std::string& error) {
    ShaderSyntax syntax;
    Parser parser(source);
    if (!parser.parse(syntax, error)) {
        return false;
    }

    auto result = std::make_unique<llvm::Module>(
        "jyd_shader_" + syntax.name, context);
    result->setDataLayout(dataLayout);

    StageCompiler vertexCompiler(
        *result, StageCompiler::Stage::Vertex, error);
    if (!vertexCompiler.compile(syntax.vertex)) {
        return false;
    }
    StageCompiler fragmentCompiler(
        *result, StageCompiler::Stage::Fragment, error);
    if (!fragmentCompiler.compile(syntax.fragment)) {
        return false;
    }

    shaderName = std::move(syntax.name);
    module = std::move(result);
    return true;
}

} // namespace jyd::shader_language
