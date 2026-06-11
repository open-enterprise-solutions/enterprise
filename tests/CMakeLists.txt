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
    test_string.cpp
    bench_string.cpp
    test_number.cpp
    test_byteCodeAOT.cpp
    test_compiler.cpp
    test_runtime.cpp
    test_connectionPool.cpp
    test_session.cpp
    test_module_manager_split.cpp
    test_pagingRamTable.cpp
    test_blobCompression.cpp
    test_firebirdLease.cpp
    test_logger.cpp
    test_dbTaxonomy.cpp
    test_metadataSerialize.cpp
    test_queryRenderer.cpp
    test_queryLifecycle.cpp
    test_queryBuilderShowcase.cpp
    test_queryTotals.cpp
    test_queryL2.cpp
    test_queryComposer.cpp
    test_queryL3Vocab.cpp
    test_queryL4Lexer.cpp
    test_queryL4Parser.cpp
    test_queryRewrite.cpp
    test_lambdaRecorder.cpp
    test_queryValueMembers.cpp
)

target_link_libraries(oes_tests
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

# Tests need to construct app-owned subsystems directly (no full appData
# bring-up). OES_TESTING opens ib::AppDataCtorToken's default ctor; see
# backend/appDataCtorToken.h.
target_compile_definitions(oes_tests PRIVATE OES_TESTING)

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

# ---------------------------------------------------------------------------
# Lightweight string-layer tests — ibString/fstring only.
#
# fstring.h + fstringPool.h are header-only and depend on nothing but wxBase,
# so this target does NOT link (or recompile) backend.dll. Use it for fast
# iteration on the string type — it builds in seconds even after touching core
# backend headers:
#
#   cmake --build build --target oes_string_tests
#   ./bin/oes_string_tests                                    # functional tests
#   ./bin/oes_string_tests --gtest_also_run_disabled_tests \
#       --gtest_filter=*IbStringBench*                        # benchmark
# ---------------------------------------------------------------------------
add_executable(oes_string_tests
    test_string.cpp
    bench_string.cpp
)

target_link_libraries(oes_string_tests
    PRIVATE
        wx::base
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(oes_string_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/engine"
)

set_target_properties(oes_string_tests PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

gtest_discover_tests(oes_string_tests)

# ---------------------------------------------------------------------------
# Standalone ИТОГИ-tree test — backend + gtest only, no other test TUs (so it
# links even when an optional driver, e.g. MySQL, is OFF and a sibling test TU
# references it). Pure RAM fold, no DB:
#   cmake --build build --target oes_query_totals_test --config Debug
#   ./bin/oes_query_totals_test
# ---------------------------------------------------------------------------
add_executable(oes_query_totals_test test_queryTotals.cpp)

target_link_libraries(oes_query_totals_test
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_query_totals_test PRIVATE OES_TESTING)

target_include_directories(oes_query_totals_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_query_totals_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

gtest_discover_tests(oes_query_totals_test)
