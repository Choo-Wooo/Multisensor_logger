add_library(sensors STATIC
    gps_worker.cpp
    radar20_worker.cpp
    radar30_worker.cpp
    camera_rtsp_worker.cpp
    camera_usb_worker.cpp
    ouster_worker.cpp
)

# OpenSSL include path (needed by live555 TLSState.hh)
if(WIN32)
    set(OPENSSL_INCLUDE_DIR ${THIRDPARTY_DIR}/OpenSSL/lib/window/include)
else()
    set(OPENSSL_INCLUDE_DIR ${THIRDPARTY_DIR}/OpenSSL/lib/linux/include)
endif()

# Ouster optional-lite path
set(OUSTER_OPTIONAL_DIR ${THIRDPARTY_DIR}/ouster-sdk/include/optional-lite)

target_include_directories(sensors PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${SPDLOG_INCLUDE_DIR}
    ${LIBNMEA_INCLUDE_DIR}
    ${BSR20_INCLUDE_DIR}
    ${BSR30_INCLUDE_DIR}
    ${OUSTER_INCLUDE_DIR}
    ${OUSTER_OPTIONAL_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${LIVE555_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
)

# ccap include is only needed on Windows (Linux has header but no prebuilt binary)
if(WIN32)
    target_include_directories(sensors PUBLIC ${CCAP_INCLUDE_DIR})
endif()

target_link_libraries(sensors PUBLIC core)
