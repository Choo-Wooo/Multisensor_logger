cmake_minimum_required(VERSION 3.14)
project(bsr20_vds_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ==============================================================================
# FetchContent: GLFW + Dear ImGui
# ==============================================================================
include(FetchContent)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
)
FetchContent_MakeAvailable(imgui)

# Build imgui as a static library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)
if(WIN32)
    target_link_libraries(imgui_lib PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui_lib PUBLIC OpenGL::GL)
endif()

# ==============================================================================
# SDK paths
# Set SDK_BUILD_DIR in CLion CMake options to point to your SDK build directory.
#   e.g. -DSDK_BUILD_DIR=C:/Workspace/byda_sdk_v2.0/cmake-build-debug-visual-studio
# Falls back to bundled sample/window/sdk or sample/linux/sdk if not set.
# ==============================================================================
set(SDK_BUILD_DIR "" CACHE PATH "Path to SDK CMake build directory (contains sdk/include, sdk/lib, sdk/bin)")

# ==============================================================================
# SDK path detection
# When built as subdirectory (via sample/CMakeLists.txt), inherit parent's SDK.
# When built standalone (CLion or manual), auto-detect or use bundled SDK.
# ==============================================================================
get_filename_component(_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

if(BSR20_SDK_LIB AND SDK_PLATFORM_DIR)
    # Inherited from parent CMakeLists.txt (add_subdirectory)
    set(SDK_INCLUDE_DIR "${SDK_PLATFORM_DIR}/include")
    set(SDK_DLL_PATH "${SDK_PLATFORM_DIR}/bin/BSR20_SDK.dll")
    message(STATUS "[Radar Viewer] Using parent SDK: ${SDK_PLATFORM_DIR}")
else()
    # Standalone build: auto-detect SDK build dir or use bundled SDK
    if(NOT SDK_BUILD_DIR)
        if(EXISTS "${_REPO_ROOT}/cmake-build-debug-visual-studio/sdk/include/Bsr20Sdk.h")
            set(SDK_BUILD_DIR "${_REPO_ROOT}/cmake-build-debug-visual-studio")
        elseif(EXISTS "${_REPO_ROOT}/cmake-build-release-visual-studio/sdk/include/Bsr20Sdk.h")
            set(SDK_BUILD_DIR "${_REPO_ROOT}/cmake-build-release-visual-studio")
        elseif(EXISTS "${_REPO_ROOT}/build-win64/sdk/include/Bsr20Sdk.h")
            set(SDK_BUILD_DIR "${_REPO_ROOT}/build-win64")
        endif()
    endif()

    if(SDK_BUILD_DIR AND EXISTS "${SDK_BUILD_DIR}/sdk/include/Bsr20Sdk.h")
        set(SDK_INCLUDE_DIR "${SDK_BUILD_DIR}/sdk/include")
        set(BSR20_SDK_LIB "${SDK_BUILD_DIR}/sdk/lib/$<CONFIG>/BSR20_SDK.lib")
        set(SDK_DLL_PATH  "${SDK_BUILD_DIR}/sdk/bin/$<CONFIG>/BSR20_SDK.dll")
        message(STATUS "[Radar Viewer] Using SDK build: ${SDK_BUILD_DIR}")
    else()
        # Fallback: bundled SDK in sample directory
        if(WIN32)
            if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                set(_VW_BIT "32bit")
            else()
                set(_VW_BIT "64bit")
            endif()
            set(_VW_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../window/sdk/${_VW_BIT}")
        else()
            if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                set(_VW_BIT "32bit")
            else()
                set(_VW_BIT "64bit")
            endif()
            set(_VW_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../linux/sdk/${_VW_BIT}")
        endif()
        set(SDK_INCLUDE_DIR "${_VW_SDK}/include")
        if(WIN32 AND EXISTS "${_VW_SDK}/lib/BSR20_SDK.lib")
            set(BSR20_SDK_LIB "${_VW_SDK}/lib/BSR20_SDK.lib")
        else()
            find_library(BSR20_SDK_LIB NAMES BSR20_SDK HINTS "${_VW_SDK}/lib" "${_VW_SDK}/bin")
        endif()
        set(SDK_DLL_PATH "${_VW_SDK}/bin/BSR20_SDK.dll")
        message(STATUS "[Radar Viewer] Using bundled SDK: ${_VW_SDK}")
    endif()

    if(NOT BSR20_SDK_LIB)
        message(FATAL_ERROR "BSR20_SDK library not found.")
    endif()
endif()

# ==============================================================================
# spdlog (header-only from SDK 3rdparty, optional)
# ==============================================================================
set(_SPDLOG_DIR "${_REPO_ROOT}/3rdparty/spdlog/include")
set(_LOGGER_DIR "${_REPO_ROOT}/src/util/log")
set(HAS_SPDLOG FALSE)
if(EXISTS "${_SPDLOG_DIR}/spdlog/spdlog.h" AND EXISTS "${_LOGGER_DIR}/logger.h")
    set(HAS_SPDLOG TRUE)
    message(STATUS "[Radar Viewer] spdlog found")
else()
    message(STATUS "[Radar Viewer] spdlog not found, using printf fallback")
endif()

# ==============================================================================
# Radar Viewer executable
# ==============================================================================
add_executable(bsr20_vds_viewer main.cpp)
target_include_directories(bsr20_vds_viewer PRIVATE "${SDK_INCLUDE_DIR}")

if(HAS_SPDLOG)
    target_include_directories(bsr20_vds_viewer PRIVATE "${_SPDLOG_DIR}" "${_LOGGER_DIR}")
    target_compile_definitions(bsr20_vds_viewer PRIVATE HAS_SPDLOG=1)
endif()
target_link_libraries(bsr20_vds_viewer PRIVATE imgui_lib ${BSR20_SDK_LIB})

if(WIN32)
    target_link_libraries(bsr20_vds_viewer PRIVATE ws2_32)
    # Always copy latest SDK DLL (force, not copy_if_different)
    add_custom_command(TARGET bsr20_vds_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${SDK_DLL_PATH}"
            $<TARGET_FILE_DIR:bsr20_vds_viewer>
        COMMENT "Copying BSR20_SDK.dll for bsr20_vds_viewer"
    )
endif()
