# ┌──────────────────────────────────────────────────────────────────┐
#   Project settings
# └──────────────────────────────────────────────────────────────────┘
cmake_minimum_required(VERSION 3.9)
project("StreamSome - the streaming library" VERSION 1.0.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/utils/cmake")
include(legacy)


set(Boost_USE_STATIC_LIBS        ON)  # only find static libs
set(Boost_USE_DEBUG_LIBS         OFF) # ignore debug libs and
set(Boost_USE_RELEASE_LIBS       ON)  # only find release libs
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     OFF)
find_package(Boost REQUIRED)


add_subdirectory(streamsome)
add_subdirectory(audio)
