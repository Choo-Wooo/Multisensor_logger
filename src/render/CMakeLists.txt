add_library(render STATIC
    gl_helpers.cpp
    camera_texture.cpp
    bev_renderer.cpp
)

target_include_directories(render PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${GLAD_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}/backends
    ${SPDLOG_INCLUDE_DIR}
)

target_link_libraries(render PUBLIC OpenGL::GL glad)
