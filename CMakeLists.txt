cmake_minimum_required(VERSION 3.5)
project(ebusd)

#set(CXX clang++-3.8)
#set(CMAKE_CXX_COMPILER clang++-3.8)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -std=c++14 -stdlib=libc++ -Wall")

add_subdirectory(external/googletest)
enable_testing()
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

include_directories(build)
include_directories(src/lib/utils)
include_directories(src/lib/ebus)

add_subdirectory(src/lib/ebus)
add_subdirectory(src/lib/utils)
add_subdirectory(src/ebusd)
add_subdirectory(src/tools)
add_subdirectory(src/lib/ebus/test)

set(TEST_SOURCES src/lib/utils/tests/TestNotify.cpp)
add_executable(test_runner ${TEST_SOURCES})
target_link_libraries(test_runner gtest gtest_main)

# TODO: create config.h from config.h.in
