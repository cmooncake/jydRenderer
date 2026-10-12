# 光栅化 PBR：逐步实施手册

初始代码核对日期：2026-10-10；手册初始基线：`922fef3`。最新验收：2026-10-12，第 1 步基于 `f51b871` 之后的工作区改动完成。旧版手册标记的 `eb0e0ba` 不再作为当前状态的依据。

这是一份分步实施手册：**第 0–1 步已完成，第 2–8 步待执行**。更新本文不代表渲染器已完成 PBR。下面每步先说明该步开始前的状况，再说明目标和具体执行代码。相机修正、统一布局常量、ABI 版本检查及相应测试已落地；第 2 步起的新增类型、字段、函数和脚本仍是目标代码，必须按依赖顺序实现，不能把最后的 shader 直接交给当前编译器。

## 范围与使用方法

第一版只做 CPU 光栅化、单方向光、基础色贴图、每模型材质、GGX 直接光、线性 HDR 和统一显示转换。暂不做阴影、IBL、法线贴图、透明、折射、体积、路径追踪、热更新和材质编辑器。没有间接光时背光面为黑，这是模型的结果。

保留 Native C++、内置 LLVM Common、脚本 shader 三条路径。C++ 参考函数用于验证脚本数值，不把 PBR 永久藏进采样回调，也不要求重编 LLVM 本身。

代码块分两类：标为“完整新增文件”的可以作为该文件初版；标为“替换函数/插入片段”的须放入指定类或函数，并删除对应旧代码。头文件依赖在完整文件中列出；插入片段需要按说明补 include。测试中的 `check`/`near` 在第 0 步定义。命令均从仓库根目录执行。

特别注意：本项目 `vec3 * vec3` 返回**点积 float**；shader 语言里的两个 vec3 相乘却是**逐分量乘法**。C++ 颜色乘法统一使用本文定义的 `mul(a,b)`，不要逐字照抄 shader 运算符。

## 执行顺序

- [x] 0. 保存现状，建立数值测试，修正透视矩阵
- [x] 1. 集中 C++ / JIT 数据布局与 ABI 版本
- [ ] 2. 传递世界位置，统一法线/光照空间，补齐插值
- [ ] 3. 添加每模型材质与方向光 uniforms
- [ ] 4. 实现最小数学内置函数、分量读取与构造器
- [ ] 5. 迁移线性浮点片元输出和基础色采样回调
- [ ] 6. 接入 HDR 缓冲、显示转换和原有模式分流
- [ ] 7. 实现完整 C++ PBR 参考和完整 PBR shader
- [ ] 8. 注册测试、接入选择入口、完成回归

每步做完先编译和验证，确认后再独立提交。本文不会自动替你提交，也不要求清理已有本地改动。

## 0. 保存现状，建立测试，修正相机

完成记录（2026-10-10）：已将透视矩阵改为全零初始化并明确设置 `proj[3][3]=0`，新增 `tools/test_support.hpp`、`tools/camera_tests.cpp`，在 CMake 中注册 `camera` 测试。提交前重新构建了 Debug/Release 的 `jydRenderer`、`jydCameraTests` 和 `jydLlvmSmoke`；两种配置的 camera CTest 与 LLVM smoke 均通过。本轮未重新做窗口截图对照。构建仍有注释编码、变量遮蔽及数值转换警告，未在本步顺带清理。

### 现在的状况

已经有 `jydRenderer` 和 `jydLlvmSmoke`。后者检查 JIT、内置 shader 和脚本 Common 的基本输出，但主要使用单位矩阵，不能证明真实相机投影正确。当前没有本文后续的 `jydPbrTests` 目标。

本步开始前，`include/camera.hpp::perspProjection()` 用 `identity<4>()` 初始化，仅设置 `proj[3][2]=-1`，因此右下角仍为 1，位置向量的 `w_clip` 变成 `1-z`。该问题现已按下文修正，保留这里的说明作为问题与修复依据。

### 目标

保留一份可复现基线；用数值测试证明标准透视投影的 `w_clip=-z`、近远平面和视场角映射正确。测试在 Release 下也必须生效，不依赖会被 `NDEBUG` 删除的 `assert`。

### 如何执行

#### 0.1 先运行旧测试并记录环境

```powershell
git status --short
git rev-parse HEAD
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
.\build\vs2022-x64\Debug\jydLlvmSmoke.exe
cmake --build --preset vs2022-x64-release
.\build\vs2022-x64\Release\jydLlvmSmoke.exe
```

每条命令成功后再执行下一条。找不到 `cmake` 时进入 VS Developer PowerShell；不要因此修改项目源码。保存同一模型、相机和贴图的截图，注明所用 shader。截图与构建产物放本地 `tmp/`，不要默认提交。

#### 0.2 新增可在后续各步复用的测试辅助函数

完整新增文件：`tools/test_support.hpp`。

```cpp
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

inline void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
inline bool near(float actual, float expected,
                 float absTol = 1e-5f, float relTol = 1e-4f) {
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::abs(actual - expected) <=
               absTol + relTol * std::abs(expected);
}
```

#### 0.3 替换透视投影函数

位置：`include/camera.hpp::Camera::perspProjection()`。这里的近远距离 `n_`、`f_` 为正数，相机朝局部 `-Z`，NDC 深度范围为 `[-1,1]`。

```cpp
mat4 perspProjection() const {
    mat4 proj{}; // 全零，不能留下右下角的 1
    proj[0][0] = 2.0f * n_ / (r_ - l_);
    proj[1][1] = 2.0f * n_ / (t_ - b_);
    proj[0][2] = (r_ + l_) / (r_ - l_);
    proj[1][2] = (t_ + b_) / (t_ - b_);
    proj[2][2] = -(f_ + n_) / (f_ - n_);
    proj[2][3] = -2.0f * f_ * n_ / (f_ - n_);
    proj[3][2] = -1.0f;
    proj[3][3] = 0.0f;
    return proj;
}
```

前提是 `0 < n_ < f_`、`0 < fovY_ < pi`、宽高为正；相机入口应拒绝非法值，而不是在矩阵分母里随意加 epsilon。

完整新增文件：`tools/camera_tests.cpp`。

```cpp
#include "camera.hpp"
#include "test_support.hpp"
#include <iostream>

int main() {
    try {
        jyd::Camera c(800, 400, 1.57079632679f);
        const mat4 p = c.perspProjection();
        const vec4 nearClip = p * vec4(0, 0, -c.n_, 1);
        const vec4 farClip  = p * vec4(0, 0, -c.f_, 1);
        check(near(nearClip.w, c.n_), "near clip w");
        check(near(nearClip.z / nearClip.w, -1), "near NDC z");
        check(near(farClip.z / farClip.w, 1), "far NDC z");
        const vec4 top = p * vec4(0, c.t_, -c.n_, 1);
        check(near(top.y / top.w, 1), "vertical FOV edge");

        // 从 NDC 反推方向，再把射线上的点投影回来。
        const float nx = 0.5f, ny = -0.25f;
        const float h = std::tan(c.fovY_ * 0.5f);
        const vec3 direction = normalize(vec3(
            nx * c.aspect_ratio_ * h, ny * h, -1));
        const vec4 q = p * vec4(direction * 4.0f, 1);
        check(near(q.x / q.w, nx), "ray/project round trip x");
        check(near(q.y / q.w, ny), "ray/project round trip y");
        std::cout << "camera tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
```

在根 `CMakeLists.txt` 中加入一次 `include(CTest)`，并增加以下目标；后续第 8 步扩充同一测试区块，不要重复定义目标。

```cmake
include(CTest)
if(BUILD_TESTING)
    add_executable(jydCameraTests tools/camera_tests.cpp)
    target_include_directories(jydCameraTests PRIVATE include tools)
    if(MSVC)
        target_compile_options(jydCameraTests PRIVATE /utf-8)
    endif()
    add_test(NAME camera COMMAND jydCameraTests)
endif()
```

先在旧矩阵上运行，应失败于 `near clip w`；替换后运行，应通过。这样测试确实捕获了错误，不只是恰好给出绿色输出。

### 验证与本步结束点

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug --target jydCameraTests
ctest --test-dir build/vs2022-x64 -C Debug -R '^camera$' --output-on-failure
```

旧模型画面可能因修正投影而改变，这是预期的数学变化，需重建截图基线，不要把新图强行调回错误投影。建议将相机修正和测试作为独立提交，不与后续 ABI 重构混在一起。

## 1. 集中 C++ / JIT 数据布局

完成记录（2026-10-12）：`include/shader_abi.hpp` 已统一输入、varying 和 uniform 布局，宿主包装器、脚本编译器和内置手写 IR 均使用字段常量。DLL 导出 ABI 版本，两个包装器构造函数及 smoke 入口检查版本。仍保持 Version=1、顶点输入=8、varying=9、uniform=44，不提前引入第 2 步的世界位置。

`tools/llvm_smoke.cpp` 新增独立的 `testRowMajorMatrix()`，分别测试内置与脚本 Common 的非对称 MVP、非对称 View、法线和 UV；新增 `testUniformFields()`，用诊断 shader 逐字段读取相机、镜面光、漫反射光及环境色，避免未使用字段漏测。测试数组与原 identity 回归隔离，shader handle 由 RAII 管理，失败时也会释放。核对时修正了手写 IR 中 `I::Normal + 3/4/5` 的重复偏移错误，正确写法是 `I::Normal + 0/1/2`。

Debug/Release 的渲染器与测试构建、新旧 LLVM smoke、camera CTest 均通过。本轮没有运行窗口截图对照或注入旧 DLL 的版本不匹配场景；测试直接填 uniform 数组，不宣称已单独执行覆盖 `prepare()` 的矩阵打包。已有注释编码等编译警告未在本步扩大清理范围。

### 现在的状况

C++ 与 JIT 通过连续 float 数组交换数据，偏移在 `src/llvm_shader.cpp`、`src/llvm_support.cpp`、`src/shader_language.cpp`、`tools/llvm_smoke.cpp` 中重复硬编码。

| 缓冲 | 当前字段偏移，单位为 float | 长度 |
| --- | --- | --- |
| 顶点输入 | position=0，normal=3，UV=6 | 8 |
| varying | clip position=0，normal=4，UV=7 | 9 |
| uniform | mvp=0，view=16，camera=32，specularLight=35，diffuseLight=38，ambient=41 | 44 |

`CommonShader::vp` 实际装的是 view 矩阵，不是 view-projection。当前桥接接口没有显式 ABI 版本。

### 目标

本步不改变布局、光照公式和输出颜色，只把偏移集中起来，并让宿主在使用编译器前检查 ABI。将来的字段扩展能查到唯一来源。

### 如何执行

#### 1.1 新增布局头文件

完整新增文件：`include/shader_abi.hpp`。以下是**第 1 步版本**；第 2、3、5 步分别升级，不要现在就把后面的长度提前改进来。

```cpp
#pragma once
#include <cstdint>
namespace jyd::shader_abi {
inline constexpr std::uint32_t Version = 1;
namespace VertexIn {
inline constexpr int Position = 0, Normal = 3, Texcoord = 6, Count = 8;
}
namespace Varying {
inline constexpr int Position = 0, Normal = 4, Texcoord = 7, Count = 9;
}
namespace Uniform {
inline constexpr int Mvp = 0, View = 16, Camera = 32;
inline constexpr int SpecularLight = 35, DiffuseLight = 38, Ambient = 41;
inline constexpr int Count = 44;
}
}
```

在 `include/llvm_shader.hpp` 包含该头，将数组改为：

```cpp
std::array<float, shader_abi::Uniform::Count> uniforms_{};
```

`src/llvm_shader.cpp::prepare()` 替换成：

```cpp
void LlvmCommonShader::prepare() {
    namespace U = shader_abi::Uniform;
    writeMatrix(uniforms_.data() + U::Mvp, mvp);
    writeMatrix(uniforms_.data() + U::View, vp);
    writeVector(uniforms_.data() + U::Camera, cameraPosition);
    writeVector(uniforms_.data() + U::SpecularLight, SpecularLightDirection);
    writeVector(uniforms_.data() + U::DiffuseLight, DiffuseLightDirection);
    writeVector(uniforms_.data() + U::Ambient, AmbientLightColor);
}
```

顶点和片元包装器里的 `float[8]`、`float[9]` 同步改成 `VertexIn::Count`、`Varying::Count`；打包、解包也改用字段常量，不要只替换数组总长。

编译器 `StageCompiler::createBuiltins()` 中，例如：

```cpp
variables_["mvp"] = {ValueKind::Mat4, {}, shader_abi::Uniform::Mvp};
variables_["vp"]  = {ValueKind::Mat4, {}, shader_abi::Uniform::View};
variables_["cameraPosition"] = vectorFromPointer(
    uniforms_, shader_abi::Uniform::Camera, 3);
