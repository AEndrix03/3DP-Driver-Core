cmake_minimum_required(VERSION 3.20)
project(3DP_Driver_Core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(3DP_Driver_Core
    src/main.cpp
)