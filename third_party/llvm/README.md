# Bundled LLVM SDK (Windows x64 / MSVC)

This directory is committed as ordinary Git content, not a submodule or Git
LFS pointers. CMake neither downloads LLVM nor searches a machine-local LLVM
installation on Windows/MSVC. Keep the entire directory when copying the project.

## Contents and origin

- LLVM 23.1.2: headers and the 67 static libraries listed in
  windows-x64/libraries.txt, from the official
  clang+llvm-23.1.2-x86_64-pc-windows-msvc development archive:
  https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.2
- zlib 1.3.2 (vcpkg port revision 2), x64-windows release import library and DLL.
- zstd 1.5.7, x64-windows release import library and DLL.
- Upstream licenses are preserved in windows-x64/licenses.
- SHA256SUMS records the shipped libraries, DLLs and licenses.

The subset supports the current native x64 ORC JIT. It is not a full Clang
toolchain or a cross-compilation SDK. The unpacked directory is about 246 MiB;
ordinary Git clones include these binaries and future updates grow Git history.

## Build contract

Use VS2022 C++ desktop tools, the Windows SDK and the VS DIA SDK (diaguids.lib).
Qt is still installed separately. No external LLVM, zlib, zstd or vcpkg is
required. cmake/VendoredLLVM.cmake links this fixed subset directly, avoiding
upstream package exports that reference omitted executables and other targets.

LLVM uses the static release CRT. It is isolated in jydShaderCompiler.dll with
a C/POD boundary; do not pass STL objects or ownership of allocations across
that boundary. The renderer retains its Qt-compatible dynamic CRT.
The bundled compression DLLs are copied beside the compiler DLL after linking.

Linux and other platforms still require a compatible external LLVM development
package. These Windows binaries cannot be reused on another architecture.

## Updating

Replace headers and libraries together from one matching upstream SDK, update
the version and transitive library list in the CMake integration, and replace
the compression import libraries and DLLs as matching pairs. Preserve all
licenses and regenerate SHA256SUMS. Test both Debug and Release with
jydLlvmSmoke (from the repository root) before committing an SDK update.
