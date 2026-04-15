add_library(player STATIC
    data_loader.cpp
    session_player.cpp
    pcap_reader.cpp
    camera_prefetch.cpp
)

target_include_directories(player PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${SPDLOG_INCLUDE_DIR}
    ${OUSTER_INCLUDE_DIR}
    ${THIRDPARTY_DIR}/ouster-sdk/include/optional-lite
    ${FFMPEG_INCLUDE_DIR}
    ${FFMPEG_DIR}/include
)

target_link_libraries(player PUBLIC core formats)
