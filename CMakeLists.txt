cmake_minimum_required(VERSION 3.24)
project(ecss LANGUAGES CXX)

add_library(ecss INTERFACE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

target_include_directories(ecss INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

include(CTest)                     # adds option BUILD_TESTING (ON by default)
option(ECSS_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})

if(ECSS_BUILD_TESTS)
  include(FetchContent)

  FetchContent_Declare(
    googletest
    URL        https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    UPDATE_DISCONNECTED TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK  OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)

  file(GLOB TEST_SOURCES "tests/*.cpp")
  add_executable(ecss_tests ${TEST_SOURCES})
  target_link_libraries(ecss_tests PRIVATE ecss gtest gtest_main)
  add_test(NAME ecss_tests COMMAND ecss_tests)
endif()