```

其余字段照表替换。手写 IR 的 `loadFloat(builder, uniforms, 38 + component)` 也必须变成 `shader_abi::Uniform::DiffuseLight + component`。矩阵内部的 `row*4+column` 是行主序索引，不属于应删掉的布局魔法数字。

#### 1.2 在调用编译入口前检查版本

在 `include/llvm_support.hpp` 的 `namespace jyd` 内、末尾 `#undef` 前增加声明；在 `src/llvm_support.cpp` 对应命名空间中增加定义：

```cpp
// llvm_support.hpp
extern "C" JYD_SHADER_COMPILER_API std::uint32_t jydShaderAbiVersion();

// llvm_support.cpp；此文件同时 include "shader_abi.hpp"
extern "C" std::uint32_t jydShaderAbiVersion() {
    return shader_abi::Version;
}
```

`src/llvm_shader.cpp` 增加局部辅助函数，两个构造函数的首行都调用它，且在 `jydCompile...()` 前调用：

```cpp
void requireShaderAbi() {
    if (jyd::jydShaderAbiVersion() != jyd::shader_abi::Version)
        throw std::runtime_error("shader compiler ABI mismatch; rebuild EXE and DLL");
}
```

smoke 测试也先检查版本。旧 DLL 没有这个导出时，动态加载阶段就可能失败；这是需要成套部署的明确不兼容，不是能靠传空指针兼容的情况。此方案不更改 `JydShaderProgram` 大小，避免先让旧 DLL 写入未知大小的结构后才检查版本。

#### 1.3 用非对称矩阵检查打包

落地位置：`tools/llvm_smoke.cpp` 的匿名命名空间中，函数名为 `testRowMajorMatrix(const jyd::JydShaderProgram&)`。`runSmokeTests()` 在内置 Common 编译成功后调用一次，在脚本 Common 编译成功后再调用一次；函数使用自己的 uniform/input/output 数组，不修改原 identity 用例的数据。下方是核心原理片段，现有工程已实现，无需再重复粘贴。

在 smoke 的独立测试用例中填 `mvp`；测试完不要把该矩阵混入原 identity 用例。

```cpp
const float matrix[16] = {
    1, 2, 0, 3,
    0, 4, 5, 6,
    7, 0, 8, 9,
    0, 0, 0, 1
};
std::copy(matrix, matrix + 16,
          uniforms + jyd::shader_abi::Uniform::Mvp);
shader.vertex(uniforms, vertexInput, varying);
// input.position=(1,2,3) 时，预期 clip=(8,29,40,1)。
check(near(varying[0], 8) && near(varying[1], 29) &&
      near(varying[2], 40) && near(varying[3], 1), "row-major matrix ABI");
```

这里的 `varying` 必须在设置矩阵之后实际调用 `shader.vertex(...)` 得到；不要只检查旧数组。给所有非矩阵字段填不同数值，逐字段核对，防止某个偏移错误刚好读到另一个零。

### 验证与本步结束点

Native、内置 JIT、脚本 Common 的既有结果应与本步开始时一致。用 `rg` 搜索旧长度及偏移，逐项区分布局常量和正常数学常数。此时 `Version=1`、varying=9、uniform=44。

## 2. 打通世界位置、法线和插值

### 现在的状况

`Commonv2f` 只有裁剪位置、法线和 UV。顶点 shader 把法线乘了 view 矩阵；Native 的 `cameraPosition - vec3(f.position)` 却把世界位置与裁剪位置相减。默认高光系数为零使这个问题不易看见。已有 `VaryingTraits` 统一处理裁剪边和透视插值，应该在这里扩展，而不是在光栅循环中再写一套。

### 目标

保持裁剪位置只负责投影、裁剪和深度；新增世界位置供光照使用。第一版 model 矩阵为单位矩阵，即模型加载后的坐标视作世界坐标。位置、法线、光方向和相机位置都在世界空间参与着色。

### 如何执行

#### 2.1 准备独立于 LLVM 的数学辅助函数

完整新增文件：`include/render_math.hpp`。不改变原 `vec.hpp` 中 `operator*` 的含义。

```cpp
#pragma once
#include "vec.hpp"
#include <algorithm>
#include <cmath>

namespace jyd {
inline bool finite3(vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline vec3 safeNormalize(vec3 v) {
    const float l2 = v * v;
    if (!std::isfinite(l2) || l2 <= 1e-16f) return vec3(0);
    return v / std::sqrt(l2);
}
inline vec3 mul(vec3 a, vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
inline vec3 saturate3(vec3 v) {
    return {std::clamp(v.x, 0.0f, 1.0f),
            std::clamp(v.y, 0.0f, 1.0f),
            std::clamp(v.z, 0.0f, 1.0f)};
}
}
```

#### 2.2 替换 varying 与 Native 顶点输出

`include/shader.hpp` 包含 `render_math.hpp`，替换结构与 `NativeCommonShader::vertex()`：

```cpp
struct Commonv2f {
    vec4 position;       // clip space，不能用于 world-space 光照
    vec3 normal;         // world space，片元使用前归一化
    vec2 texcoord;
    vec3 worldPosition;
};

Commonv2f vertex(const Commona2v& a) const override {
    return {mvp * vec4(a.position, 1.0f),
            a.normal, a.texcoord, a.position};
}
```

第 2 步不增加模型变换。将来加入 model 矩阵时，位置乘 model，法线乘 model 线性部分的逆转置，不能把这个扩展悄悄省略。

`shader_abi.hpp` 改为 `Version=2`，只修改 varying 命名空间：

```cpp
namespace Varying {
inline constexpr int Position = 0, Normal = 4, Texcoord = 7;
inline constexpr int WorldPosition = 9, Count = 12;
}
```

#### 2.3 完整替换两个插值函数

位置：`VaryingTraits<Commonv2f>`。保留原 `clipPosition()`。

```cpp
static Commonv2f interpolateEdge(
    const Commonv2f& a, const Commonv2f& b, float t) {
    return {
        a.position + (b.position - a.position) * t,
        a.normal + (b.normal - a.normal) * t,
        a.texcoord + (b.texcoord - a.texcoord) * t,
        a.worldPosition + (b.worldPosition - a.worldPosition) * t
    };
}

static Commonv2f interpolateFragment(
    const Commonv2f (&v)[3], float a, float b, float c) {
    // 调用方保留 w 有限、非零和分母有效性检查。
    const float q0 = a / v[0].position.w;
    const float q1 = b / v[1].position.w;
    const float q2 = c / v[2].position.w;
    const float s = q0 + q1 + q2;
    return {
        (v[0].position*q0 + v[1].position*q1 + v[2].position*q2) / s,
        (v[0].normal*q0 + v[1].normal*q1 + v[2].normal*q2) / s,
        (v[0].texcoord*q0 + v[1].texcoord*q1 + v[2].texcoord*q2) / s,
        (v[0].worldPosition*q0 + v[1].worldPosition*q1 +
         v[2].worldPosition*q2) / s
    };
}
```

法线在裁剪和插值过程中不反复归一化；在每个 fragment 入口调用 `safeNormalize`。不要改成仿射插值，也不要把 NDC 深度再做一次上述透视修正。`src/renderer.cpp` 已有分母检查，补上 `std::isfinite(denominator)`，继续在 `discard` 后才写深度。

#### 2.4 改完全部生产者与消费者

`src/llvm_shader.cpp::vertex()` 解包时追加第四个成员：

```cpp
namespace V = shader_abi::Varying;
return {
    vec4(shaderOutput[V::Position], shaderOutput[V::Position+1],
         shaderOutput[V::Position+2], shaderOutput[V::Position+3]),
    vec3(shaderOutput[V::Normal], shaderOutput[V::Normal+1], shaderOutput[V::Normal+2]),
    vec2(shaderOutput[V::Texcoord], shaderOutput[V::Texcoord+1]),
    vec3(shaderOutput[V::WorldPosition], shaderOutput[V::WorldPosition+1],
         shaderOutput[V::WorldPosition+2])
};
```

`LlvmCommonShader::fragment()` 的输入数组由原 9 个值变成 12 个值，最后追加 `input.worldPosition.x/y/z`。已有数组常量会自动变长，但自动补零不是正确传值，必须显式写入。

在 `StageCompiler::createBuiltins()` 中仅 fragment 阶段增加输入；在 `finishVertex()` 中强制输出它：

```cpp
if (stage_ == Stage::Fragment) {
    variables_["in.worldPosition"] = vectorFromPointer(
        input_, shader_abi::Varying::WorldPosition, 3);
}
// finishVertex() 的目标实现：
return storeRequired("position", ValueKind::Vec4, shader_abi::Varying::Position) &&
       storeRequired("normal", ValueKind::Vec3, shader_abi::Varying::Normal) &&
       storeRequired("texcoord", ValueKind::Vec2, shader_abi::Varying::Texcoord) &&
       storeRequired("worldPosition", ValueKind::Vec3,
                     shader_abi::Varying::WorldPosition) &&
       (builder_.CreateRetVoid(), true);
```

