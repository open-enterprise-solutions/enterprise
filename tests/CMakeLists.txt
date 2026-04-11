################################################################################
# OES Enterprise — Unit Tests
#
# Uses Google Test fetched via FetchContent.
# Build with: cmake -B build -DBUILD_TESTING=ON && cmake --build build
################################################################################

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

# Prevent GoogleTest from overriding our compiler/linker settings (Windows)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Do not install GTest alongside the project
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
add_executable(oes_tests
    test_clsid.cpp
    test_value.cpp
)

target_link_libraries(oes_tests
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(oes_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

# On non-Windows the backend is a .dylib/.so; ensure the test can find it
# at runtime by setting the RPATH to the build output directory.
set_target_properties(oes_tests PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

# ---------------------------------------------------------------------------
# Register with CTest
# ---------------------------------------------------------------------------
include(GoogleTest)
gtest_discover_tests(oes_tests)
