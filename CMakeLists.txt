cmake_minimum_required(VERSION 3.5)
project(ebusd)

set(CMAKE_CXX_COMPILER clang++-3.8)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -stdlib=libc++ -Wall")

include_directories(build)
include_directories(src/lib/utils)

add_subdirectory(src/lib/ebus)
add_subdirectory(src/lib/utils)
add_subdirectory(src/ebusd)