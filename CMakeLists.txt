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
set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/zlib")
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_LIBRARIES zlibstatic)

add_subdirectory(external/zlib)

# Collect all source files (excluding test files)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        src/*.cpp
)
list(FILTER SOURCES EXCLUDE REGEX ".*main_.*test.*\\.cpp$")

# Collect all connector header files (header-only for now)
file(GLOB_RECURSE CONNECTOR_HEADERS CONFIGURE_DEPENDS
        include/connector/models/*.hpp
        include/connector/events/*.hpp
        include/connector/processors/*.hpp
)

# Main executable
add_executable(3DP_Driver_Core
        ${SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc
)

# Static library (without main.cpp)
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
        src/*.cpp
)
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*main.*\\.cpp$")

add_library(3dp_driver STATIC
        ${LIB_SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc
)

# Test executables
add_executable(3DP_EventSystem_Test
        src/main_eventbus_test.cpp
        ${LIB_SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc
)

# Connector Models Test (new)
add_executable(3DP_Connector_Models_Test
        src/main_connector_test.cpp
        ${LIB_SOURCES}
        external/serial/src/serial.cc
        external/serial/src/impl/win.cc
)

# Link libraries
target_link_libraries(3DP_Driver_Core PRIVATE zlibstatic)
target_link_libraries(3dp_driver PRIVATE zlibstatic)
target_link_libraries(3DP_EventSystem_Test PRIVATE zlibstatic)
target_link_libraries(3DP_Connector_Models_Test PRIVATE zlibstatic)

# Compiler warnings
if (MSVC)
    target_compile_options(3DP_Driver_Core PRIVATE /W4 /WX-)
    target_compile_options(3dp_driver PRIVATE /W4 /WX-)
    target_compile_options(3DP_EventSystem_Test PRIVATE /W4 /WX-)
    target_compile_options(3DP_Connector_Models_Test PRIVATE /W4 /WX-)
else ()
    target_compile_options(3DP_Driver_Core PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    target_compile_options(3dp_driver PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    target_compile_options(3DP_EventSystem_Test PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    target_compile_options(3DP_Connector_Models_Test PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif ()

# Debug/Release postfix
set_target_properties(3DP_Driver_Core 3dp_driver 3DP_EventSystem_Test 3DP_Connector_Models_Test
        PROPERTIES
        DEBUG_POSTFIX "_d"
        RELEASE_POSTFIX ""
)

# Custom target to show connector structure
add_custom_target(show_connector_structure
        COMMAND ${CMAKE_COMMAND} -E echo "=== Connector Structure ==="
        COMMAND ${CMAKE_COMMAND} -E echo "Models: ${CMAKE_SOURCE_DIR}/include/connector/models/"
        COMMAND ${CMAKE_COMMAND} -E echo "Events: ${CMAKE_SOURCE_DIR}/include/connector/events/"
        COMMAND ${CMAKE_COMMAND} -E echo "Processors: ${CMAKE_SOURCE_DIR}/include/connector/processors/"
        COMMAND find ${CMAKE_SOURCE_DIR}/include/connector/ -name "*.hpp" -type f | sort
)

# Installation
install(TARGETS 3DP_Driver_Core 3dp_driver
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING PATTERN "*.hpp")

# Print configuration summary
message(STATUS "3DP Driver Core Configuration:")
message(STATUS "  - C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  - Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  - Connector Models: ${CMAKE_SOURCE_DIR}/include/connector/models/")
message(STATUS "  - Connector Events: ${CMAKE_SOURCE_DIR}/include/connector/events/")
message(STATUS "  - Connector Processors: ${CMAKE_SOURCE_DIR}/include/connector/processors/")
message(STATUS "  - Total Header Files: ${CMAKE_SOURCE_DIR}/include/connector/")