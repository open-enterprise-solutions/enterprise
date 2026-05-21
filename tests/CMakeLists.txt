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
# OpenSSL for the triple-review integration test's direct HTTPS probe.
# cpp-httplib needs CPPHTTPLIB_OPENSSL_SUPPORT defined to enable TLS; the
# test's call site does this with #define before #include "httplib.h",
# but we still need to link OpenSSL::SSL + OpenSSL::Crypto. Without
# OpenSSL on this machine the integration test is excluded from the
# sources list entirely so it can't drag <openssl/*> into a build that
# has nothing to satisfy it.
# ---------------------------------------------------------------------------
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "tests: OpenSSL found — triple-review HTTPS probe enabled")
else()
    message(STATUS "tests: OpenSSL not found — skipping test_tripleReviewIntegration.cpp")
endif()

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
set(OES_TESTS_SOURCES
    test_clsid.cpp
    test_value.cpp
    test_number.cpp
    test_byteCodeAOT.cpp
    test_compiler.cpp
    test_runtime.cpp
    test_connectionPool.cpp
    test_session.cpp
    # test_pagingRamTable.cpp  # disabled: ibValueTableRow abstract class regression
    test_pluginApi.cpp
    test_pluginWebPane.cpp
    test_pluginWebPaneHistory.cpp
    test_legacyShim.cpp
    test_pluginLoadIntegration.cpp
    # Designer <-> oes-mcp concurrency primitives (configLock, mutation
    # marker). Same-process gtest coverage; inter-process semantics are
    # exercised by tests/mcp-smoke.py.
    test_configLock.cpp
    # Functional-test runner — assertion builtins + fixture manager +
    # `@test` discovery. End-to-end run_tests envelope shape is covered
    # by mcp-smoke.py.
    test_testRunner.cpp
    # Form-layout authoring (MCP form_layout_read / form_layout_set).
    # Serializer is DEFERRED (see formLayoutBlob.hpp); these tests
    # pin the deferral contract + validator-level checks that run
    # today against agent-supplied DTOs.
    test_formLayoutBlob.cpp
)
# SEC-P2-3: only pull the triple-review integration test in when OpenSSL is
# available — the source unconditionally includes <openssl/*> via httplib.
if(OpenSSL_FOUND)
    list(APPEND OES_TESTS_SOURCES test_tripleReviewIntegration.cpp)
    # AGENT-MODE: oes_agent integration suite — same OpenSSL gate.
    list(APPEND OES_TESTS_SOURCES test_oesAgentIntegration.cpp)
endif()
add_executable(oes_tests ${OES_TESTS_SOURCES})

target_link_libraries(oes_tests
    PRIVATE
        backend
        frontend
        wx::core
        wx::base
        wx::webview
        GTest::gtest
        GTest::gtest_main
)

if(OpenSSL_FOUND)
    target_link_libraries(oes_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    if(APPLE)
        # cpp-httplib + OpenSSL pull in SecTrust* via the keychain trust-
        # anchor support — link the system frameworks they live in.
        target_link_libraries(oes_tests PRIVATE
            "-framework CoreFoundation"
            "-framework Security")
    endif()
endif()

target_include_directories(oes_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
    "${CMAKE_SOURCE_DIR}/src/engine/frontend"
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
