# formats/ is header-only (all inline in .h files)
add_library(formats INTERFACE)
target_include_directories(formats INTERFACE ${CMAKE_SOURCE_DIR}/src)
