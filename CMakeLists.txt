cmake_minimum_required(VERSION 3.24)
project(ecss LANGUAGES CXX)

option(ECSS_BUILD_STATIC "Build ecss as a static library (developer mode)" FALSE)

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

# The suite is for developing ecss, not for the projects that embed it. Defaulting to
# BUILD_TESTING made a consumer that merely add_subdirectory()s us fetch googletest and
# compile twelve more translation units -- each one instantiating the whole Registry --
# as part of its own build, which is minutes it never asked for. include(CTest) is inside
# the branch for the same reason: it defines BUILD_TESTING as a cache variable and adds
# Nightly/Continuous/Experimental targets to whichever project pulled us in.
option(ECSS_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})

if(ECSS_BUILD_TESTS)
    message(STATUS "ecss: with unit tests")
    include(CTest)
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

    # googletest builds itself with warnings as errors (-WX under the MSVC-style drivers),
    # so every diagnostic a newer compiler learns turns into a build failure inside a
    # dependency we do not maintain: 1.17.0 trips -Wcharacter-conversion on the clang that
    # now ships with the Windows runner. Disabling that one warning by name would be worse,
    # since a clang that does not know it rejects the flag and -WX makes that fatal too.
    # Our own targets are untouched and still fail on their own warnings.
    foreach(gtest_target gtest gtest_main)
        if (TARGET ${gtest_target} AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(${gtest_target} PRIVATE -Wno-error)
        endif()
    endforeach()

    # CONFIGURE_DEPENDS so a new test file is picked up by the next build rather than
    # needing a manual reconfigure. The glob still takes whatever is in tests/, so do not
    # leave scratch .cpp files there -- they become part of the suite.
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    add_executable(ecss_tests ${TEST_SOURCES})
    target_link_libraries(ecss_tests PRIVATE ecss gtest gtest_main)

    # The suite stores std::string and move-only components on purpose -- exercising those
    # paths is its job -- so the warning about them has nothing to say here. Silencing it per
    # type is not an option either: several of those components are declared inside a TestBody,
    # and a local class cannot be named in an explicit specialization of ecss::AllowNonTrivial.
    target_compile_definitions(ecss_tests PRIVATE ECSS_NO_TRIVIALITY_WARNINGS)

    include(GoogleTest)
    gtest_discover_tests(ecss_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        # Per-test wall clock. It has to travel as a test property: gtest_discover_tests
        # has no TIMEOUT keyword of its own, so a bare one is swallowed as an unparsed
        # argument and every test runs under ctest's default instead.
        PROPERTIES TIMEOUT 150
    )

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(ecss_tests PRIVATE atomic)
    endif()
endif()
