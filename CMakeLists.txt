# ┌──────────────────────────────────────────────────────────────────┐
# │  Project settings                                                │
# └──────────────────────────────────────────────────────────────────┘
cmake_minimum_required(VERSION 3.9)
project(RE)
set(CMAKE_CXX_STANDARD 17)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ┌──────────────────────────────────────────────────────────────────┐
# │  Many different programs                                         │
# └──────────────────────────────────────────────────────────────────┘

add_subdirectory(linux-wifisound)
add_subdirectory(linux-audiowire)
