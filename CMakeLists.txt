cmake_minimum_required(VERSION 3.16)
project(MultisensorLogger VERSION 2.0.1 LANGUAGES C CXX)

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

    # Architecture detection: 3rdparty linux binaries live in per-arch
    # subdirectories (x86_64 / aarch64; legacy 32-bit i386 files kept in x86).
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _msl_proc)
    if(_msl_proc MATCHES "aarch64|arm64")
        set(MSL_ARCH "aarch64")
    elseif(_msl_proc MATCHES "x86_64|amd64")
        set(MSL_ARCH "x86_64")
    elseif(_msl_proc MATCHES "^(i[3-6]86|x86)$")
        set(MSL_ARCH "x86")
    else()
        message(FATAL_ERROR "MSL: unsupported Linux architecture '${CMAKE_SYSTEM_PROCESSOR}'")
    endif()
    message(STATUS "MSL: Linux target arch = ${MSL_ARCH} (CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})")
endif()

set(THIRDPARTY_DIR ${CMAKE_SOURCE_DIR}/3rdparty)

# Warn at configure time when a 3rdparty binary for this arch is not present
# (the build would otherwise fail much later at link time).
function(msl_require_binary _name _path)
    if(NOT EXISTS ${_path})
        message(WARNING "MSL: ${_name}: no ${MSL_ARCH} binary at ${_path} — link will fail until it is provided")
    endif()
