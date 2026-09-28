# 光栅化 PBR：逐步实施手册

基线：`eb0e0ba`。本文是后续实施计划，不表示下面的新接口已经实现。
目标读者：已经能修改本项目 C++，正在学习 LLVM 和 shader 编程的开发者。

## 目标与边界

第一版实现：CPU 光栅化、单方向光、基础色贴图、每模型独立的金属度和粗糙度、线性 HDR 光照、统一显示转换。

暂不实现：阴影、IBL、法线贴图、透明、折射、多光源、路径追踪、shader 热更新、通用材质编辑器。
它是直接光照 PBR，不是 PBRT 渲染器的复刻。没有环境光和间接光时，背光面偏黑是正常现象。

实施原则：一次完成一个步骤；通过本步验收后再提交；不要一次性重写整个渲染器。
本文中的文件路径均相对仓库根目录；标注“新增”的文件、标注“目标”的 API/语法均待实现。

## 执行清单

- [ ] 0. 保存基线，确认旧版本测试通过
- [ ] 1. 集中定义 C++ / JIT 数据布局
- [ ] 2. 打通世界空间位置、法线与插值
- [ ] 3. 加入每模型材质和方向光参数
- [ ] 4. 补齐 PBR 所需的最小语言能力
- [ ] 5. 将片元输出和采样接口迁移为线性浮点
- [ ] 6. 加入 HDR 缓冲与统一显示转换
- [ ] 7. 实现 C++ PBR 参考函数，再编写 PBR shader
- [ ] 8. 完成回归测试与界面接入检查

## 0. 保存并验证基线

从仓库根目录，在能找到 CMake 的终端执行：

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
./build/vs2022-x64/Debug/jydLlvmSmoke.exe
cmake --build --preset vs2022-x64-release
./build/vs2022-x64/Release/jydLlvmSmoke.exe
```

预期：`JIT result: 42`、内置 Common 与脚本 vertex/fragment 检查均通过。
若终端找不到 cmake，使用 VS Developer PowerShell，或 VS 自带 cmake.exe 的完整路径。

手动保存同一模型、同一相机的 Native、LLVM Common、脚本 Common 截图。
记录模型与贴图路径，但不要提交个人绝对路径；截图放本地测试输出目录。

**完成标准：** 清楚区分已有问题与后续引入的问题；Debug、Release 都可以启动并执行测试。

## 1. 集中定义 JIT 数据布局

### 为什么先做

现在双方通过 float 数组传递数据，约定散落在多处：

| 数据 | 当前布局（单位：float） |
| --- | --- |
| 顶点输入 | position 0..2、normal 3..5、UV 6..7，总计 8 |
| varying | clip position 0..3、normal 4..6、UV 7..8，总计 9 |
| uniforms | mvp 0..15、vp 16..31、camera 32..34、specular light 35..37、diffuse light 38..40、ambient 41..43，总计 44 |

新增属性时，如果一端改成 12 个 float、另一端仍按 9 个读取，LLVM 不一定能检查出越界。

### 操作

1. 新增 `include/shader_abi.hpp`，定义各字段 offset、数组总长度和 ABI 版本常量。
2. 先只替换现有魔法数字，不改变布局和计算行为。
3. 矩阵约定明确为 row-major 打包、矩阵乘列向量，与当前 `writeMatrix()` 和 IR 读取方式一致。
4. 在程序句柄/桥接接口中加入版本检查的设计；同一个版本的 DLL、宿主和生成函数必须成套使用。
5. 保留 float 数组或简单 C/POD 边界，不跨 DLL 传递 STL 容器、所有权或异常。

### 改动位置

- `include/llvm_shader.hpp`：uniform 数组长度。
- `src/llvm_shader.cpp`：`prepare()`、`vertex()`、`fragment()` 的打包/解包。
- `src/shader_language.cpp`：`createBuiltins()`、`finishVertex()`、输出读写。
- `src/llvm_support.cpp`：手写 LLVM Common 的 IR 构造，不可漏改。
- `include/llvm_support.hpp`：桥接接口/版本信息。
- `tools/llvm_smoke.cpp`：测试输入输出数组。

**验证：** 原 smoke 测试不变且通过；额外使用非对称矩阵测试行列约定，不能只测单位矩阵。

**完成标准：** 所有布局长度与偏移有共同来源。建议提交：`refactor(shader): centralize JIT ABI layout`。

## 2. 打通世界空间数据

### 第一版约定

暂时把模型加载后的顶点坐标视作世界坐标，即 model 矩阵为单位矩阵。
这样不必把“模型变换系统”也加入本次范围。

- `position` 仍表示裁剪空间 vec4，只用于裁剪、投影和深度。
- `worldPosition` 是新增 vec3，供光照计算。
- 现有 `normal` 统一改为世界空间法线，文档明确其语义，不再乘视图矩阵。
- `texcoord` 不变。
- 当前 `vp` 实际保存 view 矩阵，不要误当作 view-projection。

### 操作

1. 给 `Commonv2f` 追加 `worldPosition`；JIT varying 建议追加在 offset 9，总长度改为 12，旧字段位置不变。
2. 修改 Native 顶点函数、LLVM 内置 Common、脚本 Common/Unlit，全部输出世界位置与世界法线。
3. 编译器提供 fragment 输入 `in.worldPosition`，并检查 vertex 必须写出 `worldPosition`。
4. 修改所有手写 IR、C++ 数组和测试，ABI 版本同步升级。
5. 明确旧脚本迁移策略：本阶段要求补齐新输出，缺失时报告错误；不要悄悄填零。

目标 vertex（本步骤完成后才可用）：

```text
vertex {
    position = transform_point(mvp, in.position);
    worldPosition = in.position;
    normal = normalize(in.normal);
    texcoord = in.texcoord;
}
```

### 裁剪与插值

修改 `include/shader.hpp` 中的 `VaryingTraits`，由 `src/renderer.cpp` 继续统一调用。

- 裁剪相交点：所有属性用同一个相交参数 t，做 `a + t * (b - a)`。
- 此时不要提前除 w，不要把世界位置拿去决定裁剪平面距离。
- 法线在边上按普通属性插值，片元使用前再归一化，不在每个裁剪交点反复归一化。
- 光栅化：世界位置、UV、法线都使用透视校正权重。

```text
q0 = barycentric0 / clipW0
q1 = barycentric1 / clipW1
q2 = barycentric2 / clipW2
attribute = (q0*a0 + q1*a1 + q2*a2) / (q0+q1+q2)
```

深度继续使用现有光栅深度约定；不要把深度当普通属性再做一遍上述处理。
保留 w、分母退化检查和三角形退化处理。

### 验证

- 用不同 clip w 的三角形核对已知重心坐标下的 worldPosition/UV；仅测 w=1 不够。
- 三角形穿越近裁剪面时，位置、UV 与法线可视化连续，没有接缝。
- 转动相机时，世界法线值不随相机旋转而变化。
- 将旧光照方向也统一到世界空间，禁止一部分方向仍是观察空间。

**完成标准：** 世界位置和法线从顶点到片元正确传递。未来引入模型变换时，再增加 model 和逆转置 normalMatrix。

## 3. 加入材质和光源 uniforms

### 最小数据模型

新增 `include/material.hpp`，定义普通 C++ 材质参数；不要让它依赖 LLVM。

```text
MaterialParameters
    baseColorFactor: vec3，线性颜色，默认 (1,1,1)
    metallic: float，默认 0
    roughness: float，默认 0.5

DirectionalLight
    surfaceToLightDirection: vec3，世界空间，非零
    lightRadiance: vec3，线性 RGB，非负
```

第一版为不透明材质，alpha 固定 1。
方向光的 RGB 是可调强度系数：可理解为沿光方向的入射量权重，不将它宣称为有完整物理单位标定的光源。

### 操作

1. 在 `src/main.cpp` 的每个 `RenderItem` 上存材质参数，而不是只有一个全局材质。
2. 每次绘制模型前，将该模型材质写入 shader；在 `prepare()` 中上传 uniforms。
3. 相机与方向光可共用，但每次绘制都要保证 uniforms 与当前模型一致。
4. 将新 uniform 追加到共享 ABI 中；保留旧字段直到迁移所有 Common 实现。
5. 编译器 `createBuiltins()` 增加对应变量；脚本中的 metallic 等名字来自宿主输入，不是语言自动提供。
6. 入口校验非有限值，metallic 限制到 [0,1]；有效 roughness 暂限制到 [0.05,1]。
7. 不允许零光源方向。粗糙度下限是第一版数值稳定策略，不是物理定律。

**验证：** 连续绘制两个材质不同的模型，再交换绘制顺序；各自效果不变，避免参数串到下一个模型。

**完成标准：** 无需重新编译脚本即可通过 uniforms 改变材质。暂不需要增加 Qt 参数控件。

## 4. 补齐最小语言能力

主要文件：`src/shader_language.cpp`、`shaders/README.md`、`tools/llvm_smoke.cpp`。

按下面顺序实施，每完成一个能力就增加正向和错误输入测试：

### 4.1 内置数学函数

- `max(a,b)`：同型标量/向量，并支持标量广播。
- `mix(a,b,t)`：a/b 同型，t 为标量，生成 `a*(1-t)+b*t`。
- `sqrt(x)`：标量/向量逐分量，调用 LLVM 对应 intrinsic；本版约定 sqrt(max(x,0))。
- `pow` 暂缓：Schlick 的五次方用连乘，颜色空间转换放 C++ 采样与显示端。
- `normalize` 明确退化行为：长度平方不足阈值时返回零向量，避免 0/0。

这些调用的语法已存在，主要改 `emitCall()` 的分发、参数检查和 IR 生成；不需要重编 LLVM。
现有 `CodeValue.components` 保存多个标量 IR 值，不必为了 PBR 改成 LLVM 原生向量类型。

### 4.2 只读分量访问

第一版支持 `.x/.y/.z/.w` 和 `.r/.g/.b/.a` 的合法读取组合，例如 `.rgb`；暂不支持 swizzle 写入。

当前 Lexer 将 `in.normal` 整体识别为名字，并没有真正的成员访问。
建议加入独立 Dot token 和 postfix member AST：先识别预定义的 `in.normal`，再对其结果执行分量提取。
数字中的小数点与成员访问点必须区分，确保 `0.5` 和 `texel.rgb` 都正确。

错误测试：vec2.z、长度超限、混用两套命名规则、标量.rgb、未知成员。

### 4.3 构造器

保留四标量 `vec4(r,g,b,a)`，新增 `vec4(vec3,scalar)`；不要默默截断或补齐其他错误组合。

**验证示例：** mix 的 t=0/1/0.5；max 的负数和向量广播；sqrt(4)=2；(1,2,3,4).rgb 得到 (1,2,3)。
先检查 shader 编译成功，再调用 JIT 比较数值，并验证错误脚本返回清晰错误而不是崩溃。

**完成标准：** 足够写直接光照 PBR；无需 if、循环、用户函数或数组。

## 5. 迁移为线性浮点 shader 接口

### 5.1 片元输出

当前 `JydShaderFragmentFunction` 返回打包的 uint32，编译器 `finishFragment()` 会 clamp 到 [0,1]。
目标是输出 float RGBA，保留 >1 的颜色。建议的新签名（尚未实现）：

```cpp
using JydShaderFragmentFunction = void (*)(
    const float* uniforms, const float* input,
    const void* texture, JydTextureSampleFunction sampler,
    float* outputRgba, int* discard);
```

同步修改：`include/llvm_support.hpp`、`src/llvm_support.cpp` 的内置 IR、
`StageCompiler::createFunction()/finishFragment()`、`LlvmCommonShader::fragment()`、smoke 测试。
输出颜色与丢弃标记在每条正常返回路径都要初始化；丢弃后不得写颜色或深度。

新增 `LinearColor`（4 个 float）；将 `IShader::fragment()` 与 Native 实现也迁移到它。
更新 ABI 版本，重新构建所有目标；禁止旧 EXE 配新 DLL。

### 5.2 基础色采样

目标采样回调改为输出 float RGBA；保留 texture 的内部 8 位存储也可以。
第一版 `sample(uv)` 明确表示“采样基础色，返回线性 RGB 与未经 gamma 处理的 alpha”。

从贴图读取字节并归一化到 [0,1] 后，对 RGB 分量 c 做：

```text
c <= 0.04045 : linear = c / 12.92
否则         : linear = ((c + 0.055) / 1.055)^2.4
```

将解码放在 `src/llvm_shader.cpp` 的采样回调或共享颜色辅助函数中，Native 使用相同逻辑。
不得把解码后的颜色重新打包成 8 位再传给 shader。
当前 nearest 采样可以保留；未来做双线性过滤时，应在线性空间混合颜色。
缺少基础色贴图时建议使用白色，仍允许纯参数材质；去掉当前无贴图就提前返回紫色的逻辑。

### 5.3 旧示例迁移

Common/Unlit 现在输出线性颜色；视觉结果可能与旧 gamma 空间计算不同，不强求截图逐像素一致。
旧 smoke 中 `0xff1e140a` 等 packed color 断言必须改为浮点值和容差。
可以暂时在 renderer 用明确的 LDR 适配器显示，以保持本提交可编译；下一步必须替换它。

**验证：** sRGB 0.5 解码约为 0.214041；alpha 0.5 保持 0.5；JIT 输出 (4,2,1,1) 时宿主读取仍是 (4,2,1,1)。

## 6. 加入 HDR 缓冲与统一显示转换

涉及：`include/framebuffer.hpp`、`src/framebuffer.cpp`、`include/renderer.hpp`、`src/renderer.cpp`、`src/main.cpp`。

### 操作

1. 保留现有 RGBA8 Framebuffer 作为 SDL 显示缓冲；新增 float RGBA 的 HDR 缓冲，可由 Renderer 持有。
2. shader 路径在深度测试、discard 判断之后写 HDR，不直接写显示缓冲。
3. 所有模型完成后，只执行一次 HDR resolve，再调用 `window.present(framebuffer)`。
4. 第一版用简单 Reinhard 显示算子，先求 `x=max(linear*exposure,0)`，再求 `mapped=x/(1+x)`。
5. 对 mapped 做线性到 sRGB 编码，最后量化为 RGBA8。
6. exposure 默认 1；它只参与显示，不参与 BRDF。
7. resolve 时处理 NaN/Inf，并在调试模式记录异常数量；不要靠显示端掩盖 BRDF 数值错误。

线性到 sRGB：

```text
x <= 0.0031308 : srgb = 12.92*x
否则          : srgb = 1.055*x^(1/2.4)-0.055
```

### 保留已有显示模式

线框、深度可视化等原有模式仍可直接写 RGBA8，但只有 HDR 着色模式才运行 resolve。
否则 HDR 空缓冲会覆盖线框/深度结果。背景色也要明确属于线性输入还是显示值，避免重复编码。
窗口尺寸变化时，同时处理 HDR、显示缓冲与深度缓冲的尺寸。

**验证：** HDR 4/2/1 不会在存储时都变成 1；改变曝光无需重编 shader；多模型只 resolve 一次；线框和深度模式不回归。

## 7. 实现 PBR 参考与 shader

### 7.1 先固定计算约定

新增 `include/pbr.hpp` 或等价的 C++ 参考函数，先进行数值测试，再翻译成脚本。
这不是把 PBR 永久藏在宿主里，而是为 JIT 实现提供独立的对照答案。

本版选各向同性 GGX、可分离 Smith G1 乘积、Schlick Fresnel、金属度工作流。
这是本项目第一版选型，不声称与 PBRT 所有材质或粗糙度映射完全一致。

```text
r = clamp(roughness, 0.05, 1)
alpha = r*r
a2 = alpha*alpha
N = safeNormalize(worldNormal)
V = safeNormalize(cameraPosition - worldPosition)
L = safeNormalize(surfaceToLightDirection)
H = safeNormalize(V + L)
NoL = clamp(dot(N,L), 0, 1)
NoV = clamp(dot(N,V), 0, 1)
NoH = clamp(dot(N,H), 0, 1)
VoH = clamp(dot(V,H), 0, 1)

F0 = mix(vec3(0.04,0.04,0.04), baseColor, metallic)
F = F0 + (1-F0)*(1-VoH)^5
d = NoH*NoH*(a2-1)+1
D = a2 / (pi*d*d)
G1(x) = 2*x / max(x + sqrt(a2+(1-a2)*x*x), epsilon)
G = G1(NoL)*G1(NoV)
specular = D*G*F / max(4*NoL*NoV, epsilon)
kd = (1-F)*(1-metallic)
diffuse = kd*baseColor/pi
Lo = (diffuse+specular)*lightRadiance*NoL
```

建议 epsilon=1e-6 用于以上除法保护，不对所有公式盲目套相同截断。
实现 D 时可使用等价的 `d=(1-NoH*NoH)+NoH*NoH*a2` 降低消减误差。
0.04 是常见介电材质 F0 近似，并非所有材质的固定物理值。

法线背向观察者或光源时，单面 BRDF 返回零；退化 N/V/L 也返回零。
shader 暂无 if，可在宿主剔除退化输入，并用 dot(N,V)>0 的数值门控：
`front = clamp(dot(N,V)/epsilon, 0, 1)`，最后乘 front。
这是 epsilon 附近的平滑数值近似；C++ 对照必须采用相同约定，严格分支版本可等布尔/if 特性加入后再替换。
不要添加旧示例固定 0.2 的环境补光，不重复乘一次 NoL。

### 7.2 编写目标脚本

新增 `shaders/pbr.jydshader`。下面只是第 2—6 步完成后的目标片段，不是当前版本可直接运行的完整 shader：

```text
fragment {
    let baseColor = sample(in.texcoord).rgb * baseColorFactor;
    let N = normalize(in.normal);
    let V = normalize(cameraPosition - in.worldPosition);
    let L = normalize(surfaceToLightDirection);
    let H = normalize(V + L);
    // 在这里逐行展开上面的 D、G、F、diffuse 和 specular。
    // 五次方用连乘；G1 对 NoL/NoV 分别展开，不要求用户函数。
    // 最后：color = vec4(Lo, 1.0);
}
```

利用现有 Qt shader 选择机制加载，无需为 PBR 单独硬编码一套选择逻辑。
构建后确认脚本复制到可执行文件旁的 shaders 目录。

**验证：** 固定输入下比较 C++ 与 JIT 线性 HDR 输出，而不是比较 tone mapping 后的字节颜色。
使用绝对与相对容差，例如 `abs(a-b) <= 1e-5 + 1e-4*abs(reference)`，同时要求两者均有限。

## 8. 回归验收与提交

新增专用测试源（例如 `tools/pbr_tests.cpp`），在 CMake 注册可执行目标及 CTest。
目前项目没有这个目标；添加后才能运行下面的命令：

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
ctest --test-dir build/vs2022-x64 -C Debug --output-on-failure
```