手写 `buildCommonVertexFunction()` 保留 mvp 变换，删除 view 变换法线的整段计算，改为把输入 normal 的三个分量直接写到输出 normal；同时将输入 position 的三个分量写到 `WorldPosition`：

```cpp
for (int c = 0; c < 3; ++c) {
    storeFloat(builder, output, shader_abi::Varying::Normal + c,
               loadFloat(builder, input, shader_abi::VertexIn::Normal + c));
    storeFloat(builder, output, shader_abi::Varying::WorldPosition + c,
               loadFloat(builder, input, shader_abi::VertexIn::Position + c));
}
```

两个现有脚本 `Common` 和 `Unlit` 的 vertex 都改成：

```text
vertex {
    position = transform_point(mvp, in.position);
    normal = in.normal;
    texcoord = in.texcoord;
    worldPosition = in.position;
}
```

旧脚本缺少输出时明确报错，不能默认世界位置为零。此时无需分量访问新语法。

#### 2.5 统一旧 Common 的光照约定

在 Native 中使用 `N=safeNormalize(f.normal)`，观察方向改成 `safeNormalize(cameraPosition-f.worldPosition)`；所有 `-f.normal` 改成明确的外向法线 `N`。`DiffuseLightDirection` 与 `SpecularLightDirection` 暂时保留旧字段名，但含义统一为“从表面指向光源”，在世界空间定义。

为了保留旧漫反射的大致照明方向，`Renderer::DiffuseLightDirection()` 默认改为 `(0,0,1)`；脚本用 `dot(diffuseLightDirection,normalize(in.normal))`，手写 IR 删除法线上的 `FNeg` 并对片元法线做安全归一化。第 4 步会把归一化阈值实现统一到与 Native 相同。

默认 Common 高光仍为零，不在本步引入新高光模型。若手动开启旧高光，必须同时使用修正后的世界位置与世界法线。

### 验证与本步结束点

用 clip w 为 1、2、4，屏幕重心权重各为 1/3 的三角形测试。设三个世界位置为 `(0,0,0)`、`(2,0,0)`、`(0,4,0)`，预期插值位置为 `(4/7,4/7,0)`，不是 `(2/3,4/3,0)`。旋转相机时，世界法线可视化应保持模型本身的颜色，而不是跟着视图旋转改变。

Native、内置 IR、脚本 vertex 对相同输入须给出相同的 12 个分量。此步结束时 `Version=2`、varying=12、uniform=44。

## 3. 每模型材质和方向光参数

### 现在的状况

`RenderItem` 只有 model 和 texture。`CommonShader` 只有旧灯光字段；`prepare()` 上传 44 个 float。改变材质没有独立的数据入口，也没有基础色因子、金属度和粗糙度。

### 目标

每个模型可有独立参数，不重新编译 shader 就能改变材质。同一次 draw 的 uniforms 全部来自该模型，避免复用 shader 对象导致参数串用。参数类型与 LLVM 解耦。

### 如何执行

#### 3.1 新增完整材质头文件

完整新增文件：`include/material.hpp`。

```cpp
#pragma once
#include "render_math.hpp"
#include <stdexcept>

namespace jyd {
struct MaterialParameters {
    vec3 baseColorFactor{1, 1, 1}; // 线性反射率因子
    float metallic = 0.0f;
    float roughness = 0.5f;
};
struct DirectionalLight {
    vec3 surfaceToLightDirection{0, 0, 1};
    vec3 lightRadiance{3, 3, 3};
};
inline MaterialParameters sanitizeMaterial(MaterialParameters m) {
    if (!finite3(m.baseColorFactor) || !std::isfinite(m.metallic) ||
        !std::isfinite(m.roughness))
        throw std::invalid_argument("nonfinite material parameter");
    m.baseColorFactor = saturate3(m.baseColorFactor);
    m.metallic = std::clamp(m.metallic, 0.0f, 1.0f);
    m.roughness = std::clamp(m.roughness, 0.05f, 1.0f);
    return m;
}
inline DirectionalLight sanitizeLight(DirectionalLight l) {
    if (!finite3(l.surfaceToLightDirection) || !finite3(l.lightRadiance) ||
        l.lightRadiance.x < 0 || l.lightRadiance.y < 0 || l.lightRadiance.z < 0)
        throw std::invalid_argument("invalid directional light");
    l.surfaceToLightDirection = safeNormalize(l.surfaceToLightDirection);
    if ((l.surfaceToLightDirection * l.surfaceToLightDirection) == 0)
        throw std::invalid_argument("zero light direction");
    return l;
}
}
```

这里 `lightRadiance` 是沿确定方向的入射权重，沿用常见工程命名；第一版没有完整物理单位标定，不把它当作普通连续方向的点值密度。`roughness>=0.05` 是本版数值策略，不是材料的物理定律。

#### 3.2 给模型和 shader 各放一份参数

在 `src/main.cpp::RenderItem` 中追加：

```cpp
jyd::MaterialParameters material{};
```

在 `CommonShader` 中追加：

```cpp
MaterialParameters material{};
DirectionalLight light{};
```

两个文件包含 `material.hpp`。在每次调用 `renderer.Pipeline()` **之前**赋值：

```cpp
shader->texture = &item.texture;
shader->material = jyd::sanitizeMaterial(item.material);
shader->light = jyd::sanitizeLight(jyd::DirectionalLight{});
totalTriangles += renderer.Pipeline(item.model, *shader, mod);
```

以后把共享灯对象放到场景中即可；本步默认一盏灯。`Pipeline()` 已会设置相机矩阵并调用 `shader.prepare()`，因此不能在 `prepare()` 之后才赋材质。

#### 3.3 扩展 uniform，保持旧偏移稳定

`shader_abi.hpp` 升级为 `Version=3`，Uniform 中保留原字段，追加：

```cpp
inline constexpr int BaseColor = 44;      // 44..46
inline constexpr int Metallic = 47;
inline constexpr int Roughness = 48;
inline constexpr int LightDirection = 49; // 49..51
inline constexpr int LightRadiance = 52;  // 52..54
inline constexpr int Count = 55; // 替换旧 Count，不是第二次定义
```

在 `LlvmCommonShader::prepare()` 末尾追加：

```cpp
writeVector(uniforms_.data() + U::BaseColor, material.baseColorFactor);
uniforms_[U::Metallic] = material.metallic;
uniforms_[U::Roughness] = material.roughness;
writeVector(uniforms_.data() + U::LightDirection, light.surfaceToLightDirection);
writeVector(uniforms_.data() + U::LightRadiance, light.lightRadiance);
```

编译器 `createBuiltins()` 追加，不要使用 `vectorFromPointer(...,1)`，它的 `vectorKind(1)` 当前返回 Invalid：

```cpp
namespace U = shader_abi::Uniform;
variables_["baseColorFactor"] = vectorFromPointer(uniforms_, U::BaseColor, 3);
variables_["metallic"] = {ValueKind::Scalar,
                           {loadFloat(uniforms_, U::Metallic)}, 0};
variables_["roughness"] = {ValueKind::Scalar,
                            {loadFloat(uniforms_, U::Roughness)}, 0};
variables_["surfaceToLightDirection"] =
    vectorFromPointer(uniforms_, U::LightDirection, 3);
variables_["lightRadiance"] = vectorFromPointer(uniforms_, U::LightRadiance, 3);
```

### 验证与本步结束点

临时用 `color=vec4(metallic,roughness,0.0,1.0)` 显示参数，两个模型设不同值后交换绘制顺序，参数仍应随模型走。此 shader 只验证数据流，尚不是 PBR。

smoke 的 uniform 长度改为 55，给新增字段填值。检查 NaN、负光强、零光方向会被入口拒绝；金属度与粗糙度按约定限制。此时 `Version=3`、varying=12、uniform=55。

## 第 4 步：让 shader 语言能表达完整的 PBR

### 现在的状况

`src/shader_language.cpp` 已支持算术、`transform_point`、`transform_direction`、`normalize`、`dot`、`clamp`、`sample` 和向量构造，但还没有 `max`、`mix`、`sqrt` 与真正的成员访问。现在 `in.normal` 能用，是因为词法分析器把含点的整串文本当成一个名字；它无法解析 `sample(in.texcoord).rgb`。

另外，当前 `normalize` 把长度平方限制为一个小正数。这会使极小向量变成“很短但不为零”的向量，和第 2 步的 C++ 约定不一致。

### 目标

仅增加本手册需要的表达能力：成员读取、标量广播的 `max`、同型向量/标量的 `mix`、非负平方根、`vec4(vec3,float)`。不顺便引入循环、用户函数或完整 GLSL 类型系统。C++ 与 JIT 的零向量处理必须一致。

### 如何执行

#### 4.1 把点号变成语法，而不是名字的一部分

在 `TokenKind` 增加 `Dot`、`Invalid`。标识符扫描条件改成下面这样，保留后面的数字扫描逻辑，因此 `.5` 仍是数值：

```cpp
if (!std::isalnum(static_cast<unsigned char>(current)) && current != '_') {
    break;
}
```

词法 `switch` 增加 `case '.': token.kind = TokenKind::Dot; break;`，`default` 设置 `token.kind = TokenKind::Invalid`。不能把未知字符当作文件结束，否则末尾的非法字符可能被悄悄忽略。

`Expression::Kind` 增加 `Member`。在 `Parser` 内新增以下完整成员函数，并把 `parseUnary()` 最后的 `return parsePrimary();` 改成 `return parsePostfix();`。一元负号原来的递归分支保留，因此 `-v.x` 表示 `-(v.x)`。

```cpp
std::unique_ptr<Expression> parsePostfix() {
    auto base = parsePrimary();
    if (!base) return nullptr;
    while (current_.kind == TokenKind::Dot) {
        advance();
        if (current_.kind != TokenKind::Identifier) {
            fail("expected a member name after '.'");
            return nullptr;
        }
        auto member = std::make_unique<Expression>();
        member->kind = Expression::Kind::Member;
        member->text = current_.text;
        member->children.push_back(std::move(base));
        advance();
        base = std::move(member);
    }
    return base;
}
```

在 `StageCompiler::emit()` 的分派中加入 `case Expression::Kind::Member: return emitMember(expression);`；下面是新增的完整成员函数。`in` 是保留的输入名字空间，不是一个普通向量。

