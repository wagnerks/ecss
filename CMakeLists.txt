cmake_minimum_required(VERSION 3.14)
project(ecss LANGUAGES CXX)


add_library(ecss INTERFACE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

target_include_directories(ecss INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(gtests EXCLUDE_FROM_ALL)

enable_testing()

file(GLOB TEST_SOURCES "tests/*.cpp")
add_executable(ecss_tests ${TEST_SOURCES})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

target_link_libraries(ecss_tests PRIVATE ecss gtest gtest_main)
target_include_directories(ecss_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME ecss_tests COMMAND ecss_tests)