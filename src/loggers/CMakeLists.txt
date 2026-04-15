add_library(loggers STATIC
    camera_logger.cpp
    lidar_logger.cpp
)

target_include_directories(loggers PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${SPDLOG_INCLUDE_DIR}
    ${FFMPEG_INCLUDE_DIR}
)

target_link_libraries(loggers PUBLIC core formats)
