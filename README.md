# jydRenderer

类似 [tinyrenderer](https://github.com/ssloy/tinyrenderer) 的软件光栅化渲染器，跨平台支持 **Windows x64** 与 **Linux x64**。

## 项目结构

```
jydRenderer/
├── CMakeLists.txt
├── CMakePresets.json
├── CMakeSettings.json
├── third_party/
│   └── SDL2/             # 固定版本 SDL2 本地源码
├── include/
│   ├── framebuffer.hpp   # RGBA 像素缓冲
│   ├── renderer.hpp      # 基础 2D 绘制（线、三角形）
│   ├── window.hpp        # SDL 窗口与 framebuffer 显示
│   └── model_selection_dialog.hpp # Qt 模型选择窗口
└── src/
    ├── main.cpp
    ├── framebuffer.cpp
    ├── renderer.cpp
    ├── window.cpp
    └── model_selection_dialog.cpp
```

## 依赖

后续开发路线：[光栅化 PBR 逐步实施手册](docs/raster-pbr-implementation.md)。

- CMake 3.21+（VS2022 自带，需勾选「使用 C++ 的桌面开发」）
- Qt 5.15+ 或 Qt 6，需包含 Widgets 组件
- SDL2 2.30.10（源码已放在 `third_party/SDL2`，配置时不需要联网）
- Windows x64/MSVC：LLVM 23.1.2 已随仓库提供；其他平台需要自行安装 LLVM 开发包
- Ninja（可选，命令行构建用；VS2022 preset 不需要）

## LLVM Shader JIT

LLVM 支持默认开启。Windows x64 + MSVC 直接使用仓库内
`third_party/llvm/windows-x64`，包含 LLVM 头文件、当前 JIT 所需静态库及
zlib/zstd 运行库。无需下载 LLVM、设置 `LLVM_DIR` 或安装 vcpkg。
这些是普通 Git 文件，不需要 Git LFS；完整 clone/copy 仓库即可获得。
第三方文件约 246 MiB，来源、许可与升级约定见 `third_party/llvm/README.md`。

仍需安装 VS2022 C++ 桌面工具（含 Windows SDK、DIA SDK）及 Qt。
不再读取旧的 `cmake/LocalDependencies.cmake`；
此前创建的本机 `local-llvm` preset 不再需要，请使用仓库内标准 preset。

在项目根目录构建并验证：

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
./build/vs2022-x64/Debug/jydLlvmSmoke.exe
```

Linux 等平台仍通过 `find_package(LLVM CONFIG)` 查找外部开发包，
可用 `LLVM_DIR` 或 `JYD_LLVM_ROOT` 指定位置。

成功时应输出：



```text
JIT result: 42
Common vertex/fragment shader: OK
Shader language vertex/fragment: OK
```

启用 LLVM 后，Renderer 使用 `LlvmCommonShader`。它在启动时通过 ORC JIT
生成顶点和片元函数，之后渲染循环直接调用生成的函数指针；矩阵和光照
uniform 通过 `prepare()` 每个模型同步一次。关闭 LLVM 时则使用行为相同的
`NativeCommonShader` 回退实现。

可编程 shader 使用 `shaders/*.jydshader`。启动时编译器会解析 vertex 和
fragment 段、进行基础类型检查、生成 LLVM IR，再交给 ORC JIT。语言支持
局部变量、标量/向量算术、矩阵变换、纹理采样、`normalize`、`dot` 和
`clamp`；完整语法见 `shaders/README.md`。

模型选择窗口现在会扫描输出目录中的 `.jydshader`，也可以通过
“Browse shader...”直接加载任意脚本。仓库提供 Common 光照和 Unlit 两份
示例，并保留 Native C++ 参考实现方便对比。推荐使用
`vs2022-x64` 或 `vs2022-x64-llvm` preset，两者默认都开启 LLVM。

如果只需要不依赖 LLVM 的 Native Renderer，可以显式关闭：

```powershell
cmake -S . -B build/native -DJYD_ENABLE_LLVM=OFF
```

## Visual Studio 2022（推荐）

### 1. 前置准备

1. 安装 VS2022，工作负载勾选 **「使用 C++ 的桌面开发」**
2. 安装 Qt，并确保 `qmake`/`qmake6` 位于 `PATH`；也可以通过 `CMAKE_PREFIX_PATH` 指定 Qt 安装目录
3. **不需要 vcpkg，不需要设置 VCPKG_ROOT**
4. VS 菜单 **工具 → 选项 → CMake → 常规**，建议 **取消勾选「vcpkg 清单模式」**（避免 VS 注入内置 vcpkg 报错）

### 2. 打开项目

1. **文件 → 打开 → 文件夹**，选择 `jydRenderer` 目录
2. 顶部 CMake 配置选择 **`Visual Studio 2022 x64`**（默认开启 LLVM）；Native 版本需显式设置 `JYD_ENABLE_LLVM=OFF`
3. **项目 → 删除缓存并重新配置**
4. 等待 CMake 配置完成；SDL2 使用仓库内本地源码，不需要访问 GitHub


### 3. 编译与运行

- **生成 → 全部生成**（或 `Ctrl+Shift+B`）
- 将启动项设为 **`jydRenderer.exe`**，按 **F5** 调试运行
- 程序启动后先选择一个 `.obj` 模型；确认并加载成功后才会创建 SDL 渲染窗口

Windows 下每次生成 `jydRenderer` 后，CMake 会自动把 Qt DLL、MSVC
运行库和 `platforms/qwindows.dll` 部署到 Debug/Release 输出目录，因此可以
直接在 Visual Studio 中按 F5 运行，不需要先执行打包脚本。

生成目录：`build\vs2022-x64\Debug\jydRenderer.exe`

## Windows 打包

在 PowerShell 中运行：

```powershell
.\scripts\package-windows.ps1
```

脚本默认使用 `vs2022-x64-release` 编译 Release 版本，调用 Qt 的
`windeployqt` 收集运行库和 Windows platform plugin，并生成：

```text
dist\jydRenderer-windows-x64\
dist\jydRenderer-windows-x64.zip
```

如果之前下载 SDL2 时被中断，脚本会识别并自动修复残缺的
`FetchContent` 缓存，然后重新下载 SDL2。

如果 Qt 没有加入 `PATH`，可以显式传入安装目录：

```powershell
.\scripts\package-windows.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64
```

调试现有构建产物时也可以使用：

```powershell
.\scripts\package-windows.ps1 -Configuration Debug -SkipBuild
```

