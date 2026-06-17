# jydRenderer

类似 [tinyrenderer](https://github.com/ssloy/tinyrenderer) 的软件光栅化渲染器，跨平台支持 **Windows x64** 与 **Linux x64**。

## 项目结构

```
jydRenderer/
├── CMakeLists.txt
├── CMakePresets.json
├── CMakeSettings.json
├── include/
│   ├── framebuffer.hpp   # RGBA 像素缓冲
│   ├── renderer.hpp      # 基础 2D 绘制（线、三角形）
│   └── window.hpp        # 窗口与 framebuffer 显示
└── src/
    ├── main.cpp
    ├── framebuffer.cpp
    ├── renderer.cpp
    └── window.cpp
```

## 依赖

- CMake 3.20+（VS2022 自带，需勾选「使用 C++ 的桌面开发」）
- SDL2（首次配置时 CMake 自动从 GitHub 下载，**无需 vcpkg**）
- Ninja（可选，命令行构建用；VS2022 preset 不需要）

## Visual Studio 2022（推荐）

### 1. 前置准备

1. 安装 VS2022，工作负载勾选 **「使用 C++ 的桌面开发」**
2. **不需要 vcpkg，不需要设置 VCPKG_ROOT**
3. VS 菜单 **工具 → 选项 → CMake → 常规**，建议 **取消勾选「vcpkg 清单模式」**（避免 VS 注入内置 vcpkg 报错）

### 2. 打开项目

1. **文件 → 打开 → 文件夹**，选择 `jydRenderer` 目录
2. 顶部 CMake 配置下拉框选 **`Visual Studio 2022 x64`**（不要选 Ninja preset，除非在「x64 Native Tools 命令提示符」里用命令行）
3. **项目 → 删除缓存并重新配置**
4. 等待 CMake 配置完成（无 vcpkg 时会自动从 GitHub 下载 SDL2，首次需联网）

> **不必安装 vcpkg 也能跑。** 若已安装独立 vcpkg，设置 `VCPKG_ROOT=C:\vcpkg` 后会优先用它；请勿把 `VCPKG_ROOT` 指到 VS 内置路径 `...\VC\vcpkg`。

### 3. 编译与运行

- **生成 → 全部生成**（或 `Ctrl+Shift+B`）
- 将启动项设为 **`jydRenderer.exe`**，按 **F5** 调试运行

生成目录：`build\vs2022-x64\Debug\jydRenderer.exe`

### 4. 常见问题

| 问题 | 处理 |
|------|------|
| `vcpkg.cmake` / `CMAKE_CXX_COMPILER not set` | 见下方 **彻底清缓存**；并关闭 VS 的「vcpkg 清单模式」 |
| 找不到 SDL2 | 确保能访问 GitHub，或安装独立 vcpkg 并设置 `VCPKG_ROOT` |
| CMake 配置失败 | 检查是否安装了 **MSVC v143** 和 **Windows SDK** |
| 窗口闪退 | 在 `main.cpp` 设断点，或 **调试 → 窗口 → 异常** 勾选 C++ 异常 |

### 5. 彻底清缓存（vcpkg 报错时必做）

1. **关闭 Visual Studio**
2. 删除项目里的这些目录/文件（若存在）：
   - `build\`
   - `out\`
   - `.vs\`
3. 重新打开项目 → 选 **`Visual Studio 2022 x64`** → **删除缓存并重新配置**

CMake 输出里应出现：`SDL2 not found via vcpkg/find_package; fetching from GitHub...`

### 6. 可选：使用 vcpkg

若以后想用 vcpkg 加速依赖安装：

1. 安装独立 vcpkg，设置 `VCPKG_ROOT=C:\vcpkg`（**不要**用 `...\VC\vcpkg`）
2. 将 `vcpkg.json.example` 复制为 `vcpkg.json`
3. 在 VS 中开启「vcpkg 清单模式」

## 命令行构建（Windows）

```powershell
# 设置 vcpkg 根目录（按你的实际路径修改）
$env:VCPKG_ROOT = "C:\vcpkg"

cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug

.\build\windows-x64-debug\jydRenderer.exe
```

### Linux

```bash
export VCPKG_ROOT="$HOME/vcpkg"

cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug

./build/linux-x64-debug/jydRenderer
```

Release 构建将 preset 中的 `debug` 换成 `release` 即可。

## 切换窗口后端

默认使用 **SDL2**（`JYD_USE_SDL2=ON`）。关闭后可在 `window.cpp` 中接入其他库：

```bash
cmake --preset windows-x64-debug -DJYD_USE_SDL2=OFF
```

## 原生 C 库推荐（实时显示 framebuffer）

| 库 | 语言 | 特点 | 适合场景 |
|---|---|---|---|
| **[SDL2](https://github.com/libsdl-org/SDL)** | C | 跨平台、文档全、`SDL_UpdateWindowSurface` 直接刷像素 | **首选**，tinyrenderer 教程常用 |
| **[GLFW](https://www.glfw.org/)** | C | 轻量窗口 + 输入，配合 OpenGL 纹理上传 framebuffer | 后续要加 GPU 纹理时很自然 |
| **[raylib](https://www.raylib.com/)** | C | 单头文件风格 API，`UpdateTexture` / `DrawTexture` 显示像素 | 快速原型、教学项目 |
| **[SFML 2](https://www.sfml-dev.org/)** (C API) | C++ 为主 | `sfImage` + `sfTexture` 更新显示 | 若接受 C++ 依赖可考虑 |
| **[X11](https://www.x.org/)** / **Win32 GDI** | C | 平台原生、零第三方依赖 | 学习底层，但需分平台写代码 |

### 推荐组合

1. **纯软件渲染入门**：SDL2 + 自家 `Framebuffer`（当前项目方案）
2. **后续加 3D / 纹理**：GLFW + glad + OpenGL `glTexSubImage2D`
3. **最少样板代码**：raylib `Image` → `Texture2D`

### SDL2 核心显示流程（与本项目一致）

```cpp
SDL_Surface* surface = SDL_GetWindowSurface(window);
// 将 RGBA 像素 memcpy 到 surface->pixels
SDL_UpdateWindowSurface(window);
```

性能足够支撑 800×600～1920×1080 的软件光栅化；更大分辨率可考虑 SDL2 纹理 + 硬件加速，或 OpenGL 上传纹理。

## 下一步

- [ ] 实现 MVP 变换与深度缓冲（Z-buffer）
- [ ] 添加纹理映射、透视校正
- [ ] 模型加载（OBJ）
- [ ] 可选：切换到 GLFW + OpenGL 纹理显示
