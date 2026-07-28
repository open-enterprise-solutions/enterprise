################################################################################
# OES Enterprise вЂ” Unit Tests
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
    bench_runtime.cpp
    test_connectionPool.cpp
    test_session.cpp
    test_module_manager_split.cpp
    # test_pagingRamTable.cpp  # API-rotted (ibValueModelTableBase renamed) вЂ” excluded until updated
    test_blobCompression.cpp
    test_firebirdLease.cpp
    test_logger.cpp
    test_dbTaxonomy.cpp
    test_metadataSerialize.cpp
    test_metadataTree.cpp
    test_dataNode.cpp
    test_queryRenderer.cpp
    test_queryLifecycle.cpp
    test_queryBuilderShowcase.cpp
    test_queryTotals.cpp
    test_queryL2.cpp
    test_queryComposer.cpp
    test_queryParity.cpp
    test_queryJoinParity.cpp
    test_queryL3Vocab.cpp
    test_queryL4Lexer.cpp
    test_queryL4Parser.cpp
    test_queryRewrite.cpp
    test_lambdaRecorder.cpp
    test_queryValueMembers.cpp
    test_columnCodec.cpp
    test_queryPredicate.cpp
    test_queryColumnExpr.cpp
    test_linqMethod.cpp
    test_queryLinqExec.cpp
    test_queryComputedDotWalk.cpp
    test_queryComputedServer.cpp
    test_typeDescription.cpp
    test_dialectDictionary.cpp
    test_materializationDialect.cpp
    test_databaseMaterializeBuilder.cpp
    test_periodFold.cpp
    test_totalsViewParity.cpp
    test_uniqueKey.cpp
    test_valueArray.cpp
    test_valueContainer.cpp
    test_valueStructure.cpp
    test_valueGeometry.cpp
    test_metaDescription.cpp
    test_rawDBColumn.cpp
    test_rowValues.cpp
    test_readerWriterMemory.cpp
    test_passwordHash.cpp
    test_md5.cpp
    test_sha256.cpp
    test_stringUtils.cpp
    # test_guid.cpp  # API-rotted (ibGuidImpl::isValid/str/bytes gone) вЂ” excluded until updated
    test_dbTransaction.cpp
    test_propertyObject.cpp
    test_sourceExplorer.cpp
    test_sourceDescription.cpp
    test_tabularHop.cpp
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
# Lightweight string-layer tests вЂ” ibString/fstring only.
#
# fstring.h + fstringPool.h are header-only and depend on nothing but wxBase,
# so this target does NOT link (or recompile) backend.dll. Use it for fast
# iteration on the string type вЂ” it builds in seconds even after touching core
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
# Standalone РРўРћР“Р-tree test вЂ” backend + gtest only, no other test TUs (so it
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

# ---------------------------------------------------------------------------
# Standalone DB/RAM parity test вЂ” backend + gtest only (isolated, like the
# РРўРћР“Р-tree target above). Real in-memory SQLite vs the RAM filter core:
#   cmake --build build --target oes_query_parity_test --config Debug
#   ./bin/oes_query_parity_test
#   ./bin/oes_query_parity_test --gtest_also_run_disabled_tests   # show the gap
# ---------------------------------------------------------------------------
add_executable(oes_query_parity_test test_queryParity.cpp)

target_link_libraries(oes_query_parity_test
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_query_parity_test PRIVATE OES_TESTING)

target_include_directories(oes_query_parity_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_query_parity_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

gtest_discover_tests(oes_query_parity_test)

# ---------------------------------------------------------------------------
# Standalone JOIN parity test вЂ” RAM join core vs a live in-memory SQLite JOIN
# (isolated, like the parity target above). No appData / pool:
#   cmake --build build --target oes_query_join_parity_test --config Debug
#   ./bin/oes_query_join_parity_test
# ---------------------------------------------------------------------------
add_executable(oes_query_join_parity_test test_queryJoinParity.cpp)

target_link_libraries(oes_query_join_parity_test
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_query_join_parity_test PRIVATE OES_TESTING)

target_include_directories(oes_query_join_parity_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_query_join_parity_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

gtest_discover_tests(oes_query_join_parity_test)

# ---------------------------------------------------------------------------
# Temp-DB round-trip on the embedded SQLite driver (INTEGRATION scope). Brings
# up the global appData env + a SQLite-only pool (the temp manager pins a
# holder's connection), so it is a STANDALONE target вЂ” kept out of the main
# oes_tests list so its appData bring-up can't affect that run. Skips if the
# headless env can't come up.
#   cmake --build build --target oes_temp_db_sqlite_test --config Debug
#   ./bin/oes_temp_db_sqlite_test
# ---------------------------------------------------------------------------
add_executable(oes_temp_db_sqlite_test test_tempDbSqlite.cpp)

target_link_libraries(oes_temp_db_sqlite_test
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_temp_db_sqlite_test PRIVATE OES_TESTING)

target_include_directories(oes_temp_db_sqlite_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_temp_db_sqlite_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

gtest_discover_tests(oes_temp_db_sqlite_test)

# ---------------------------------------------------------------------------
# Frontend GUI harness вЂ” STEP 1. Links the DESKTOP frontend.dll (real wx
# widgets: ibVisualHost : wxScrolledCanvas) + backend into a gtest target and
# proves a live GUI wxApp + a runtime-mode appData env come up together in the
# test process вЂ” the seam every later form/control test stands on.
#
# Desktop controls are wxWindow subclasses, so this needs a live GUI toolkit (a
# display); the harness GTEST_SKIPs on a headless box. Kept as its OWN target
# (like oes_temp_db_sqlite_test) so its wxApp / appData bring-up can't perturb
# the main oes_tests run, and so its heavy wx link stays off the fast suites.
#   cmake --build build --target oes_frontend_runtime_test --config Debug
#   ./bin/oes_frontend_runtime_test
# ---------------------------------------------------------------------------
add_executable(oes_frontend_runtime_test
    test_frontendRuntime.cpp
    test_frontendForm.cpp
    test_frontendDocView.cpp
    test_frontendVisualHost.cpp
    test_frontendControls.cpp
)

target_link_libraries(oes_frontend_runtime_test
    PRIVATE
        backend
        frontend
        wx::core wx::aui wx::html wx::propgrid wx::adv
        wx::stc  wx::xrc wx::qa  wx::base wx::net wx::xml
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_frontend_runtime_test PRIVATE OES_TESTING)

target_include_directories(oes_frontend_runtime_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
    "${CMAKE_SOURCE_DIR}/src/engine/frontend"
)

set_target_properties(oes_frontend_runtime_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

gtest_discover_tests(oes_frontend_runtime_test)
