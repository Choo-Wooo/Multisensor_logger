add_library(ui STATIC
    main_window.cpp
    settings_panel.cpp
    player_panel.cpp
    camera_view.cpp
    sensor_info_view.cpp
    bev_view.cpp
    gps_map_view.cpp
)

target_include_directories(ui PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${GLAD_INCLUDE_DIR}
    ${CCAP_INCLUDE_DIR}
)

target_link_libraries(ui PUBLIC core render)