```cpp
CodeValue emitMember(const Expression& expression) {
    const Expression& base = *expression.children[0];
    if (base.kind == Expression::Kind::Name && base.text == "in") {
        auto it = variables_.find("in." + expression.text);
        if (it != variables_.end()) return it->second;
        fail("unknown input member: " + expression.text);
        return {};
    }
    CodeValue value = emit(base);
    const int count = vectorSize(value.kind);
    const std::string& fields = expression.text;
    if (count == 0 || fields.empty() || fields.size() > 4) {
        fail("swizzle requires a vector and 1 to 4 components");
        return {};
    }
    const std::string alphabet =
        std::string("xyzw").find(fields[0]) != std::string::npos
            ? "xyzw" : "rgba";
    CodeValue result;
    result.kind = fields.size() == 1 ? ValueKind::Scalar
                                    : vectorKind(static_cast<int>(fields.size()));
    for (char c : fields) {
        const auto index = alphabet.find(c);
        if (index == std::string::npos || index >= static_cast<std::size_t>(count)) {
            fail("invalid or mixed swizzle: " + fields);
            return {};
        }
        result.components.push_back(value.components[index]);
    }
    return result;
}
```

允许 `v.xxx`、`v.bgr`；拒绝 vec2 的 `.z`，以及混合字母组的 `.xr`。这里只实现读取，不实现 `v.xy = ...`。

#### 4.2 增加三个内建函数

在 `StageCompiler` 增加下面三个成员函数。它们使用现有 `scalar()`、`vectorSize()`、`fail()` 和 `builder_`，不是需要另找实现的伪接口。

```cpp
CodeValue emitMax(const CodeValue& a, const CodeValue& b) {
    const int na = a.kind == ValueKind::Scalar ? 1 : vectorSize(a.kind);
    const int nb = b.kind == ValueKind::Scalar ? 1 : vectorSize(b.kind);
    if (!na || !nb || (na != nb && na != 1 && nb != 1)) {
        fail("max expects compatible scalars or vectors"); return {};
    }
    CodeValue out;
    out.kind = na >= nb ? a.kind : b.kind;
    for (int i = 0; i < (na > nb ? na : nb); ++i) {
        auto* x = a.components[na == 1 ? 0 : i];
        auto* y = b.components[nb == 1 ? 0 : i];
        out.components.push_back(builder_.CreateSelect(
            builder_.CreateFCmpOGT(x, y), x, y));
    }
    return out;
}

CodeValue emitMix(const CodeValue& a, const CodeValue& b, const CodeValue& t) {
    if (a.kind != b.kind || t.kind != ValueKind::Scalar ||
        (a.kind != ValueKind::Scalar && vectorSize(a.kind) == 0)) {
        fail("mix expects matching values and a scalar weight"); return {};
    }
    CodeValue out;
    out.kind = a.kind;
    auto* weight = t.components[0];
    for (std::size_t i = 0; i < a.components.size(); ++i) {
        out.components.push_back(builder_.CreateFAdd(
            builder_.CreateFMul(a.components[i], builder_.CreateFSub(
                scalar(1.0f).components[0], weight)),
            builder_.CreateFMul(b.components[i], weight)));
    }
    return out;
}

CodeValue emitSqrt(const CodeValue& value) {
    if (value.kind != ValueKind::Scalar && vectorSize(value.kind) == 0) {
        fail("sqrt expects a scalar or vector"); return {};
    }
    CodeValue out;
    out.kind = value.kind;
    auto* sqrtFn = llvm::Intrinsic::getDeclaration(
        &module_, llvm::Intrinsic::sqrt, {builder_.getFloatTy()});
    auto* zero = scalar(0.0f).components[0];
    for (auto* x : value.components) {
        auto* nonNegative = builder_.CreateSelect(
            builder_.CreateFCmpOLT(x, zero), zero, x);
        out.components.push_back(builder_.CreateCall(sqrtFn, {nonNegative}));
    }
    return out;
}
```

在 `emitCall()` 完成参数 `arguments` 的求值后、原有函数分派之前插入以下分派：

```cpp
if (expression.text == "max" && arguments.size() == 2)
    return emitMax(arguments[0], arguments[1]);
if (expression.text == "mix" && arguments.size() == 3)
    return emitMix(arguments[0], arguments[1], arguments[2]);
if (expression.text == "sqrt" && arguments.size() == 1)
    return emitSqrt(arguments[0]);
if (expression.text == "vec4" && arguments.size() == 2 &&
    arguments[0].kind == ValueKind::Vec3 && arguments[1].kind == ValueKind::Scalar) {
    CodeValue out = arguments[0];
    out.kind = ValueKind::Vec4;
    out.components.push_back(arguments[1].components[0]);
    return out;
}
```

构造函数特例必须放在原来“vec4 要求 4 个参数”的检查之前。参数数量错误仍走诊断，不能访问越界参数。这里 `mix` 不会自动限制权重；材质入口已负责把 metallic 限制在 [0,1]。

#### 4.3 对齐 normalize 的边界行为

新增 `<limits>`。把原 `normalize()` 的长度限制和除法替换为下面的逻辑。新增这个完整成员函数，然后将原 `normalize(arguments)` 函数中参数数量/类型检查之后的计算替换为 `return emitSafeNormalize(arguments[0]);`，保留原来的参数检查。

```cpp
CodeValue emitSafeNormalize(const CodeValue& value) {
    if (vectorSize(value.kind) == 0) {
        fail("normalize expects a vector"); return {};
    }
    auto* l2 = scalar(0.0f).components[0];
    for (auto* c : value.components)
        l2 = builder_.CreateFAdd(l2, builder_.CreateFMul(c, c));
    auto* valid = builder_.CreateAnd(
        builder_.CreateFCmpOGT(l2, scalar(1e-16f).components[0]),
        builder_.CreateFCmpOLE(l2,
            scalar(std::numeric_limits<float>::max()).components[0]));
    auto* safeL2 = builder_.CreateSelect(valid, l2, scalar(1.0f).components[0]);
    auto* sqrtFn = llvm::Intrinsic::getDeclaration(
        &module_, llvm::Intrinsic::sqrt, {builder_.getFloatTy()});
    auto* length = builder_.CreateCall(sqrtFn, {safeL2});
    CodeValue out;
    out.kind = value.kind;
    for (auto* c : value.components) {
        out.components.push_back(builder_.CreateSelect(valid,
            builder_.CreateFDiv(c, length), scalar(0.0f).components[0]));
    }
    return out;
}
```

这些是 C++ 中的浮点字面量，可以写 `1e-16f`；当前 shader 词法还不支持科学计数法，shader 文件里的 epsilon 要写成 `0.000001`。先不要给 IR 开 `fast-math`，否则非有限数检查的语义可能改变。

### 验证与本步结束点

用最小 shader 分别编译并执行：`sample(in.texcoord).rgb`、`(vec3(1,2,3)+vec3(2,3,4)).zyx`、`-in.normal.x`、`vec4(vec3(.5,.5,.5),1)`。检查 max 的标量广播、mix 两个端点、sqrt(4)=2、normalize(0)=0。反例包括 vec2.z、vec3.xr、max(mat4,1)、少参数构造，以及文件尾部 `@`。

这一步只扩展语言，不改变 ABI：仍为 Version=3、varying=12、uniform=55。LLVM 的表达式构建背景可参考 [官方前端教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html)。

## 第 5 步：把纹理和片元输出改成线性浮点颜色

### 现在的状况

纹理是 RGBA8；Native 把 RGB 除以 255 就拿去算光照；JIT 的纹理回调和片元函数都用一个 `uint32_t` 打包 RGBA。问题有两个：普通 base-color 图片通常是 sRGB 编码，除以 255 不等于转成线性光；而片元返回字节会把所有大于 1 的亮度提前丢掉。

### 目标

base-color 采样时解码 sRGB；着色过程和片元输出全部保留线性 float；只在最后显示时编码为 sRGB。alpha 是线性覆盖率，不做 sRGB 解码。本阶段按不透明表面渲染，不新增透明混合。

### 如何执行

#### 5.1 定义颜色类型及转换

新增 `include/color_space.hpp`，完整内容如下。这里约定 base-color 图片为 sRGB；以后读取法线、粗糙度等数据贴图，必须另设“不解码”的采样入口。

```cpp
#pragma once
#include "framebuffer.hpp"
#include <algorithm>
#include <cmath>

namespace jyd {
struct LinearColor {
    float r = 0, g = 0, b = 0, a = 1;
};
inline float srgbToLinear(float x) {
    return x <= 0.04045f ? x / 12.92f
        : std::pow((x + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float x) {
    return x <= 0.0031308f ? 12.92f * x
        : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}
inline LinearColor decodeBaseColor(Color c) {
    return {srgbToLinear(c.r / 255.0f), srgbToLinear(c.g / 255.0f),
            srgbToLinear(c.b / 255.0f), c.a / 255.0f};
}
inline bool finiteColor(LinearColor c) {
    return std::isfinite(c.r) && std::isfinite(c.g) &&
           std::isfinite(c.b) && std::isfinite(c.a);
}
inline std::uint8_t encodeLinearByte(float x) {
    // 调用前要求有限。此函数只编码显示值，不应出现在 BRDF 中。
    return static_cast<std::uint8_t>(std::lround(
        linearToSrgb(std::clamp(x, 0.0f, 1.0f)) * 255.0f));
}
}
```

新增 `include/texture_sampling.hpp`，使 Native 和 JIT 回调共享同一套解码。空纹理采用白色默认值；非法 UV/采样异常采用洋红诊断色。异常必须在宿主侧接住，不穿过 JIT/DLL 调用边界。

```cpp
#pragma once
#include "texture.hpp"
#include "color_space.hpp"

namespace jyd {
inline LinearColor sampleBaseColor(const Texture* texture, vec2 uv) noexcept {
    if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) return {1, 0, 1, 1};
    if (!texture) return {1, 1, 1, 1};
    try { return decodeBaseColor(texture->sampleNearest(uv)); }
    catch (...) { return {1, 0, 1, 1}; }
}
}
```

这还不代表 UI 支持不选纹理：当前 `RenderItem` 构造和文件选择校验仍要求纹理路径。本步只定义 shader 层缺省行为，界面放宽是另一个改动。

#### 5.2 一次性迁移浮点 ABI

将 `shader_abi::Version` 改为 **4**，缓冲区长度仍为 varying=12、uniform=55。在 `include/llvm_support.hpp` 中替换两个函数指针类型：

```cpp
using JydTextureSampleFunction = void (*)(
    const void* texture, float u, float v, float* outRGBA);
using JydShaderFragmentFunction = void (*)(
    const float* uniforms, const float* varying,
    const void* texture, JydTextureSampleFunction sampler,
    float* outRGBA, int* discard);
```

不要只改 typedef：生成的 LLVM 函数类型、间接调用类型、宿主调用、smoke 回调必须同一个提交修改。四个 float 由调用方提供，不跨 DLL 返回 C++ 对象。保留 `JydShaderProgram` 的字段布局，先检查版本再编译/调用。

