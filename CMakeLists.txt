cmake_minimum_required(VERSION 3.20)
project(3DP_Driver_Core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress policy warnings
cmake_policy(SET CMP0167 NEW)

# Dependencies (managed by vcpkg.json)
find_package(RdKafka CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(ZLIB REQUIRED)
find_package(CURL CONFIG REQUIRED)

# Executable
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE include)

target_link_libraries(${PROJECT_NAME} PRIVATE
        RdKafka::rdkafka++
        nlohmann_json::nlohmann_json
        Boost::system
        ZLIB::ZLIB
        CURL::libcurl
)

# Platform-specific libraries
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 setupapi)
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif ()

# Compiler settings
if (MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif ()