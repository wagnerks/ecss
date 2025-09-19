cmake_minimum_required(VERSION 3.24)
project(ecss LANGUAGES CXX)

option(ECSS_BUILD_STATIC "Build ecss as a static library (developer mode)" TRUE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# IPO only matters if we actually compile sources
if(ECSS_BUILD_STATIC)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(ECSS_BUILD_STATIC)
    message(STATUS "ecss: building as STATIC library (developer mode)")
    # Collect headers to show in the IDE
    file(GLOB_RECURSE ECSS_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
    )

    # Ensure at least one TU exists
    set(ECSS_DUMMY_SRC "${CMAKE_CURRENT_BINARY_DIR}/ecss_dummy.cpp")
    if (NOT EXISTS "${ECSS_DUMMY_SRC}")
        file(WRITE "${ECSS_DUMMY_SRC}" "// Auto-generated dummy source to build the ecss static library.\n")
    endif()

    add_library(ecss STATIC
        ${ECSS_HEADERS}
        ${ECSS_DUMMY_SRC}
    )

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ECSS_HEADERS})
    target_include_directories(ecss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(ecss PUBLIC atomic)
    endif()
else()
    message(STATUS "ecss: building as INTERFACE (header-only) library")
    add_library(ecss INTERFACE)
    target_include_directories(ecss INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(ecss INTERFACE atomic)
    endif()
endif()

include(CTest) # adds option BUILD_TESTING (ON by default)
option(ECSS_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})

if(ECSS_BUILD_TESTS)
    include(FetchContent)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        UPDATE_DISCONNECTED TRUE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK  OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
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