`src/llvm_shader.cpp` 中替换原采样回调，新增 `texture_sampling.hpp` include：

```cpp
void sampleTexture(const void* texture, float u, float v, float* out) noexcept {
    const auto c = jyd::sampleBaseColor(
        static_cast<const jyd::Texture*>(texture), vec2(u, v));
    out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a;
}
```

`IShader::fragment`、Native 和 LLVM 的声明/定义统一改为 `LinearColor&`。包含 `color_space.hpp`。`LlvmCommonShader::fragment` 完整替换为：

```cpp
bool LlvmCommonShader::fragment(const Commonv2f& f, LinearColor& color) const {
    namespace V = shader_abi::Varying;
    std::array<float, V::Count> input{};
    for (int i = 0; i < 4; ++i) input[V::Position + i] = f.position[i];
    for (int i = 0; i < 3; ++i) {
        input[V::Normal + i] = f.normal[i];
        input[V::WorldPosition + i] = f.worldPosition[i];
    }
    for (int i = 0; i < 2; ++i) input[V::Texcoord + i] = f.texcoord[i];
    float output[4] = {0, 0, 0, 1};
    int discard = 0;
    program_.fragment(uniforms_.data(), input.data(), texture,
                      &sampleTexture, output, &discard);
    color = {output[0], output[1], output[2], output[3]};
    return discard != 0;
}
```

删除原来空纹理直接输出洋红色的提前返回，否则白色缺省策略在 Native/JIT 之间不一致。

#### 5.3 改脚本编译器的输入输出

`StageCompiler::createFunction()` 的 fragment 分支改为 `void(ptr,ptr,ptr,ptr,ptr,ptr)`；vertex 分支不动。创建 basic block 后，fragment 参数读取改成：

```cpp
texture_ = argument++;
sampler_ = argument++;
output_ = argument++;
discard_ = argument;
builder_.CreateStore(builder_.getInt32(0), discard_);
for (int i = 0; i < 4; ++i)
    storeFloat(output_, i, scalar(i == 3 ? 1.0f : 0.0f).components[0]);
```

生成函数类型使用：

```cpp
llvm::FunctionType* type = llvm::FunctionType::get(
    builder_.getVoidTy(),
    {pointerType, pointerType, pointerType, pointerType, pointerType, pointerType},
    false);
```

`sample()` 保留原来的阶段和参数检查，之后的函数体替换为：

```cpp
auto* texel = builder_.CreateAlloca(builder_.getFloatTy(), builder_.getInt32(4));
for (int i = 0; i < 4; ++i)
    storeFloat(texel, i, scalar(i == 3 ? 1.0f : 0.0f).components[0]);
auto* samplerType = llvm::FunctionType::get(builder_.getVoidTy(),
    {builder_.getPtrTy(), builder_.getFloatTy(), builder_.getFloatTy(),
     builder_.getPtrTy()}, false);
builder_.CreateCall(samplerType, sampler_,
    {texture_, arguments[0].components[0], arguments[0].components[1], texel});
return vectorFromPointer(texel, 0, 4);
```

`finishFragment()` 完整替换如下。注意没有 clamp、乘 255、整数转换或位打包。

```cpp
bool finishFragment() {
    if (!storeRequired("color", ValueKind::Vec4, 0)) return false;
    llvm::Value* flag = builder_.getInt32(0);
    const auto it = variables_.find("discard");
    if (it != variables_.end()) {
        if (it->second.kind != ValueKind::Scalar)
            return fail("fragment 'discard' output must be scalar");
        flag = builder_.CreateZExt(builder_.CreateFCmpONE(
            it->second.components[0], scalar(0.0f).components[0]),
            builder_.getInt32Ty());
    }
    builder_.CreateStore(flag, discard_);
    builder_.CreateRetVoid();
    return true;
}
```

#### 5.4 不遗漏手写 IR 和 Common 参考实现

手写 IR 不经过上面的编译器。将 `src/llvm_support.cpp` 的 `buildCommonFragmentFunction()` 完整替换为以下版本，增加 `shader_abi.hpp` 与 `<limits>` include。它仍是 Common，不是 PBR；作用是维持基准路径可比较。

```cpp
void buildCommonFragmentFunction(llvm::Module& module, llvm::IRBuilder<>& b) {
    namespace U = jyd::shader_abi::Uniform;
    namespace V = jyd::shader_abi::Varying;
    auto* p = b.getPtrTy();
    auto* type = llvm::FunctionType::get(b.getVoidTy(), {p,p,p,p,p,p}, false);
    auto* fn = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                     "jyd_shader_fragment", module);
    b.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "entry", fn));
    auto a = fn->arg_begin();
    llvm::Value* uniforms = a++;
    llvm::Value* input = a++;
    llvm::Value* texture = a++;
    llvm::Value* sampler = a++;
    llvm::Value* output = a++;
    llvm::Value* discard = a;
    auto c = [&](float x) -> llvm::Value* {
        return llvm::ConstantFP::get(b.getFloatTy(), x);
    };
    auto* texel = b.CreateAlloca(b.getFloatTy(), b.getInt32(4));
    for (int i = 0; i < 4; ++i) storeFloat(b, texel, i, c(i == 3 ? 1 : 0));
    auto* st = llvm::FunctionType::get(b.getVoidTy(),
        {p, b.getFloatTy(), b.getFloatTy(), p}, false);
    b.CreateCall(st, sampler, {texture,
        loadFloat(b, input, V::Texcoord), loadFloat(b, input, V::Texcoord + 1), texel});
    llvm::Value* n[3];
    llvm::Value* l2 = c(0);
    for (int i = 0; i < 3; ++i) {
        n[i] = loadFloat(b, input, V::Normal + i);
        l2 = b.CreateFAdd(l2, b.CreateFMul(n[i], n[i]));
    }
    auto* valid = b.CreateAnd(b.CreateFCmpOGT(l2, c(1e-16f)),
        b.CreateFCmpOLE(l2, c(std::numeric_limits<float>::max())));
    auto* length = b.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt,
        b.CreateSelect(valid, l2, c(1)));
    llvm::Value* ndotl = c(0);
    for (int i = 0; i < 3; ++i) {
        auto* ni = b.CreateSelect(valid, b.CreateFDiv(n[i], length), c(0));
        ndotl = b.CreateFAdd(ndotl, b.CreateFMul(ni,
            loadFloat(b, uniforms, U::DiffuseLight + i)));
    }
    auto* lighting = b.CreateFAdd(c(0.2f),
        b.CreateFMul(c(0.8f), clampFloat(b, ndotl, 0, 1)));
    for (int i = 0; i < 3; ++i)
        storeFloat(b, output, i, b.CreateFMul(loadFloat(b, texel, i), lighting));
    storeFloat(b, output, 3, loadFloat(b, texel, 3));
    b.CreateStore(b.getInt32(0), discard);
    b.CreateRetVoid();
}
```

Native Common 的 fragment 同步替换为以下完整方法；删除旧的“系数为 0 但仍计算”的高光代码。乘零不能消除 NaN。

```cpp
bool fragment(const Commonv2f& f, LinearColor& color) const override {
    const auto t = sampleBaseColor(texture, f.texcoord);
    const float ndotl = std::clamp(
        safeNormalize(f.normal) * DiffuseLightDirection, 0.0f, 1.0f);
    const float lighting = 0.2f + 0.8f * ndotl;
    color = {t.r * lighting, t.g * lighting, t.b * lighting, t.a};
    return false;
}
```

`shaders/common.jydshader` 的 fragment 改为：

```text
fragment {
    let texel = sample(in.texcoord);
    let n = normalize(in.normal);
    let lighting = 0.2 + 0.8 * clamp(dot(n, diffuseLightDirection), 0.0, 1.0);
    color = vec4(texel.rgb * lighting, texel.a);
}
```

Unlit 的 fragment 用 `color = sample(in.texcoord);`。二者 vertex 都采用第 2 步写出的四项输出。Common 保留 0.2 环境常数仅为历史基准；第 7 步的 PBR 不保留它。

为了让本步骤独立可构建，`drawTriangle_byShader()` 暂时把 `Color color` 换为 `LinearColor color`，通过 discard 后再转换：

```cpp
const Color display = finiteColor(color)
    ? Color{encodeLinearByte(color.r), encodeLinearByte(color.g),
            encodeLinearByte(color.b), 255}
    : Color{255, 0, 255, 255};
framebuffer_.setPixel(x, y, display);
```

此转换只是一步过渡，仍会截断 HDR；下一步必须删除它。smoke 的回调也改为 void+输出数组，比较 float，不再比较 packed uint32。

### 验证与本步结束点

检查 `srgbToLinear(0.5)` 约为 0.214041；字节 128 对应的是 `128/255`，不能拿它和精确 0.5 混为一谈。白色保持 1；alpha=128 只变成 128/255。让脚本输出 `vec4(4,2,0.5,1)`，**直接检查片元返回数组**仍是 4、2、0.5，而不是拿窗口里的饱和颜色判断。

此时 ABI=4，三个 Common 后端仍可运行；显示端暂时未支持 HDR。用 `rg` 搜索旧回调返回类型、`Color&` 片元签名和 `CreateRet(packed`，确保生产调用链上没有残留。

## 第 6 步：保存 HDR，再统一转换成屏幕颜色

### 现在的状况

`Framebuffer` 和 SDL 上传都使用 RGBA8。`drawTriangle_byShader()` 直接写这个缓冲；线框、填充、深度显示也直接写它。把所有路径一概当 HDR 处理，会让旧模式被空的 HDR 缓冲覆盖。

### 目标

光照路径单独写线性浮点缓冲；全部模型画完后只做一次曝光、tone mapping 和 sRGB 编码。线框/填充/深度路径继续直写 RGBA8，不经过 resolve。

### 如何执行

#### 6.1 把显示变换写成可测试的纯函数

完整新增文件 `include/display_transform.hpp`。第一版选简单 Reinhard，目的是建立正确链路，不宣称它是最终的电影级色彩管理。

```cpp
#pragma once
#include "color_space.hpp"
#include <stdexcept>

namespace jyd {
inline float toneMapChannel(float linear, float exposure) {
    // 前提：参数有限且 exposure >= 0。用 double 避免两个有限 float 相乘溢出。
    const double x = std::max(0.0, static_cast<double>(linear)) * exposure;
    return static_cast<float>(x / (1.0 + x));
}
inline Color toDisplay(LinearColor c, float exposure) {
    if (!std::isfinite(exposure) || exposure < 0)
        throw std::invalid_argument("exposure must be finite and non-negative");
    if (!finiteColor(c)) return {255, 0, 255, 255};
    return {encodeLinearByte(toneMapChannel(c.r, exposure)),
            encodeLinearByte(toneMapChannel(c.g, exposure)),
            encodeLinearByte(toneMapChannel(c.b, exposure)), 255};
}
}
```