原 smoke 依赖根目录下的 shaders，注册 CTest 时设置 WORKING_DIRECTORY 为源码根目录。
Release 重复相同流程，并继续直接执行 jydLlvmSmoke。

| 验收项 | 输入/操作 | 预期 |
| --- | --- | --- |
| ABI | 非对称矩阵、非零全部字段 | 各字段无错位 |
| 插值 | 不同 w、穿越近裁剪面 | UV/世界位置连续 |
| 颜色 | sRGB 0.5、alpha 0.5 | RGB≈0.214041，alpha不变 |
| HDR | fragment 输出大于1 | resolve前不截断 |
| 粗糙度 | 0.05、0.2、0.5、1 | 高光形状变化，结果有限 |
| 金属度 | 0、0.5、1 | metallic=1时漫反射项为0 |
| 相机 | 固定光源，绕物体旋转 | 高光响应观察方向 |
| 光照背面 | NoL<=0 | 直接光照为0 |
| 数值边界 | V≈-L、掠射角、零长度输入 | 无NaN/Inf，无异常崩溃 |
| 材质隔离 | 两模型交换绘制顺序 | 参数不串用 |
| 语言错误 | 错误swizzle/类型/缺少输出 | 明确报错 |
| 旧功能 | Common、Unlit、线框、深度 | 保持可用，颜色语义变更有文档 |

视觉测试先使用光滑法线的球体或曲面；平面无法充分呈现高光宽度。
固定相机、光源和曝光，在多次渲染中只改变一个参数，不自动曝光。
不要用“看起来更亮”代替数值测试，也不宣称这组测试已经证明完整能量守恒。

建议每一步一个独立提交。新增测试、脚本、ABI 文档要一起提交；本机缓存和测试截图不默认上传。
同时验证 `JYD_ENABLE_LLVM=OFF` 的 Native 构建，避免新材质数据意外依赖 LLVM。

## 第一版完成后再做什么

1. Qt 为每个模型增加 metallic/roughness 控件，变化后触发重绘而非重新编译。
2. 增加粗糙度/金属度贴图与资源槽，按线性数据读取，不做 sRGB 解码。
3. 增加切线、TBN 与法线贴图，明确切线空间与法线贴图约定。
4. 增加阴影，先验证遮挡再调材质。
5. 增加环境贴图和 IBL，再考虑更完整的多重散射补偿。
6. 语言扩展用户函数、布尔与 if，之后再考虑循环；不要为单光源 PBR 提前实现整个通用语言。

## 学习材料

- [PBRT：微表面理论](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)：理解 D、遮蔽和掩蔽。注意书中实现选择与本文第一版近似并非全部相同，尤其不能混用 roughness 到 alpha 的映射。
- [LLVM：语言前端教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/)：对照项目阅读词法、语法树、IR 生成和 JIT。
- [LLVM：控制流](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl05.html)：留到增加 if/循环时阅读，先理解基本块、分支和 phi。

**现在从第 0 步开始。第一项实际代码改动应是第 1 步的数据布局集中化，不是直接写 PBR 公式。**
