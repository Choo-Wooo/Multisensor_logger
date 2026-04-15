cmake_minimum_required(VERSION 3.14)
project(bsr20_samples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: use UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ==============================================================================
# SDK paths: sample/window/sdk/{32bit,64bit}/ or sample/linux/sdk/
# ==============================================================================
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(SDK_BIT_DIR "32bit")
    else()
        set(SDK_BIT_DIR "64bit")
    endif()
    set(SDK_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/window/sdk/${SDK_BIT_DIR}")
else()
    if(SDK_BITS)
        set(SDK_BIT_DIR "${SDK_BITS}bit")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(SDK_BIT_DIR "32bit")
    else()
        set(SDK_BIT_DIR "64bit")
    endif()
    set(SDK_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/linux/sdk/${SDK_BIT_DIR}")
endif()

message(STATUS "SDK path: ${SDK_PLATFORM_DIR}")

find_library(BSR20_SDK_LIB
    NAMES BSR20_SDK
    HINTS
        "${SDK_PLATFORM_DIR}/lib"
        "${SDK_PLATFORM_DIR}/bin"
)

if(NOT BSR20_SDK_LIB)
    message(FATAL_ERROR
        "BSR20_SDK library not found in ${SDK_PLATFORM_DIR}\n"
        "Please copy the built SDK to the platform folder first.")
endif()

message(STATUS "Found BSR20_SDK: ${BSR20_SDK_LIB}")

# ==============================================================================
# BSR20 sample (2D radar)
# ==============================================================================
add_executable(bsr20_sample bsr20/cpp/main.cpp)
target_include_directories(bsr20_sample PRIVATE "${SDK_PLATFORM_DIR}/include")
target_link_libraries(bsr20_sample PRIVATE ${BSR20_SDK_LIB})

# ==============================================================================
# HDR20 sample (4D radar)
# ==============================================================================
add_executable(hdr20_sample hdr20/cpp/main.cpp)
target_include_directories(hdr20_sample PRIVATE "${SDK_PLATFORM_DIR}/include")
target_link_libraries(hdr20_sample PRIVATE ${BSR20_SDK_LIB})

# ==============================================================================
# Windows: link ws2_32, copy DLL
# ==============================================================================
if(WIN32)
    target_link_libraries(bsr20_sample PRIVATE ws2_32)
    target_link_libraries(hdr20_sample PRIVATE ws2_32)

    add_custom_command(TARGET bsr20_sample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDK_PLATFORM_DIR}/bin/BSR20_SDK.dll"
            $<TARGET_FILE_DIR:bsr20_sample>
        COMMENT "Copying BSR20_SDK.dll for bsr20_sample"
    )
    add_custom_command(TARGET hdr20_sample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDK_PLATFORM_DIR}/bin/BSR20_SDK.dll"
            $<TARGET_FILE_DIR:hdr20_sample>
        COMMENT "Copying BSR20_SDK.dll for hdr20_sample"
    )
endif()

# ==============================================================================
# BSR20 Radar Viewer (Dear ImGui + OpenGL)
# ==============================================================================
add_subdirectory(bsr20/cpp_vds_viewer)
