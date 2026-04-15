add_library(core STATIC
    serial_port.cpp
    file_dialog.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SPDLOG_INCLUDE_DIR}
)