曝光在这里是线性乘数，例如 2 表示加一档；如果以后 UI 使用 EV，应先换算 `exposure = exp2(EV)`。不能把 EV 本身直接乘到颜色上。非有限颜色显示洋红，不要默默变黑掩盖错误。

#### 6.2 接到 Renderer 的生命周期

`include/renderer.hpp` 增加 `color_space.hpp`、`<cstddef>`，private 增加：

```cpp
std::vector<LinearColor> hdr_;
```

public 增加 `std::size_t resolveHdr(float exposure);`。构造函数的 zbuffer 初始化后插入：

```cpp
hdr_.resize(static_cast<std::size_t>(framebuffer_.width()) * framebuffer_.height());
```

`clear()` 保留原来的 byte framebuffer 和 depth 清理，再补：

```cpp
std::fill(hdr_.begin(), hdr_.end(), LinearColor{0, 0, 0, 1});
```

本版明确采用黑色 HDR 背景；旧的 `{20,24,33}` 仍是非光照模式背景。直接把旧字节背景解码再 tone-map，会改变背景亮度，不能声称保持原样。未来若增加窗口尺寸变化，HDR、深度、Framebuffer 和相机宽高必须一起更新；当前固定窗口不新增 resize 系统。

在 `drawTriangle_byShader()` 中删除第 5 步的临时显示转换，用下面代码替换原片元调用到写像素的部分。它在通过深度测试、获得 `fragmentInput` 后执行：

```cpp
LinearColor color{};
if (shader.fragment(fragmentInput, color)) continue;
setZbuffer(x, y, zbuf);
hdr_[static_cast<std::size_t>(y) * framebuffer_.width() + x] = color;
```

必须先判断 discard，再写深度和颜色。否则被丢弃的片元会遮住后面的物体。这里沿用现有光栅循环对 x/y 的屏幕范围裁剪。

`src/renderer.cpp` include `display_transform.hpp`，新增完整成员函数：

```cpp
std::size_t Renderer::resolveHdr(float exposure) {
    if (!std::isfinite(exposure) || exposure < 0)
        throw std::invalid_argument("invalid exposure");
    std::size_t invalidPixels = 0;
    for (int y = 0; y < framebuffer_.height(); ++y) {
        for (int x = 0; x < framebuffer_.width(); ++x) {
            const auto c = hdr_[static_cast<std::size_t>(y) * framebuffer_.width() + x];
            if (!finiteColor(c)) ++invalidPixels;
            framebuffer_.setPixel(x, y, toDisplay(c, exposure));
        }
    }
    return invalidPixels;
}
```

`src/main.cpp` 中，在模型循环结束后、`window.present(framebuffer)` 之前插入：

```cpp
if (mod == jyd::Lighting) {
    const auto invalidPixels = renderer.resolveHdr(1.0f);
    if (invalidPixels != 0)
        std::cerr << "Non-finite HDR pixels: " << invalidPixels << '\n';
}
```

只允许已知合法的 RenderMod 值；原 Pipeline 的 default 也画 shader，如果保留它作为可达路径，就必须同步把它算入 resolve 条件。更清楚的做法是让未知模式直接报错。

注意：当前 `Nolighting` 枚举分支没有绘制代码。选择 `unlit.jydshader` 仍应走 **Lighting 的 shader 通道**，并照常 resolve；不能把脚本 Unlit 和这个空的枚举分支混为一谈。DepthMap 分支还存在向 Filling 落入的问题，回归时应单独记录并在修复它的改动中加 `break`，不要把模式问题误判为 PBR 公式错误。

### 验证与本步结束点

先检查 HDR 内存中仍有 4，再检查曝光 1 时 `toneMapChannel(4,1)=0.8`，曝光 0 时输出黑。只更改曝光不应改变 HDR 数据。两个遮挡模型交换顺序，应得到同样的可见表面；discard 的前景不应写深度。

切换 Wireframe/Filling/DepthMap 不应被 HDR resolve 覆盖。此时 SDL 和 Framebuffer 无需改成浮点格式：只有着色中间结果是 HDR，显示上传仍是字节。

## 第 7 步：用同一组公式实现 C++ 与脚本 PBR

### 现在的状况

Common 实际是“纹理 × 简单漫反射 + 常量环境项”，不是微表面模型。前六步完成后，世界位置、材质、数学函数和线性浮点链路才足以承载 PBR。

### 目标

实现各向同性 GGX、Schlick Fresnel、非高度相关的 Smith `G1(V)G1(L)`，配合金属度工作流与 Lambert 漫反射。先有可测试的 C++ 数值参考，再由 shader 重现相同运算。它是教学用的直接光模型，**不是逐行复刻 PBRT 的完整材质系统**。

### 如何执行

#### 7.1 先固定符号和计算顺序

N 是表面外法线，V 指向相机，L 从表面指向光，H=normalize(V+L)。四者都在世界空间。以下 c 均限制在 [0,1]：`c_v=N·V`、`c_l=N·L`、`c_h=N·H`、`c_vh=V·H`。m 为金属度，r 为感知粗糙度，C 为线性基础色，F0 是法向入射反射率。

粗糙度先限制到 [0.05,1]，再令 alpha=r²；因此公式中的 alpha² 是 r⁴，不是 r²。GGX 法线分布和单侧遮蔽项为：

\[
D=\frac{\alpha^2}{\pi[1-c_h^2+\alpha^2c_h^2]^2},\qquad
G_1(c)=\frac{2c}{c+\sqrt{\alpha^2+(1-\alpha^2)c^2}}.
\]

将 D 的分母写成 `1-c_h²+alpha²*c_h²`，比先算 `c_h²*(alpha²-1)+1` 在 c_h 接近 1 时更稳。Fresnel 与两个 BRDF 分量为：

\[
F_0=(1-m)\,0.04+mC,\quad
F=F_0+(1-F_0)(1-c_{vh})^5,
\]
\[
f_d=\frac{(1-F)(1-m)C}{\pi},\qquad
f_s=\frac{D\,G_1(c_v)G_1(c_l)\,F}{4c_vc_l}.
\]

颜色之间的乘法均为逐分量乘法。单方向光的输出为 `Lo=(fd+fs)*lightRadiance*c_l`：末尾余弦只乘一次。没有阴影时还缺可见性因子；没有环境光时不额外加 0.2。

数值上把镜面分母下限设为 0.000001，并用 `front=clamp((N·V)/0.000001,0,1)` 让背面为零。这在极窄的掠射区引入平滑近似，是为了在尚无 if 的语言中与 C++ 保持一致；不是严格的无限精度公式。若追求更准确的极限行为，可后续实现分支或使用解析化简后的稳定形式，但必须同时更改两端。

