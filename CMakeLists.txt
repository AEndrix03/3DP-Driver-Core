cmake_minimum_required(VERSION 3.20)
project(3DP_Driver_Core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)
include_directories(external/serial/include)
include_directories(external/nlohmann/json/single_include)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

# Zlib setup
add_subdirectory(external/zlib)

# Find librdkafka (Windows)
find_path(LIBRDKAFKA_INCLUDE_DIR
        NAMES librdkafka/rdkafka.h
        PATHS
        ${CMAKE_PREFIX_PATH}/include
        "C:/vcpkg/installed/x64-windows/include"
        "C:/vcpkg/installed/x86-windows/include"
)

find_library(LIBRDKAFKA_LIBRARY
        NAMES rdkafka
        PATHS
        ${CMAKE_PREFIX_PATH}/lib
        "C:/vcpkg/installed/x64-windows/lib"
        "C:/vcpkg/installed/x86-windows/lib"
)

if (NOT LIBRDKAFKA_INCLUDE_DIR OR NOT LIBRDKAFKA_LIBRARY)
    message(FATAL_ERROR "librdkafka not found! Install with: vcpkg install librdkafka")
endif ()

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*main_.*\\.cpp$")

file(GLOB_RECURSE CONNECTOR_SOURCES CONFIGURE_DEPENDS src/connector/*.cpp)

# Main executable
add_executable(3DP_Driver_Core
        src/main.cpp
        ${SOURCES}
        ${CONNECTOR_SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc
)

# Static library
add_library(3dp_driver STATIC
        ${SOURCES}
        ${CONNECTOR_SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc
)

# Include directories
target_include_directories(3DP_Driver_Core PRIVATE ${LIBRDKAFKA_INCLUDE_DIR})
target_include_directories(3dp_driver PRIVATE ${LIBRDKAFKA_INCLUDE_DIR})

# Link libraries
target_link_libraries(3DP_Driver_Core PRIVATE
        zlibstatic
        ${LIBRDKAFKA_LIBRARY}
        ws2_32
        secur32
        crypt32
)

target_link_libraries(3dp_driver PRIVATE
        zlibstatic
        ${LIBRDKAFKA_LIBRARY}
        ws2_32
        secur32
        crypt32
)

# MSVC settings
if (MSVC)
    target_compile_options(3DP_Driver_Core PRIVATE /W4)
    target_compile_options(3dp_driver PRIVATE /W4)
    target_compile_definitions(3DP_Driver_Core PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(3dp_driver PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()