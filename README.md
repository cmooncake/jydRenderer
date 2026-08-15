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

- CMake 3.20+（VS2022 自带，需勾选「使用 C++ 的桌面开发」）
- Qt 5.15+ 或 Qt 6，需包含 Widgets 组件
- SDL2（首次配置时 CMake 自动从 GitHub 下载，**无需 vcpkg**）
- Ninja（可选，命令行构建用；VS2022 preset 不需要）

## Visual Studio 2022（推荐）

### 1. 前置准备

1. 安装 VS2022，工作负载勾选 **「使用 C++ 的桌面开发」**
2. 安装 Qt，并确保 `qmake`/`qmake6` 位于 `PATH`；也可以通过 `CMAKE_PREFIX_PATH` 指定 Qt 安装目录
3. **不需要 vcpkg，不需要设置 VCPKG_ROOT**
4. VS 菜单 **工具 → 选项 → CMake → 常规**，建议 **取消勾选「vcpkg 清单模式」**（避免 VS 注入内置 vcpkg 报错）

### 2. 打开项目

1. **文件 → 打开 → 文件夹**，选择 `jydRenderer` 目录
2. 顶部 CMake 配置下拉框选 **`Visual Studio 2022 x64`**（不要选 Ninja preset，除非在「x64 Native Tools 命令提示符」里用命令行）
3. **项目 → 删除缓存并重新配置**
4. 等待 CMake 配置完成（无 vcpkg 时会自动从 GitHub 下载 SDL2，首次需联网）


### 3. 编译与运行

- **生成 → 全部生成**（或 `Ctrl+Shift+B`）
- 将启动项设为 **`jydRenderer.exe`**，按 **F5** 调试运行
- 程序启动后先选择一个 `.obj` 模型；确认并加载成功后才会创建 SDL 渲染窗口

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

