cmake_minimum_required(VERSION 3.20)
project(3DP_Driver_Core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)
include_directories(external/serial/include)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/zlib")
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_LIBRARIES zlibstatic)

add_subdirectory(external/zlib)

add_subdirectory(external/ixwebsocket)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(3DP_Driver_Core
        ${SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc)

add_library(3dp_driver STATIC
        ${SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc)

target_include_directories(3DP_Driver_Core PRIVATE external/nlohmann/json/single_include)
target_include_directories(3dp_driver PRIVATE external/nlohmann/json/single_include)

target_link_libraries(3DP_Driver_Core PRIVATE ixwebsocket zlibstatic)
target_link_libraries(3dp_driver PRIVATE ixwebsocket zlibstatic)
