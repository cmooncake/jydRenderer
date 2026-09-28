cmake_minimum_required(VERSION 3.21)

project(jydRenderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JYD_USE_SDL2 "Use SDL2 for window and framebuffer display" ON)
option(JYD_ENABLE_LLVM "Build the LLVM-based shader compiler support" ON)
set(JYD_SDL2_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2"
    CACHE PATH "Path to a local SDL2 source tree")
set(JYD_LLVM_ROOT "" CACHE PATH
    "External LLVM root for non-MSVC platforms (Windows MSVC uses the bundled SDK)")

if(JYD_ENABLE_LLVM)
    if(WIN32 AND MSVC)
        include(cmake/VendoredLLVM.cmake)
    else()
        find_package(LLVM REQUIRED CONFIG HINTS
            "${JYD_LLVM_ROOT}/lib/cmake/llvm"
            "$ENV{LLVM_ROOT}/lib/cmake/llvm")
        separate_arguments(JYD_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
        llvm_map_components_to_libnames(JYD_LLVM_LIBRARIES core orcjit support native)
    endif()

    add_library(jydShaderCompiler SHARED
        src/llvm_support.cpp
        src/shader_language.cpp
    )
    target_include_directories(jydShaderCompiler
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_include_directories(jydShaderCompiler SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
    )
    target_compile_options(jydShaderCompiler PRIVATE
        ${JYD_LLVM_DEFINITIONS}
    )
    target_compile_definitions(jydShaderCompiler PRIVATE
        JYD_SHADER_COMPILER_BUILD=1
    )
    target_link_libraries(jydShaderCompiler PRIVATE
        ${JYD_LLVM_LIBRARIES}
    )
    if(MSVC)
        # Official LLVM Windows development archives use the static release
        # CRT. Keep it isolated inside this DLL; the renderer and Qt continue
        # to use their existing dynamic CRT.
        set_property(TARGET jydShaderCompiler PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
    if(WIN32)
        # LLVM's static libraries can retain dynamic compression-library
        # dependencies. Deploy those imported DLLs beside this bridge so both
        # the renderer and the standalone smoke test can start directly.
        add_custom_command(TARGET jydShaderCompiler POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E env
                "JYD_RUNTIME_DLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:jydShaderCompiler>,|>"
                "JYD_RUNTIME_BINARY=$<TARGET_FILE:jydShaderCompiler>"
                "JYD_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:jydShaderCompiler>"
                "JYD_RUNTIME_SEARCH_ROOTS=$<JOIN:${CMAKE_PREFIX_PATH},|>"
                "${CMAKE_COMMAND}" -P
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy-target-runtime.cmake"
            COMMAND_EXPAND_LISTS
            COMMENT "Deploying LLVM runtime dependencies"
            VERBATIM
        )
    endif()

    add_executable(jydLlvmSmoke
        tools/llvm_smoke.cpp
    )
    target_link_libraries(jydLlvmSmoke PRIVATE
        jydShaderCompiler
    )

endif()

# Qt's CMake package is normally found through CMAKE_PREFIX_PATH. When qmake
# is already available on PATH, also use its installation prefix so a clean
# Visual Studio configure works without a machine-specific path in this file.
set(_jyd_qmake_hints
    "$ENV{QTDIR}/bin"
    "$ENV{QT_ROOT}/bin"
    "$ENV{Qt6_ROOT}/bin"
)
if(WIN32)
    # The Qt online installer normally uses C:/Qt/<version>/<kit>. Discover
    # qmake from that convention without baking a Qt version into the project.
    file(GLOB _jyd_windows_qt_bins LIST_DIRECTORIES TRUE
        "C:/Qt/*/msvc*_64/bin"
        "C:/Qt/*/msvc*/bin")
    list(SORT _jyd_windows_qt_bins COMPARE NATURAL ORDER DESCENDING)
    list(APPEND _jyd_qmake_hints ${_jyd_windows_qt_bins})
endif()
find_program(JYD_QMAKE_EXECUTABLE
    NAMES qmake6 qmake
    HINTS
        ${_jyd_qmake_hints}
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
unset(_jyd_qmake_hints)
unset(_jyd_windows_qt_bins)

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
    src/texture.cpp
    src/stb_image.cpp
)

target_include_directories(jydRenderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_link_libraries(jydRenderer PRIVATE ${JYD_QT_WIDGETS_TARGET})

if(JYD_ENABLE_LLVM)
    target_sources(jydRenderer PRIVATE src/llvm_shader.cpp)
    target_link_libraries(jydRenderer PRIVATE jydShaderCompiler)
    target_compile_definitions(jydRenderer PRIVATE JYD_ENABLE_LLVM=1)
    add_custom_command(TARGET jydRenderer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:jydRenderer>/shaders"
        COMMENT "Deploying programmable shader sources"
        VERBATIM
    )
endif()

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
