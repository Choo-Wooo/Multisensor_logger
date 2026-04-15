CMAKE_MINIMUM_REQUIRED(VERSION 3.15)

PROJECT(BSR30_Sample
    VERSION 1.0.0
    LANGUAGES CXX
)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)
SET(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

MESSAGE(STATUS "")
MESSAGE(STATUS "=== BSR30 SDK Sample ===")
MESSAGE(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
MESSAGE(STATUS "")

# SDK 경로 (플랫폼별)
IF(WIN32)
    SET(SDK_ROOT        ${CMAKE_CURRENT_SOURCE_DIR}/window/sdk)
    SET(SDK_INCLUDE_DIR ${SDK_ROOT}/include)
    SET(SDK_LIB_DIR     ${SDK_ROOT}/lib)
    SET(SDK_BIN_DIR     ${SDK_ROOT}/bin)
ELSE()
    SET(SDK_ROOT        ${CMAKE_CURRENT_SOURCE_DIR}/linux/sdk)
    SET(SDK_INCLUDE_DIR ${SDK_ROOT}/include)
    SET(SDK_LIB_DIR     ${SDK_ROOT}/lib)
ENDIF()

MESSAGE(STATUS "SDK root   : ${SDK_ROOT}")

# 실행 파일
ADD_EXECUTABLE(${PROJECT_NAME}
    src/main.cpp
)

# 컴파일 옵션
IF(MSVC)
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE /utf-8 /W3)
    SET(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
ELSE()
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME} PRIVATE -Wall -Wextra)
ENDIF()

# Include
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PRIVATE
    ${SDK_INCLUDE_DIR}
)

# Link
IF(WIN32)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE
        ${SDK_LIB_DIR}/BSR30_SDK.lib
    )

    ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SDK_BIN_DIR}/BSR30_SDK.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SDK_BIN_DIR}/uv.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying BSR30_SDK.dll / uv.dll to output"
    )

ELSE() # Linux
    # .so.1.0.0 직접 링크 (symlink 없이도 동작, Windows 호환 우회)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE
        ${SDK_LIB_DIR}/libBSR30_SDK.so.1.0.0
        pthread
        dl
    )

    # RPATH: 실행 시 sdk/lib 경로에서 .so 탐색
    SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH             "${SDK_LIB_DIR}"
        INSTALL_RPATH           "${SDK_LIB_DIR}"
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )

    # .so symlink 생성 (SDK 디렉토리에 없는 경우 대비)
    ADD_CUSTOM_COMMAND(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            ${SDK_LIB_DIR}/libBSR30_SDK.so.1
            ${SDK_LIB_DIR}/libBSR30_SDK.so
            ${SDK_LIB_DIR}/libuv.so.1
            ${SDK_LIB_DIR}/libuv.so
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libBSR30_SDK.so.1.0.0 ${SDK_LIB_DIR}/libBSR30_SDK.so.1
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libBSR30_SDK.so.1     ${SDK_LIB_DIR}/libBSR30_SDK.so
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libuv.so.1.0.0        ${SDK_LIB_DIR}/libuv.so.1
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libuv.so.1            ${SDK_LIB_DIR}/libuv.so
        COMMENT "Creating shared library symlinks in linux/sdk/lib/"
    )

ENDIF()

# 빌드 결과 안내
IF(WIN32)
    MESSAGE(STATUS "Output: build/${PROJECT_NAME}.exe")
    MESSAGE(STATUS "        (+ BSR30_SDK.dll, uv.dll)")
ELSE()
    MESSAGE(STATUS "Output: build/${PROJECT_NAME}")
    MESSAGE(STATUS "Run   : ./build/${PROJECT_NAME} [radar_ip] [tcp_port] [udp_port]")
ENDIF()
MESSAGE(STATUS "")
