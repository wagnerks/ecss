cmake_minimum_required(VERSION 3.24)
project(ecss LANGUAGES CXX)

add_library(ecss INTERFACE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

target_include_directories(ecss INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_libraries(ecss INTERFACE atomic)
endif()

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
    include(GoogleTest)
    gtest_discover_tests(ecss_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        TIMEOUT 150
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_link_libraries(ecss_tests PRIVATE atomic)
    endif()

endif()