这个组合不是严格的“完整能量补偿材质”：粗糙金属会因单次微表面散射近似损失能量，漫反射/Fresnel 的组合也是常见工作流近似。关于微表面遮蔽与粗糙度的物理背景，见 [PBRT 4：Roughness Using Microfacet Theory](https://pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)。

#### 7.2 完整 C++ 数值参考

完整新增文件 `include/pbr.hpp`。参数前提：基础色、m/r、光源已经经过第 3 步校验；基础色贴图已线性化。此函数也限制有限数值的 m/r 范围，但不把 NaN 偷偷当有效材质。

```cpp
#pragma once
#include "render_math.hpp"
#include <algorithm>
#include <cmath>

namespace jyd {
struct PbrResult {
    vec3 diffuse;
    vec3 specular;
    vec3 radiance;
};
inline PbrResult evaluatePbr(vec3 base, float metallic, float roughness,
                            vec3 normal, vec3 view, vec3 light,
                            vec3 lightRadiance) {
    constexpr float pi = 3.14159265358979323846f;
    constexpr float eps = 0.000001f;
    const float m = std::clamp(metallic, 0.0f, 1.0f);
    const float r = std::clamp(roughness, 0.05f, 1.0f);
    const vec3 n = safeNormalize(normal);
    const vec3 v = safeNormalize(view);
    const vec3 l = safeNormalize(light);
    const vec3 h = safeNormalize(v + l);
    const float rawNoV = n * v;
    const float NoV = std::clamp(rawNoV, 0.0f, 1.0f);
    const float NoL = std::clamp(n * l, 0.0f, 1.0f);
    const float NoH = std::clamp(n * h, 0.0f, 1.0f);
    const float VoH = std::clamp(v * h, 0.0f, 1.0f);
    const float alpha = r * r;
    const float a2 = alpha * alpha;
    const float NoH2 = NoH * NoH;
    const float d = (1.0f - NoH2) + a2 * NoH2;
    const float D = a2 / (pi * d * d);
    const auto G1 = [a2](float c) {
        return 2.0f * c / (c + std::sqrt(a2 + (1.0f - a2) * c * c));
    };
    const float G = G1(NoV) * G1(NoL);
    const vec3 F0 = vec3(0.04f) * (1.0f - m) + base * m;
    const float q = 1.0f - VoH;
    const float q2 = q * q;
    const vec3 F = F0 + (vec3(1.0f) - F0) * (q2 * q2 * q);
    const vec3 kd = (vec3(1.0f) - F) * (1.0f - m);
    const vec3 fd = mul(kd, base) / pi;
    const vec3 fs = F * (D * G / std::max(4.0f * NoV * NoL, eps));
    const float front = std::clamp(rawNoV / eps, 0.0f, 1.0f);
    const vec3 Lo = mul(fd + fs, lightRadiance) * NoL * front;
    return {fd, fs, Lo};
}
}
```

返回 diffuse/specular 是为了分别测试，最终显示的是 radiance。背面测试应检查 radiance=0，不应误要求所有中间 BRDF 分量都为零。

在 `include/shader.hpp` 包含 `pbr.hpp`、`texture_sampling.hpp`，新增完整类；保留 NativeCommonShader：

```cpp
struct NativePbrShader final : public CommonShader {
    const char* name() const override { return "Native PBR"; }
    Commonv2f vertex(const Commona2v& a) const override {
        return {mvp * vec4(a.position, 1.0f), a.normal, a.texcoord, a.position};
    }
    bool fragment(const Commonv2f& f, LinearColor& color) const override {
        const auto t = sampleBaseColor(texture, f.texcoord);
        const vec3 base = mul(vec3(t.r, t.g, t.b), material.baseColorFactor);
        const auto result = evaluatePbr(base, material.metallic, material.roughness,
            f.normal, cameraPosition - f.worldPosition,
            light.surfaceToLightDirection, light.lightRadiance);
        color = {result.radiance[0], result.radiance[1], result.radiance[2], 1};
        return false;
    }
};
```

#### 7.3 完整脚本，而不是省略 D/G/F 的示意代码

完整新增文件 `shaders/pbr.jydshader`。这里所有颜色乘法都是 shader 的逐分量乘法；没有调用隐藏在宿主里的 PBR 函数。

```text
shader PBR {
    vertex {
        position = transform_point(mvp, in.position);
        normal = in.normal;
        texcoord = in.texcoord;
        worldPosition = in.position;
    }
    fragment {
        let base = sample(in.texcoord).rgb * baseColorFactor;
        let m = clamp(metallic, 0.0, 1.0);
        let r = clamp(roughness, 0.05, 1.0);
        let n = normalize(in.normal);
        let v = normalize(cameraPosition - in.worldPosition);
        let l = normalize(surfaceToLightDirection);
        let h = normalize(v + l);
        let rawNoV = dot(n, v);
        let NoV = clamp(rawNoV, 0.0, 1.0);
        let NoL = clamp(dot(n, l), 0.0, 1.0);
        let NoH = clamp(dot(n, h), 0.0, 1.0);
        let VoH = clamp(dot(v, h), 0.0, 1.0);
        let alpha = r * r;
        let a2 = alpha * alpha;
        let NoH2 = NoH * NoH;
        let d = (1.0 - NoH2) + a2 * NoH2;
        let D = a2 / (3.141592653589793 * d * d);
        let Gv = 2.0 * NoV / (NoV + sqrt(a2 + (1.0 - a2) * NoV * NoV));
        let Gl = 2.0 * NoL / (NoL + sqrt(a2 + (1.0 - a2) * NoL * NoL));
        let G = Gv * Gl;
        let F0 = mix(vec3(0.04, 0.04, 0.04), base, m);
        let q = 1.0 - VoH;
        let q2 = q * q;
        let F = F0 + (vec3(1.0, 1.0, 1.0) - F0) * (q2 * q2 * q);
        let kd = (vec3(1.0, 1.0, 1.0) - F) * (1.0 - m);
        let fd = kd * base / 3.141592653589793;
        let fs = F * (D * G / max(4.0 * NoV * NoL, 0.000001));
        let front = clamp(rawNoV / 0.000001, 0.0, 1.0);
        let Lo = (fd + fs) * lightRadiance * NoL * front;
        color = vec4(Lo, 1.0);
    }
}
```

先固定白贴图、白基础色、m=0、r=0.5，再分别只改变 m、r、光强、视角，别一次换所有变量。粗糙度影响高光宽度与峰值；金属度=1 消除漫反射并让基础色控制 F0。不要用“更亮”作为唯一的正确标准。

### 验证与本步结束点

令 N=V=L=(0,0,1)、白色基础色、m=0、r=0.5、白光强=1：D=16/pi，G=1，F=0.04；diffuse=0.96/pi，specular=0.16/pi，最终每通道应约为 **1.12/pi=0.356507**。这个手算点能发现 alpha 平方次数、漏除 pi、误用点积等问题。

m=1 时 diffuse 必须为零；N·L<=0 或 N·V<=0 时输出为零；零法线和 V+L=0 不能产生 NaN。最小粗糙度正面高光可远大于 1，必须在 HDR 中保留，再经过第 6 步显示。

## 第 8 步：接入入口，用测试收口

### 现在的状况

原 UI 只有 Native Common 和 LLVM shader 文件选项；`jydLlvmSmoke` 也只认识旧布局和字节返回值。前面新增类型如果没有测试目标和选择入口，就只是散落的代码，用户无法验证它们是否真正连起来了。

### 目标

Native PBR 与脚本 PBR 都可选；Common、Unlit、无 LLVM 构建仍可用。数值测试覆盖公式，JIT 对照覆盖编译器与 ABI，场景回归覆盖插值、遮挡和显示。三者不能互相替代。

### 如何执行

#### 8.1 接入选择入口

`include/model_selection_dialog.hpp` 中给 `ShaderProgram` 追加 `NativePbr = 2`，保留已有数值。在 `src/model_selection_dialog.cpp` 创建 Native Common 选项之后新增：

```cpp
shaderProgramCombo_->addItem(tr("Native C++ - PBR (reference)"),
    static_cast<int>(ShaderProgram::NativePbr));
```

`src/main.cpp` 创建 shader 的代码中，在现有 `if (!shader)` 的 Common fallback **之前**加入：

```cpp
if (shaderProgram == jyd::ShaderProgram::NativePbr)
    shader = std::make_unique<jyd::NativePbrShader>();
```

这段放在 LLVM 条件编译块外，因此 `JYD_ENABLE_LLVM=OFF` 也能选 Native PBR。不要把 Native Common 的 fallback 偷换成 PBR，否则旧基准消失。

已有 CMake 在 LLVM 构建后复制整个 `shaders/` 目录，UI 会枚举目录下的 shader。新增 PBR 文件后要触发可执行目标重新链接/其 POST_BUILD，或手动执行一次 `cmake -E copy_directory shaders build/vs2022-x64/Debug/shaders`。单纯新增 shader 不一定会触发已是最新状态的目标的 POST_BUILD。Release 的目标目录同理替换；这里不引入热更新。

第 3 步的材质赋值留在每次模型绘制之前。第一版可在模型加载后临时指定参数以做回归：

```cpp
for (std::size_t i = 0; i < loadedScene.size(); ++i) {
    loadedScene[i].material.baseColorFactor = vec3(0.8f, 0.3f, 0.1f);
    loadedScene[i].material.metallic = (i % 2 == 0) ? 0.0f : 1.0f;
    loadedScene[i].material.roughness = 0.35f;
}
```

插在 `scene = std::move(loadedScene)` 前。它是验证配置，不是已经实现了材质编辑界面。

#### 8.2 新增纯数值测试

完整新增文件 `tools/pbr_tests.cpp`。这些测试不创建窗口，不依赖 SDL/Qt 初始化。代码使用第 0 步的 `check`，Release 下不会被优化掉。

```cpp
#include "pbr.hpp"
#include "material.hpp"
#include "display_transform.hpp"
#include "test_support.hpp"
#include <iostream>
#include <limits>

int main() {
    try {
        using namespace jyd;
        check(near(srgbToLinear(0.5f), 0.21404114f), "sRGB decode");
        for (int i = 0; i < 256; ++i) {
            const float x = i / 255.0f;
            check(near(linearToSrgb(srgbToLinear(x)), x), "sRGB round trip");
        }
        check(near(decodeBaseColor({128,128,128,128}).a, 128.0f/255), "alpha");
        check(near(toneMapChannel(4, 1), 0.8f), "HDR tone mapping");
        check(toDisplay({4,2,1,1}, 0).r == 0, "zero exposure");
        check(toDisplay({std::numeric_limits<float>::quiet_NaN(),0,0,1},1).b == 255,
              "NaN diagnostic");
        const vec3 z(0,0,1), white(1);
        const auto p = evaluatePbr(white, 0, 0.5f, z, z, z, white);
        check(near(p.radiance[0], 1.12f / 3.141592653589793f), "analytic point");
        for (float r : {0.05f, 0.1f, 0.5f, 1.0f}) {
            for (float m : {0.0f, 0.5f, 1.0f}) {
                const auto front = evaluatePbr(white, m, r, z, z, z, white);
                check(finite3(front.radiance), "finite front");
                if (m == 1) check(near(front.diffuse * front.diffuse, 0), "metal diffuse");
                for (vec3 v : {vec3(1,0,0.0000001f), -z, vec3(0)}) {
                    const auto edge = evaluatePbr(white, m, r, z, v, z, white);
                    check(finite3(edge.radiance), "finite edge case");
                }
            }
        }
        const auto dark = evaluatePbr(white,0,0.5f,z,z,-z,white);
        check(near(dark.radiance * dark.radiance,0), "back light");
        const auto back = evaluatePbr(white,0,0.5f,z,-z,z,white);
        check(near(back.radiance * back.radiance,0), "back view");
        const auto zero = evaluatePbr(white,0,0.5f,vec3(0),z,z,white);
        check(near(zero.radiance * zero.radiance,0), "zero normal");
        const auto bright = evaluatePbr(white,1,0.05f,z,z,z,white);
        check(bright.radiance[0] > 1, "do not clamp HDR");
        const auto twice = evaluatePbr(white,0,0.5f,z,z,z,vec3(2));
        check(near(twice.radiance[0], 2*p.radiance[0]), "linear light intensity");
        bool rejected = false;
        try {
            auto bad = MaterialParameters{};
            bad.roughness = std::numeric_limits<float>::quiet_NaN();
            (void)sanitizeMaterial(bad);
        } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "reject non-finite material");
        std::cout << "PBR numeric tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
```

#### 8.3 用真实 shader 对照 C++ 参考

完整新增文件 `tools/pbr_jit_tests.cpp`。它直接使用真实编译入口和新 ABI，不绕过 shader 前端；测试采样回调返回已线性化的常量色，专门把纹理解码和 BRDF 对照拆开。参数组合覆盖金属度、粗糙度、正面、掠射与背面。

```cpp
#include "llvm_support.hpp"
#include "shader_abi.hpp"
#include "pbr.hpp"
#include "test_support.hpp"
#include <array>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>

namespace {
void sampleConstant(const void*, float, float, float* out) noexcept {
    out[0]=0.7f; out[1]=0.5f; out[2]=0.2f; out[3]=1;
}
struct Program {
    jyd::JydShaderProgram value{};
    Program() = default;
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    ~Program() { if (value.handle) jyd::jydDestroyShaderProgram(value.handle); }
};
}
int main() {
    try {
        using namespace jyd;
        namespace U = shader_abi::Uniform;
        namespace V = shader_abi::Varying;
        check(jydShaderAbiVersion() == shader_abi::Version, "shader ABI version");
        std::ifstream file("shaders/pbr.jydshader", std::ios::binary);
        check(file.good(), "cannot open PBR shader; run from source root");
        const std::string source((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        Program program;
        // 现有 API 成功返回非零，失败返回 0；不要套用进程退出码约定。
        check(jydCompileShaderSource(source.data(), source.size(), &program.value) != 0,
              program.value.error);
        check(program.value.vertex && program.value.fragment, "missing entry point");
        std::array<float, U::Count> uniforms{};
        std::array<float, V::Count> varying{};
        varying[V::Position + 3] = 1;
        varying[V::Normal + 2] = 1;
        const vec3 factor(0.8f, 0.6f, 0.4f);
        const vec3 intensity(3,2,1);
        for (int c = 0; c < 3; ++c) {
            uniforms[U::BaseColor+c] = factor[c];
            uniforms[U::LightRadiance+c] = intensity[c];
        }
        const vec3 base = mul(vec3(0.7f,0.5f,0.2f), factor);
        for (float r : {0.05f,0.15f,0.5f,1.0f}) {
            for (float m : {0.0f,0.5f,1.0f}) {
                for (vec3 view : {vec3(0,0,2),vec3(2,0,1),vec3(1,0,0.0000001f),
                                  vec3(0,0,-1),vec3(0)}) {
                    for (vec3 light : {vec3(0,0,1),vec3(1,1,1),vec3(0,0,-1)}) {
                        uniforms[U::Metallic]=m; uniforms[U::Roughness]=r;
                        for (int c=0;c<3;++c) {
                            uniforms[U::Camera+c]=view[c];
                            uniforms[U::LightDirection+c]=light[c];
                        }
                        float actual[4] = {-1,-1,-1,-1};
                        int discard = -1;
                        program.value.fragment(uniforms.data(), varying.data(), nullptr,
                            &sampleConstant, actual, &discard);
                        const auto ref = evaluatePbr(base,m,r,vec3(0,0,1),view,light,intensity);
                        check(discard == 0 && near(actual[3],1), "alpha/discard");
                        for (int c=0;c<3;++c)
                            check(near(actual[c],ref.radiance[c],1e-5f,2e-4f),
                                  "PBR JIT/reference mismatch");
                    }
                }
            }
        }
        std::cout << "PBR JIT comparison passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
```

这是片元对照，不代表世界位置插值已经通过。第 1 步的非对称矩阵、第 2 步的插值测试仍要保留。也不能只比较最终 RGBA8：tone mapping 和量化会遮住小误差与超范围截断。

#### 8.4 注册目标和执行命令

第 0 步已加入 `include(CTest)`。在根 `CMakeLists.txt` 的已有目标定义之后追加以下块；不要重复定义 camera 测试。

```cmake
if(BUILD_TESTING)
    add_executable(jydPbrTests tools/pbr_tests.cpp)
    target_include_directories(jydPbrTests PRIVATE include tools)
    target_compile_features(jydPbrTests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(jydPbrTests PRIVATE /utf-8)
    endif()
    add_test(NAME pbr_numeric COMMAND jydPbrTests)

    if(JYD_ENABLE_LLVM)
        add_executable(jydPbrJitTests tools/pbr_jit_tests.cpp)
        target_include_directories(jydPbrJitTests PRIVATE include tools)
        target_compile_features(jydPbrJitTests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(jydPbrJitTests PRIVATE /utf-8)
        endif()
        target_link_libraries(jydPbrJitTests PRIVATE jydShaderCompiler)
        add_test(NAME pbr_jit COMMAND jydPbrJitTests)
        set_tests_properties(pbr_jit PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME llvm_smoke COMMAND jydLlvmSmoke)
        set_tests_properties(llvm_smoke PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
```

Windows 下测试需要 `jydShaderCompiler.dll` 及其运行依赖能被找到。默认同配置输出目录沿用现有部署；如果修改了目标输出目录，必须同步复制 DLL，而不是把加载失败算作公式错误。测试本身不初始化 Qt，但项目顶层配置仍会查找 Qt/SDL，因此这里没有声称可脱离项目依赖随意配置。

现有 `tools/llvm_smoke.cpp` 也必须迁移，不能让旧的 packed-color 测试继续调用新 ABI。以下为完整替换版本，保留最初的 LLVM 42 测试，并对手写 Common 和脚本 Common 使用非对称矩阵、12 项 varying 和线性回调。给 `jydLlvmSmoke` 同样补 `target_compile_options(jydLlvmSmoke PRIVATE /utf-8)`（放在 MSVC 条件内）；测试头文件与源文件同在 tools 下，无需额外包含目录。

```cpp
#include "llvm_support.hpp"
#include "shader_abi.hpp"
#include "color_space.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
void sampleTestTexture(const void*, float, float, float* out) noexcept {
    const auto c = jyd::decodeBaseColor({10,20,30,255});
    out[0]=c.r; out[1]=c.g; out[2]=c.b; out[3]=c.a;
}
struct Program {
    jyd::JydShaderProgram value{};
    Program() = default;
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    ~Program() { if (value.handle) jyd::jydDestroyShaderProgram(value.handle); }
};
void testCommon(jyd::JydShaderProgram& shader) {
    namespace U = jyd::shader_abi::Uniform;
    namespace V = jyd::shader_abi::Varying;
    std::array<float,U::Count> uniforms{};
    const float matrix[16] = {1,2,0,3, 0,4,5,6, 7,0,8,9, 0,0,0,1};
    std::copy(matrix, matrix+16, uniforms.data()+U::Mvp);
    for (int i=0;i<4;++i) uniforms[U::View+i*4+i]=1;
    uniforms[U::DiffuseLight+2]=1;
    const float input[8]={1,2,3, 0,0,2, 0.25f,0.75f};
    std::array<float,V::Count> varying{};
    shader.vertex(uniforms.data(),input,varying.data());
    // 法线在顶点阶段保留原长度，片元阶段再归一化。
    const float expected[12]={8,29,40,1, 0,0,2, 0.25f,0.75f, 1,2,3};
    for (int i=0;i<V::Count;++i) check(near(varying[i],expected[i]),"Common vertex");
    float actual[4]={};
    int discard=-1;
    shader.fragment(uniforms.data(),varying.data(),nullptr,&sampleTestTexture,
                    actual,&discard);
    float texel[4]; sampleTestTexture(nullptr,0,0,texel);
    for (int i=0;i<4;++i) check(near(actual[i],texel[i]),"Common linear fragment");
    check(discard==0,"Common discard");
}
}
int main() {
    try {
        using namespace jyd;
        check(jydShaderAbiVersion()==shader_abi::Version,"ABI mismatch");
        LlvmSmokeResult result{};
        check(jydRunLlvmSmokeTest(&result)!=0,result.error);
        check(near(result.value,42),"LLVM 42");
        Program builtin;
        check(jydCompileBuiltinCommonShader(&builtin.value)!=0,builtin.value.error);
        testCommon(builtin.value);
        std::ifstream file("shaders/common.jydshader",std::ios::binary);
        check(file.good(),"cannot open Common shader");
        const std::string source((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        Program scripted;
        check(jydCompileShaderSource(source.data(),source.size(),&scripted.value)!=0,
              scripted.value.error);
        testCommon(scripted.value);
        std::cout << "LLVM, builtin Common, scripted Common passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
```

为错误编译、非法 swizzle、零 normalize 另外补最小脚本 case；失败编译的 handle 如非空也应销毁。用于验证 `discard=1` 的脚本仍须产生合法的 color 输出。UTF-8 编译选项不能只加给 jydRenderer：测试也会包含带中文注释的头文件，系统默认代码页可能把它们解析错。

```powershell
cmake --preset vs2022-x64 -DJYD_ENABLE_LLVM=ON -DBUILD_TESTING=ON
cmake --build --preset vs2022-x64-debug
ctest --test-dir build/vs2022-x64 -C Debug --output-on-failure
cmake --build --preset vs2022-x64-release
ctest --test-dir build/vs2022-x64 -C Release --output-on-failure
```

再用单独目录测试 Native-only，避免改变原构建目录的配置：

```powershell
cmake -S . -B build/native-pbr -G "Visual Studio 17 2022" -A x64 -DJYD_ENABLE_LLVM=OFF -DBUILD_TESTING=ON
cmake --build build/native-pbr --config Release
ctest --test-dir build/native-pbr -C Release --output-on-failure
```

每次失败先保留具体编译/测试诊断，修好后再进入下一阶段。不要只看到窗口能打开就跳过 Release 或 Native-only。

### 验证与本步结束点

以下检查全部完成，第一版才算交付：

- 数值层：投影、sRGB、解析 PBR 点、金属漫反射为零、零向量、背面、掠射、HDR、非法材质测试通过。
- 编译器层：成员访问正反例通过；真实 PBR 脚本与 C++ 在线性域对照；ABI 不匹配明确失败；内置 Common 和脚本 Common 均通过。
- 场景层：非单位 clip-w 的三角形插值正确；三角形跨近裁剪面没有纹理/法线跳变；两模型材质不串；交换绘制顺序不改遮挡；discard 不写深度。
- 显示层：调曝光不改 HDR；线框、填充、深度不被 resolve 覆盖；Unlit 脚本仍通过 shader 通道显示；无异常洋红像素。
- 构建层：Debug、Release、LLVM OFF 均通过；PBR 和 Common 入口都可选；启动时加载到的是本次构建的 DLL 和 shader 文件。

若画面太暗，按“法线和 L 的符号 → sRGB 是否重复解码 → 光强 → 曝光”排查；不要先加环境常量。若 Native 正确而 JIT 错误，先检查布局偏移与成员读取，再检查公式。若高光随相机奇怪地粘在屏幕上，检查 V 是否误用了 clipPosition，或者法线是否仍被 view 矩阵转换。

## 后续扩展：先保持接口稳定，再增加效果

上述步骤完成后，可分别开小任务增加双线性/mipmap、纹理角色区分、模型矩阵及逆转置法线、阴影、IBL、法线贴图和更完整的材质。它们不是本次“完成 PBR”验收的隐藏条件。

PBRT 对这里最有用的启发是清晰的数据与物理约定：哪些量是辐亮度，哪些是反射率，方向属于哪个空间，采样和评价各自返回什么。光谱采样、路径积分器和 GPU 波前执行则属于不同层次，不能因为读到这些章节，就把目前的 CPU 光栅器一次改成另一个项目。

本文的函数与脚本是面向当前仓库编写的实施参考；后续落地时仍须跑完整构建与场景回归。**目前第 0–1 步已进入项目源码，第 2–8 步尚未实施；未执行的验收项不视为通过。**

## 本次文档校验范围

2026-10-10：将本文数学、材质、颜色转换、显示转换、PBR 头文件及 `pbr_tests.cpp` 提取到隔离的临时目录，以仓库现有 `vec.hpp` 为基础，使用 MSVC 分别按 Debug 和 Release（含 `NDEBUG`）设置编译并运行，数值测试均通过。

最初校验手册时，相机测试在旧相机头文件上如预期失败于 `near clip w`，在临时副本中应用矩阵替换后通过。随后第 0 步已正式落地到项目源码，并通过 Debug/Release 的构建、相机 CTest 及已有 LLVM smoke。相机头与新测试中的注释编码仍在 UTF-8 编译时产生警告，编码清理未纳入此次提交。

2026-10-12：第 1 步已完成并通过 Debug/Release 构建、LLVM smoke 和相机回归。新增测试覆盖内置/脚本 Common 的 MVP、View、法线、UV 和脚本 uniform 字段读取；ABI 版本检查已接入。第 1 步只集中原有布局，不改变颜色接口或字段总数。

尚未实施或运行的是：后续世界位置与浮点颜色 ABI 扩展、编译器新语法、完整 PBR JIT 对照、Renderer HDR 接线、UI 和场景回归。临时数值测试与第 1 步测试通过，不等于这些后续集成步骤已经完成。
