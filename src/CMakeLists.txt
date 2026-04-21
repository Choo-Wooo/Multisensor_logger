add_subdirectory(core)
add_subdirectory(formats)
add_subdirectory(sensors)
add_subdirectory(loggers)
add_subdirectory(player)
add_subdirectory(render)
add_subdirectory(ui)

add_executable(MultisensorLogger main.cpp)

# Set RPATH so Linux exe finds .so files in ./lib (relative to exe)
if(UNIX AND NOT APPLE)
    set_target_properties(MultisensorLogger PROPERTIES
        INSTALL_RPATH "$ORIGIN/lib:$ORIGIN"
        BUILD_WITH_INSTALL_RPATH FALSE
    )
endif()

target_include_directories(MultisensorLogger PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SPDLOG_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}/backends
    ${GLFW_DIR}/include
    ${GLAD_INCLUDE_DIR}
    ${LIBNMEA_INCLUDE_DIR}
)

if(UNIX AND NOT APPLE)
    # Linux: wrap internal static libs with --start-group to resolve circular deps
    target_link_libraries(MultisensorLogger PRIVATE
        -Wl,--start-group
        ui player sensors loggers render core formats
        -Wl,--end-group

        # UI/Graphics
        imgui glfw OpenGL::GL glad

        # Sensors
        ouster_sdk bsr20_sdk bsr30_sdk libnmea

        # Video
        ffmpeg_avcodec ffmpeg_avutil ffmpeg_swscale ffmpeg_avformat
        live555_liveMedia live555_groupsock
        live555_UsageEnvironment live555_BasicUsageEnvironment

        # Security
        openssl_ssl openssl_crypto
    )
    # Allow 3rdparty shared libs to have unresolved symbols (e.g. FFmpeg internal refs)
    target_link_options(MultisensorLogger PRIVATE -Wl,--allow-shlib-undefined)
else()
    target_link_libraries(MultisensorLogger PRIVATE
        # Internal modules
        core formats sensors loggers player render ui

        # UI/Graphics
        imgui glfw OpenGL::GL glad

        # Sensors (ccap is Windows-only, linked below)
        ouster_sdk bsr20_sdk bsr30_sdk libnmea

        # Video
        ffmpeg_avcodec ffmpeg_avutil ffmpeg_swscale ffmpeg_avformat
        live555_liveMedia live555_groupsock
        live555_UsageEnvironment live555_BasicUsageEnvironment

        # Security
        openssl_ssl openssl_crypto
    )
endif()

# Windows-specific link libraries
if(WIN32)
    target_link_libraries(MultisensorLogger PRIVATE ws2_32 setupapi ccap)
endif()

# Linux-specific link libraries (dl for glad OpenGL loader, pthread for std::thread)
if(UNIX AND NOT APPLE)
    target_link_libraries(MultisensorLogger PRIVATE dl pthread)
endif()

# Copy DLLs to output directory on Windows
if(WIN32)
    set(DLL_LIST
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
    foreach(DLL_PATH ${DLL_LIST})
        if(EXISTS ${DLL_PATH})
            add_custom_command(TARGET MultisensorLogger POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${DLL_PATH}
                    $<TARGET_FILE_DIR:MultisensorLogger>
            )
        endif()
    endforeach()
endif()
