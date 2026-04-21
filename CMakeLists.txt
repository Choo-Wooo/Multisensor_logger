cmake_minimum_required(VERSION 3.16)
project(MultisensorLogger VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# Platform detection
if(WIN32)
    set(PLATFORM "window")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
    add_compile_options(/utf-8)

    # Option: static MSVC runtime (no VC++ Redist needed on target PC)
    option(MSL_STATIC_RUNTIME "Link MSVC runtime statically" OFF)
    if(MSL_STATIC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        message(STATUS "MSL: Using static MSVC runtime")
    endif()
else()
    set(PLATFORM "linux")
endif()

set(THIRDPARTY_DIR ${CMAKE_SOURCE_DIR}/3rdparty)

# =============================================================================
# 3rdparty: spdlog (header-only)
# =============================================================================
set(SPDLOG_INCLUDE_DIR ${THIRDPARTY_DIR}/spdlog/include)

# =============================================================================
# 3rdparty: GLFW (prebuilt shared)
# =============================================================================
set(GLFW_DIR ${THIRDPARTY_DIR}/glfw)

add_library(glfw SHARED IMPORTED)
if(WIN32)
    set_target_properties(glfw PROPERTIES
        IMPORTED_IMPLIB ${GLFW_DIR}/lib/window/glfw3dll.lib
        IMPORTED_LOCATION ${GLFW_DIR}/lib/window/glfw3.dll
        INTERFACE_INCLUDE_DIRECTORIES ${GLFW_DIR}/include
    )
else()
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION ${GLFW_DIR}/lib/linux/libglfw.so
        INTERFACE_INCLUDE_DIRECTORIES ${GLFW_DIR}/include
    )
endif()

# =============================================================================
# 3rdparty: imgui (prebuilt, with GLFW headers bundled)
# =============================================================================
set(IMGUI_DIR ${THIRDPARTY_DIR}/imgui)
set(IMGUI_INCLUDE_DIR ${IMGUI_DIR}/include)

add_library(imgui SHARED IMPORTED)
if(WIN32)
    set_target_properties(imgui PROPERTIES
        IMPORTED_IMPLIB ${IMGUI_DIR}/lib/window/imgui.lib
        IMPORTED_LOCATION ${IMGUI_DIR}/lib/window/imgui.dll
        INTERFACE_INCLUDE_DIRECTORIES "${IMGUI_DIR}/include;${IMGUI_DIR}/include/backends"
    )
else()
    set_target_properties(imgui PROPERTIES
        IMPORTED_LOCATION ${IMGUI_DIR}/lib/linux/libimgui.so
        INTERFACE_INCLUDE_DIRECTORIES "${IMGUI_DIR}/include;${IMGUI_DIR}/include/backends"
    )
endif()

# =============================================================================
# 3rdparty: glad (generated OpenGL 3.3 Core loader)
# =============================================================================
set(GLAD_DIR ${THIRDPARTY_DIR}/glad)
set(GLAD_INCLUDE_DIR ${GLAD_DIR}/include)

add_library(glad STATIC ${GLAD_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${GLAD_INCLUDE_DIR})

find_package(OpenGL REQUIRED)

# =============================================================================
# 3rdparty: libnmea (prebuilt DLL)
# =============================================================================
set(LIBNMEA_DIR ${THIRDPARTY_DIR}/libnmea/${PLATFORM}/sdk)
set(LIBNMEA_INCLUDE_DIR ${LIBNMEA_DIR}/include)

add_library(libnmea SHARED IMPORTED)
if(WIN32)
    set_target_properties(libnmea PROPERTIES
        IMPORTED_IMPLIB ${LIBNMEA_DIR}/lib/nmea.lib
        IMPORTED_LOCATION ${LIBNMEA_DIR}/bin/nmea.dll
        INTERFACE_INCLUDE_DIRECTORIES ${LIBNMEA_INCLUDE_DIR}
    )
else()
    set_target_properties(libnmea PROPERTIES
        IMPORTED_LOCATION ${LIBNMEA_DIR}/lib/libnmea.so
        INTERFACE_INCLUDE_DIRECTORIES ${LIBNMEA_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: BSR20_SDK (32bit/64bit structure)
# =============================================================================
set(BSR20_DIR ${THIRDPARTY_DIR}/BSR20_SDK/${PLATFORM}/sdk/32bit)
set(BSR20_INCLUDE_DIR ${BSR20_DIR}/include)

add_library(bsr20_sdk SHARED IMPORTED)
if(WIN32)
    set_target_properties(bsr20_sdk PROPERTIES
        IMPORTED_IMPLIB ${BSR20_DIR}/lib/BSR20_SDK.lib
        IMPORTED_LOCATION ${BSR20_DIR}/bin/BSR20_SDK.dll
        INTERFACE_INCLUDE_DIRECTORIES ${BSR20_INCLUDE_DIR}
    )
else()
    set_target_properties(bsr20_sdk PROPERTIES
        IMPORTED_LOCATION ${BSR20_DIR}/lib/libBSR20_SDK.so
        INTERFACE_INCLUDE_DIRECTORIES ${BSR20_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: BSR30_SDK
# =============================================================================
set(BSR30_DIR ${THIRDPARTY_DIR}/BSR30_SDK/${PLATFORM}/sdk)
set(BSR30_INCLUDE_DIR ${BSR30_DIR}/include)

add_library(bsr30_sdk SHARED IMPORTED)
if(WIN32)
    set_target_properties(bsr30_sdk PROPERTIES
        IMPORTED_IMPLIB ${BSR30_DIR}/lib/BSR30_SDK.lib
        IMPORTED_LOCATION ${BSR30_DIR}/bin/BSR30_SDK.dll
        INTERFACE_INCLUDE_DIRECTORIES ${BSR30_INCLUDE_DIR}
    )
else()
    set_target_properties(bsr30_sdk PROPERTIES
        IMPORTED_LOCATION ${BSR30_DIR}/lib/libBSR30_SDK.so
        INTERFACE_INCLUDE_DIRECTORIES ${BSR30_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: Ouster SDK
# =============================================================================
set(OUSTER_DIR ${THIRDPARTY_DIR}/ouster-sdk)
set(OUSTER_INCLUDE_DIR ${OUSTER_DIR}/include)

add_library(ouster_sdk SHARED IMPORTED)
if(WIN32)
    set_target_properties(ouster_sdk PROPERTIES
        IMPORTED_IMPLIB ${OUSTER_DIR}/lib/window/shared_library.lib
        IMPORTED_LOCATION ${OUSTER_DIR}/lib/window/shared_library.dll
        INTERFACE_INCLUDE_DIRECTORIES ${OUSTER_INCLUDE_DIR}
    )
else()
    set_target_properties(ouster_sdk PROPERTIES
        IMPORTED_LOCATION ${OUSTER_DIR}/lib/linux/libshared_library.so
        INTERFACE_INCLUDE_DIRECTORIES ${OUSTER_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: CameraCapture (ccap)
# =============================================================================
set(CCAP_DIR ${THIRDPARTY_DIR}/CameraCapture)
set(CCAP_INCLUDE_DIR ${CCAP_DIR}/include)

# ccap is Windows-only in this project (no Linux prebuilt binary available)
if(WIN32)
    add_library(ccap SHARED IMPORTED)
    set_target_properties(ccap PROPERTIES
        IMPORTED_IMPLIB ${CCAP_DIR}/lib/window/async/ccap.lib
        IMPORTED_LOCATION ${CCAP_DIR}/lib/window/async/ccap.dll
        INTERFACE_INCLUDE_DIRECTORIES ${CCAP_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: FFmpeg
# =============================================================================
set(FFMPEG_DIR ${THIRDPARTY_DIR}/FFmpeg)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_DIR}/include)

add_library(ffmpeg_avcodec SHARED IMPORTED)
add_library(ffmpeg_avutil SHARED IMPORTED)
add_library(ffmpeg_swscale SHARED IMPORTED)
add_library(ffmpeg_avformat SHARED IMPORTED)

if(WIN32)
    set_target_properties(ffmpeg_avcodec PROPERTIES
        IMPORTED_IMPLIB ${FFMPEG_DIR}/lib/window/avcodec.lib
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/window/avcodec-62.dll
    )
    set_target_properties(ffmpeg_avutil PROPERTIES
        IMPORTED_IMPLIB ${FFMPEG_DIR}/lib/window/avutil.lib
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/window/avutil-60.dll
    )
    set_target_properties(ffmpeg_swscale PROPERTIES
        IMPORTED_IMPLIB ${FFMPEG_DIR}/lib/window/swscale.lib
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/window/swscale-9.dll
    )
    set_target_properties(ffmpeg_avformat PROPERTIES
        IMPORTED_IMPLIB ${FFMPEG_DIR}/lib/window/avformat.lib
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/window/avformat-62.dll
    )
else()
    set_target_properties(ffmpeg_avcodec PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/linux/libavcodec.so
        IMPORTED_NO_SONAME TRUE
    )
    set_target_properties(ffmpeg_avutil PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/linux/libavutil.so
        IMPORTED_NO_SONAME TRUE
    )
    set_target_properties(ffmpeg_swscale PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/linux/libswscale.so
        IMPORTED_NO_SONAME TRUE
    )
    set_target_properties(ffmpeg_avformat PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_DIR}/lib/linux/libavformat.so
        IMPORTED_NO_SONAME TRUE
    )
endif()

# =============================================================================
# 3rdparty: live555
# =============================================================================
set(LIVE555_DIR ${THIRDPARTY_DIR}/live555)
set(LIVE555_INCLUDE_DIR
    ${LIVE555_DIR}/include/liveMedia
    ${LIVE555_DIR}/include/groupsock
    ${LIVE555_DIR}/include/UsageEnvironment
    ${LIVE555_DIR}/include/BasicUsageEnvironment
)

# live555 is built as static libraries
add_library(live555_liveMedia STATIC IMPORTED)
add_library(live555_groupsock STATIC IMPORTED)
add_library(live555_UsageEnvironment STATIC IMPORTED)
add_library(live555_BasicUsageEnvironment STATIC IMPORTED)

if(WIN32)
    set_target_properties(live555_liveMedia PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/liveMedia.lib)
    set_target_properties(live555_groupsock PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/groupsock.lib)
    set_target_properties(live555_UsageEnvironment PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/UsageEnvironment.lib)
    set_target_properties(live555_BasicUsageEnvironment PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/BasicUsageEnvironment.lib)
else()
    set_target_properties(live555_liveMedia PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/libliveMedia.a)
    set_target_properties(live555_groupsock PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/libgroupsock.a)
    set_target_properties(live555_UsageEnvironment PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/libUsageEnvironment.a)
    set_target_properties(live555_BasicUsageEnvironment PROPERTIES
        IMPORTED_LOCATION ${LIVE555_DIR}/lib/${PLATFORM}/libBasicUsageEnvironment.a)
endif()

# =============================================================================
# 3rdparty: OpenSSL
# =============================================================================
set(OPENSSL_DIR ${THIRDPARTY_DIR}/OpenSSL)

add_library(openssl_ssl SHARED IMPORTED)
add_library(openssl_crypto SHARED IMPORTED)
if(WIN32)
    set_target_properties(openssl_ssl PROPERTIES
        IMPORTED_IMPLIB ${OPENSSL_DIR}/lib/window/lib/libssl.lib
        IMPORTED_LOCATION ${OPENSSL_DIR}/lib/window/bin/libssl-3.dll
    )
    set_target_properties(openssl_crypto PROPERTIES
        IMPORTED_IMPLIB ${OPENSSL_DIR}/lib/window/lib/libcrypto.lib
        IMPORTED_LOCATION ${OPENSSL_DIR}/lib/window/bin/libcrypto-3.dll
    )
else()
    set_target_properties(openssl_ssl PROPERTIES
        IMPORTED_LOCATION ${OPENSSL_DIR}/lib/linux/lib/libssl.so
        IMPORTED_NO_SONAME TRUE
    )
    set_target_properties(openssl_crypto PROPERTIES
        IMPORTED_LOCATION ${OPENSSL_DIR}/lib/linux/lib/libcrypto.so
        IMPORTED_NO_SONAME TRUE
    )
endif()

# =============================================================================
# Source
# =============================================================================
add_subdirectory(src)

# =============================================================================
# Install / Packaging
# =============================================================================

# Install the executable and all DLLs it needs
install(TARGETS MultisensorLogger
    RUNTIME DESTINATION .
)

# Install config file
install(FILES ${CMAKE_SOURCE_DIR}/config.ini DESTINATION .)

# Install all required DLLs (Windows) — explicit list from 3rdparty paths.
# (install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/) would miss bin/Release/
# under VS multi-config builds.)
if(WIN32)
    set(RUNTIME_DLLS
        ${LIBNMEA_DIR}/bin/nmea.dll
        ${BSR20_DIR}/bin/BSR20_SDK.dll
        ${BSR30_DIR}/bin/BSR30_SDK.dll
        ${BSR30_DIR}/bin/uv.dll
        ${GLFW_DIR}/lib/window/glfw3.dll
        ${IMGUI_DIR}/lib/window/imgui.dll
        ${CCAP_DIR}/lib/window/async/ccap.dll
        ${OUSTER_DIR}/lib/window/shared_library.dll
        ${FFMPEG_DIR}/lib/window/avcodec-62.dll
        ${FFMPEG_DIR}/lib/window/avutil-60.dll
        ${FFMPEG_DIR}/lib/window/swscale-9.dll
        ${FFMPEG_DIR}/lib/window/avformat-62.dll
        ${FFMPEG_DIR}/lib/window/libgcc_s_dw2-1.dll
        ${FFMPEG_DIR}/lib/window/libiconv-2.dll
        ${FFMPEG_DIR}/lib/window/libwinpthread-1.dll
        ${FFMPEG_DIR}/lib/window/libx264-165.dll
        ${FFMPEG_DIR}/lib/window/libva.dll
        ${FFMPEG_DIR}/lib/window/libva_win32.dll
        ${FFMPEG_DIR}/lib/window/zlib1.dll
        ${FFMPEG_DIR}/lib/window/libbz2-1.dll
        ${OPENSSL_DIR}/lib/window/bin/libssl-3.dll
        ${OPENSSL_DIR}/lib/window/bin/libcrypto-3.dll
    )
    foreach(DLL ${RUNTIME_DLLS})
        if(EXISTS ${DLL})
            install(FILES ${DLL} DESTINATION .)
        else()
            message(WARNING "CPack: DLL not found, skipping: ${DLL}")
        endif()
    endforeach()
endif()

# =============================================================================
# CPack configuration
# =============================================================================
set(CPACK_PACKAGE_NAME "MultisensorLogger")
set(CPACK_PACKAGE_VENDOR "BYDA")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multi-Sensor Data Logger")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MultisensorLogger")

if(WIN32)
    # Generate both: NSIS (.exe installer) + ZIP (portable)
    set(CPACK_GENERATOR "NSIS;ZIP")

    # NSIS-specific settings
    set(CPACK_NSIS_DISPLAY_NAME "Multi-Sensor Logger")
    set(CPACK_NSIS_PACKAGE_NAME "Multi-Sensor Logger ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_CONTACT "BYDA")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)

    # Start Menu shortcut — CPACK_PACKAGE_EXECUTABLES is a pair:
    # "<exe-name-without-ext>" "<display-name>"
    set(CPACK_PACKAGE_EXECUTABLES "MultisensorLogger;Multi-Sensor Logger")
    set(CPACK_CREATE_DESKTOP_LINKS "MultisensorLogger")

    # NSIS assumes exe lives in bin/ by default. We install to the root ("."),
    # so tell NSIS shortcut creator to look there.
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")

    # Optional: installer icon (if you have .ico file)
    # set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/docs/icon.ico")
    # set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/docs/icon.ico")

    # Default install location: C:\Program Files (x86)\MultisensorLogger
    # For 32-bit build, NSIS defaults to Program Files (x86) automatically
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
