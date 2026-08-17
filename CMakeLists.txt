cmake_minimum_required(VERSION 3.20)

# vcpkg：仅在 VS/CMake 尚未注入 toolchain 时，且 VCPKG_ROOT 指向独立安装时使用。
# 不要在 VS2022 preset 里硬编码 CMAKE_TOOLCHAIN_FILE，否则会触发
# "CMAKE_CXX_COMPILER not set"（vcpkg 比 VS 生成器更早运行）。
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(_vcpkg_toolchain "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        if(EXISTS "${_vcpkg_toolchain}")
            set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_toolchain}" CACHE STRING "vcpkg toolchain")
        endif()
        unset(_vcpkg_toolchain)
    endif()
endif()

project(jydRenderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JYD_USE_SDL2 "Use SDL2 for window and framebuffer display" ON)
set(JYD_SDL2_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2"
    CACHE PATH "Path to a local SDL2 source tree")

# Qt's CMake package is normally found through CMAKE_PREFIX_PATH. When qmake
# is already available on PATH, also use its installation prefix so a clean
# Visual Studio configure works without a machine-specific path in this file.
find_program(JYD_QMAKE_EXECUTABLE
    NAMES qmake6 qmake
    HINTS
        "$ENV{CONDA_PREFIX}/Library/bin"
        "$ENV{USERPROFILE}/anaconda3/Library/bin"
        "$ENV{USERPROFILE}/miniconda3/Library/bin"
)
if(JYD_QMAKE_EXECUTABLE)
    execute_process(
        COMMAND "${JYD_QMAKE_EXECUTABLE}" -query QT_INSTALL_PREFIX
        OUTPUT_VARIABLE JYD_QT_INSTALL_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(JYD_QT_INSTALL_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${JYD_QT_INSTALL_PREFIX}")
    endif()
endif()

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(JYD_QT_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    set(JYD_QT_WIDGETS_TARGET Qt5::Widgets)
endif()

if(JYD_USE_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_package(SDL2 QUIET)
    endif()

    if(NOT SDL2_FOUND AND EXISTS "${JYD_SDL2_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "Using vendored SDL2 source: ${JYD_SDL2_SOURCE_DIR}")
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST   OFF CACHE BOOL "" FORCE)
        add_subdirectory(
            "${JYD_SDL2_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/_deps/sdl2-build"
            EXCLUDE_FROM_ALL
        )
    elseif(NOT SDL2_FOUND)
        message(STATUS "SDL2 not found via vcpkg/find_package; fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        release-2.30.10
            GIT_SHALLOW    TRUE
        )
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST   OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL2)
    endif()
endif()

add_executable(jydRenderer
    src/main.cpp
    src/renderer.cpp
    src/framebuffer.cpp
    src/window.cpp
    src/model.cpp
    src/model_selection_dialog.cpp
)

target_include_directories(jydRenderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(jydRenderer PRIVATE ${JYD_QT_WIDGETS_TARGET})

if(JYD_USE_SDL2)
    if(TARGET SDL2::SDL2)
        target_link_libraries(jydRenderer PRIVATE SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        target_link_libraries(jydRenderer PRIVATE SDL2::SDL2-static)
    else()
        message(FATAL_ERROR "SDL2 target not found after find_package/FetchContent")
    endif()
    target_compile_definitions(jydRenderer PRIVATE JYD_USE_SDL2=1)
endif()

if(MSVC)
    target_compile_options(jydRenderer PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET jydRenderer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    target_compile_options(jydRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Make the executable launched directly from Visual Studio self-contained
# enough for local development. The release packaging script performs the
# broader distribution deployment separately.
if(WIN32)
    option(JYD_DEPLOY_DEV_RUNTIME
        "Copy Qt runtime DLLs next to the executable after a Windows build"
        ON)
    if(JYD_DEPLOY_DEV_RUNTIME)
        if(NOT JYD_QT_INSTALL_PREFIX)
            message(FATAL_ERROR
                "Qt was found, but its installation prefix could not be queried. "
                "Make qmake/qmake6 available or set CMAKE_PREFIX_PATH.")
        endif()
        find_program(JYD_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
        add_custom_command(TARGET jydRenderer POST_BUILD
            COMMAND "${JYD_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/deploy-windows-runtime.ps1"
                -Executable "$<TARGET_FILE:jydRenderer>"
                -QtPrefix "${JYD_QT_INSTALL_PREFIX}"
                -Configuration "$<CONFIG>"
            COMMENT "Deploying Qt runtime for direct Visual Studio launch"
            VERBATIM
        )
    endif()
endif()

install(TARGETS jydRenderer RUNTIME DESTINATION bin)
