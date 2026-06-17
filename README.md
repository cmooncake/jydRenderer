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


### 3. 编译与运行

- **生成 → 全部生成**（或 `Ctrl+Shift+B`）
- 将启动项设为 **`jydRenderer.exe`**，按 **F5** 调试运行

生成目录：`build\vs2022-x64\Debug\jydRenderer.exe`

