# A deliberately small native x64 ORC JIT SDK, stored as ordinary Git files.
# Do not load the upstream LLVMConfig: its exports require unrelated tools.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "The bundled LLVM SDK requires Windows x64 MSVC. Select an x64 configuration.")
endif()

get_filename_component(_jyd_llvm_dir
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/llvm/windows-x64" ABSOLUTE)
set(LLVM_PACKAGE_VERSION "23.1.2")
set(LLVM_INCLUDE_DIRS "${_jyd_llvm_dir}/include")
set(JYD_LLVM_DEFINITIONS
    -D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS
    -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_NONSTDC_NO_WARNINGS
    -D_SCL_SECURE_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS
    -DUNICODE -D_UNICODE -D__STDC_CONSTANT_MACROS
    -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)

file(STRINGS "${_jyd_llvm_dir}/libraries.txt" _jyd_llvm_names)
set(_jyd_llvm_files)
foreach(_jyd_name IN LISTS _jyd_llvm_names)
    list(APPEND _jyd_llvm_files "${_jyd_llvm_dir}/lib/${_jyd_name}")
endforeach()
foreach(_jyd_file IN ITEMS
        ${_jyd_llvm_files}
        "${_jyd_llvm_dir}/include/llvm/Config/llvm-config.h"
        "${_jyd_llvm_dir}/lib/z.lib" "${_jyd_llvm_dir}/bin/z.dll"
        "${_jyd_llvm_dir}/lib/zstd.lib" "${_jyd_llvm_dir}/bin/zstd.dll")
    if(NOT EXISTS "${_jyd_file}")
        message(FATAL_ERROR "Incomplete bundled LLVM SDK: ${_jyd_file}. Restore third_party/llvm from Git; no download is performed by CMake.")
    endif()
endforeach()

find_library(JYD_DIA_GUIDS_LIBRARY NAMES diaguids
    HINTS "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/lib/amd64"
          "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64"
          "${MSVC_DIA_SDK_DIR}/lib/amd64"
    REQUIRED)

foreach(_jyd_compression IN ITEMS z zstd)
    add_library(jyd_llvm_${_jyd_compression} SHARED IMPORTED)
    set_target_properties(jyd_llvm_${_jyd_compression} PROPERTIES
        IMPORTED_IMPLIB "${_jyd_llvm_dir}/lib/${_jyd_compression}.lib"
        IMPORTED_LOCATION "${_jyd_llvm_dir}/bin/${_jyd_compression}.dll")
endforeach()
add_library(jydVendoredLLVM INTERFACE)
target_link_libraries(jydVendoredLLVM INTERFACE
    ${_jyd_llvm_files} jyd_llvm_z jyd_llvm_zstd
    "${JYD_DIA_GUIDS_LIBRARY}"
    psapi shell32 ole32 uuid advapi32 ws2_32 ntdll delayimp)
target_link_options(jydVendoredLLVM INTERFACE
    /DELAYLOAD:shell32.dll /DELAYLOAD:ole32.dll /INCLUDE:malloc)
set(JYD_LLVM_LIBRARIES jydVendoredLLVM)
list(PREPEND CMAKE_PREFIX_PATH "${_jyd_llvm_dir}")
message(STATUS "Using bundled LLVM ${LLVM_PACKAGE_VERSION}: ${_jyd_llvm_dir}")