endfunction()

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
    msl_require_binary(glfw ${GLFW_DIR}/lib/linux/${MSL_ARCH}/libglfw.so.3)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION ${GLFW_DIR}/lib/linux/${MSL_ARCH}/libglfw.so.3
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
    msl_require_binary(imgui ${IMGUI_DIR}/lib/linux/${MSL_ARCH}/libimgui.so.1)
    set_target_properties(imgui PROPERTIES
        IMPORTED_LOCATION ${IMGUI_DIR}/lib/linux/${MSL_ARCH}/libimgui.so.1
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
    msl_require_binary(libnmea ${LIBNMEA_DIR}/lib/${MSL_ARCH}/libnmea.so.0.1.2)
    set_target_properties(libnmea PROPERTIES
        IMPORTED_LOCATION ${LIBNMEA_DIR}/lib/${MSL_ARCH}/libnmea.so.0.1.2
        INTERFACE_INCLUDE_DIRECTORIES ${LIBNMEA_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: BSR20_SDK
#   window: sdk/32bit (32-bit app build)
#   linux : sdk/include + sdk/lib/<arch>
# =============================================================================
add_library(bsr20_sdk SHARED IMPORTED)
if(WIN32)
    set(BSR20_DIR ${THIRDPARTY_DIR}/BSR20_SDK/window/sdk/32bit)
    set(BSR20_INCLUDE_DIR ${BSR20_DIR}/include)
    set_target_properties(bsr20_sdk PROPERTIES
        IMPORTED_IMPLIB ${BSR20_DIR}/lib/BSR20_SDK.lib
        IMPORTED_LOCATION ${BSR20_DIR}/bin/BSR20_SDK.dll
        INTERFACE_INCLUDE_DIRECTORIES ${BSR20_INCLUDE_DIR}
    )
else()
    set(BSR20_DIR ${THIRDPARTY_DIR}/BSR20_SDK/linux/sdk)
    set(BSR20_INCLUDE_DIR ${BSR20_DIR}/include)
    msl_require_binary(bsr20_sdk ${BSR20_DIR}/lib/${MSL_ARCH}/libBSR20_SDK.so.2)
    set_target_properties(bsr20_sdk PROPERTIES
        IMPORTED_LOCATION ${BSR20_DIR}/lib/${MSL_ARCH}/libBSR20_SDK.so.2
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
    # Runtime dependency: libuv.so.1 must ship next to libBSR30_SDK.so.1
    msl_require_binary(bsr30_sdk ${BSR30_DIR}/lib/${MSL_ARCH}/libBSR30_SDK.so.1)
    set_target_properties(bsr30_sdk PROPERTIES
        IMPORTED_LOCATION ${BSR30_DIR}/lib/${MSL_ARCH}/libBSR30_SDK.so.1
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
    msl_require_binary(ouster_sdk ${OUSTER_DIR}/lib/linux/${MSL_ARCH}/libshared_library.so)
    set_target_properties(ouster_sdk PROPERTIES
        IMPORTED_LOCATION ${OUSTER_DIR}/lib/linux/${MSL_ARCH}/libshared_library.so
        INTERFACE_INCLUDE_DIRECTORIES ${OUSTER_INCLUDE_DIR}
    )
endif()

# =============================================================================
# 3rdparty: CameraCapture (ccap)
# =============================================================================
set(CCAP_DIR ${THIRDPARTY_DIR}/CameraCapture)
set(CCAP_INCLUDE_DIR ${CCAP_DIR}/include)

# ccap: Windows prebuilt + Linux aarch64 prebuilt (Jetson build).
# Note: camera_usb_worker.cpp is still guarded by #ifdef _WIN32, so the Linux
# target is only defined here for future use / deployment of libccap.so.
if(WIN32)
    add_library(ccap SHARED IMPORTED)
    set_target_properties(ccap PROPERTIES
        IMPORTED_IMPLIB ${CCAP_DIR}/lib/window/async/ccap.lib
        IMPORTED_LOCATION ${CCAP_DIR}/lib/window/async/ccap.dll
        INTERFACE_INCLUDE_DIRECTORIES ${CCAP_INCLUDE_DIR}
    )
elseif(EXISTS ${CCAP_DIR}/lib/linux/${MSL_ARCH}/libccap.so)
    add_library(ccap SHARED IMPORTED)
    set_target_properties(ccap PROPERTIES
        IMPORTED_LOCATION ${CCAP_DIR}/lib/linux/${MSL_ARCH}/libccap.so
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
if(NOT WIN32)
    # libavformat/libavcodec 이 내부적으로 libswresample 을 참조한다. 이 라이브러리는
    # RUNPATH 를 갖고 있지 않아서(그리고 실행 파일의 RUNPATH 는 "간접" 의존성 탐색에
    # 쓰이지 않아서) 명시적으로 링크해 두지 않으면 빌드 트리에서 바로 실행할 때
    #   error while loading shared libraries: libswresample.so.6
    # 가 난다. 직접 의존성으로 만들면 실행 파일의 RPATH 로 해결된다.
    add_library(ffmpeg_swresample SHARED IMPORTED)
endif()

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
    set(FFMPEG_LIB_DIR ${FFMPEG_DIR}/lib/linux/${MSL_ARCH})
    msl_require_binary(ffmpeg_avcodec  ${FFMPEG_LIB_DIR}/libavcodec.so.62)
    msl_require_binary(ffmpeg_avutil   ${FFMPEG_LIB_DIR}/libavutil.so.60)
    msl_require_binary(ffmpeg_swscale  ${FFMPEG_LIB_DIR}/libswscale.so.9)
    msl_require_binary(ffmpeg_avformat ${FFMPEG_LIB_DIR}/libavformat.so.62)
    set_target_properties(ffmpeg_avcodec PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavcodec.so.62
    )
    set_target_properties(ffmpeg_avutil PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavutil.so.60
    )
    set_target_properties(ffmpeg_swscale PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libswscale.so.9
    )
    set_target_properties(ffmpeg_avformat PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavformat.so.62
    )
    msl_require_binary(ffmpeg_swresample ${FFMPEG_LIB_DIR}/libswresample.so.6)
    set_target_properties(ffmpeg_swresample PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libswresample.so.6
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
    set(LIVE555_LIB_DIR ${LIVE555_DIR}/lib/linux/${MSL_ARCH})
    msl_require_binary(live555 ${LIVE555_LIB_DIR}/libliveMedia.a)
    set_target_properties(live555_liveMedia PROPERTIES
        IMPORTED_LOCATION ${LIVE555_LIB_DIR}/libliveMedia.a)
    set_target_properties(live555_groupsock PROPERTIES
        IMPORTED_LOCATION ${LIVE555_LIB_DIR}/libgroupsock.a)
    set_target_properties(live555_UsageEnvironment PROPERTIES
        IMPORTED_LOCATION ${LIVE555_LIB_DIR}/libUsageEnvironment.a)
    set_target_properties(live555_BasicUsageEnvironment PROPERTIES
        IMPORTED_LOCATION ${LIVE555_LIB_DIR}/libBasicUsageEnvironment.a)
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
    set(OPENSSL_LIB_DIR ${OPENSSL_DIR}/lib/linux/lib/${MSL_ARCH})
    msl_require_binary(openssl ${OPENSSL_LIB_DIR}/libssl.so.3)
    set_target_properties(openssl_ssl PROPERTIES
        IMPORTED_LOCATION ${OPENSSL_LIB_DIR}/libssl.so.3
    )
    set_target_properties(openssl_crypto PROPERTIES
        IMPORTED_LOCATION ${OPENSSL_LIB_DIR}/libcrypto.so.3
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
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
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

    # --- Branding (icons) ---
    # CMake/NSIS accepts forward slashes — no need to convert to native, and
    # converting via TO_NATIVE_PATH causes CMP0010 warnings when CPackConfig
    # re-parses the resulting backslashes as bad escape sequences ('\W' etc.).
    set(_msl_app_icon "${CMAKE_SOURCE_DIR}/assets/icons/icon.ico")
    if(EXISTS ${_msl_app_icon})
        set(CPACK_NSIS_MUI_ICON           "${_msl_app_icon}")
        set(CPACK_NSIS_MUI_UNIICON        "${_msl_app_icon}")
        # Icon shown in Add/Remove Programs (Windows 10+ uses the .exe's
        # embedded icon, but we set DisplayIcon explicitly for older versions).
        set(CPACK_NSIS_INSTALLED_ICON_NAME "MultisensorLogger.exe")
        message(STATUS "MSL: Installer icon = ${_msl_app_icon}")
    else()
        message(WARNING "MSL: Installer icon not found at ${_msl_app_icon}")
    endif()

    # Bundle the assets folder so the installed app can use icons at runtime.
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION .)

    # Default install location: C:\Program Files (x86)\MultisensorLogger
    # For 32-bit build, NSIS defaults to Program Files (x86) automatically
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
