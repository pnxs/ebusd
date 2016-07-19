cmake_minimum_required(VERSION 3.5)
project(ebusd)

set(PACKAGE_NAME ${CMAKE_PROJECT_NAME})
set(PACKAGE_BUGREPORT "ebusd@ebusd.eu")
set(PACKAGE_STRING "${PACKAGE_NAME} 2.1")
set(SCAN_VERSION "201")
set(REVISION "dummy")
set(PACKAGE_VERSION "2.1")
set(PACKAGE_LOGFILE "${LOCALSTATEDIR}/log/${PACKAGE_NAME}.log")

#set(CXX clang++-3.8)
#set(CMAKE_CXX_COMPILER clang++-3.8)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -std=c++14 -stdlib=libc++ -Wall")
# config.h checks
include(ConfigureChecks.cmake)
configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_subdirectory(external/googletest)
enable_testing()
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(src/lib/utils)
include_directories(src/lib/ebus)

add_subdirectory(src/lib/ebus)
add_subdirectory(src/lib/utils)
add_subdirectory(src/ebusd)
add_subdirectory(src/tools)
add_subdirectory(src/lib/ebus/test)

set(TEST_SOURCES
        src/lib/utils/tests/TestNotify.cpp
        src/lib/utils/tests/TestQueue.cpp
        src/lib/ebus/tests/TestSymbolString.cpp
        )
add_executable(test_runner ${TEST_SOURCES})
target_link_libraries(test_runner ebus gtest gtest_main)

# TODO: create config.h from config.h.in
