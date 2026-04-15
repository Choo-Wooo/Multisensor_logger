# CMakeLists.txt for live555 streaming media library
# This file is used by build_window.bat to build live555 with CMake
cmake_minimum_required(VERSION 3.10)
project(live555 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

# Root of the extracted live555 source
set(LIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/live")

# Include directories
set(LIVE_INCLUDE_DIRS
    ${LIVE_ROOT}/UsageEnvironment/include
    ${LIVE_ROOT}/BasicUsageEnvironment/include
    ${LIVE_ROOT}/groupsock/include
    ${LIVE_ROOT}/liveMedia/include
)

# Common compile definitions
add_compile_definitions(NO_OPENSSL=1)
if(WIN32)
    add_compile_definitions(SOCKLEN_T=int)
    add_compile_definitions(LOCALE_NOT_USED)
    add_compile_definitions(NO_GETIFADDRS)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# --- UsageEnvironment (static library) ---
add_library(UsageEnvironment STATIC
    ${LIVE_ROOT}/UsageEnvironment/HashTable.cpp
    ${LIVE_ROOT}/UsageEnvironment/strDup.cpp
    ${LIVE_ROOT}/UsageEnvironment/UsageEnvironment.cpp
)
target_include_directories(UsageEnvironment PUBLIC ${LIVE_INCLUDE_DIRS})

# --- BasicUsageEnvironment (static library) ---
add_library(BasicUsageEnvironment STATIC
    ${LIVE_ROOT}/BasicUsageEnvironment/BasicHashTable.cpp
    ${LIVE_ROOT}/BasicUsageEnvironment/BasicTaskScheduler.cpp
    ${LIVE_ROOT}/BasicUsageEnvironment/BasicTaskScheduler0.cpp
    ${LIVE_ROOT}/BasicUsageEnvironment/BasicUsageEnvironment.cpp
    ${LIVE_ROOT}/BasicUsageEnvironment/BasicUsageEnvironment0.cpp
    ${LIVE_ROOT}/BasicUsageEnvironment/DelayQueue.cpp
)
target_include_directories(BasicUsageEnvironment PUBLIC ${LIVE_INCLUDE_DIRS})

# --- groupsock (static library) ---
add_library(groupsock STATIC
    ${LIVE_ROOT}/groupsock/GroupEId.cpp
    ${LIVE_ROOT}/groupsock/Groupsock.cpp
    ${LIVE_ROOT}/groupsock/GroupsockHelper.cpp
    ${LIVE_ROOT}/groupsock/inet.c
    ${LIVE_ROOT}/groupsock/IOHandlers.cpp
    ${LIVE_ROOT}/groupsock/NetAddress.cpp
    ${LIVE_ROOT}/groupsock/NetInterface.cpp
)
target_include_directories(groupsock PUBLIC ${LIVE_INCLUDE_DIRS})

# --- liveMedia (static library) ---
file(GLOB LIVEMEDIA_CPP_SOURCES "${LIVE_ROOT}/liveMedia/*.cpp")
file(GLOB LIVEMEDIA_C_SOURCES "${LIVE_ROOT}/liveMedia/*.c")
add_library(liveMedia STATIC
    ${LIVEMEDIA_CPP_SOURCES}
    ${LIVEMEDIA_C_SOURCES}
)
target_include_directories(liveMedia PUBLIC ${LIVE_INCLUDE_DIRS})

# Windows system libraries
if(WIN32)
    target_link_libraries(liveMedia PUBLIC ws2_32)
    target_link_libraries(groupsock PUBLIC ws2_32)
endif()
