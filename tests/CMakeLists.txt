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
    test_ctorRegistry.cpp
    test_exception.cpp
    test_value.cpp
    test_valueFactoryInit.cpp
    test_string.cpp
    bench_string.cpp
    test_number.cpp
    test_fdatetime.cpp
    test_driverDates.cpp
    test_formatString.cpp
    test_blockSyntaxLINQ.cpp
    test_byteCodeAOT.cpp
    test_byteCodeFormat.cpp
    test_compiler.cpp
    test_lexerComments.cpp
    test_runtime.cpp
    test_compileTree.cpp
    test_compilerContract.cpp
    test_scriptCorpus.cpp
    test_helpCorpus.cpp
    bench_runtime.cpp
    test_connectionPool.cpp
    test_fieldCipher.cpp
    test_session.cpp
    test_jobManager.cpp
    test_jobSchedule.cpp
    test_module_manager_split.cpp
    # test_pagingRamTable.cpp  # API-rotted (ibValueModelTableBase renamed) — excluded until updated
    test_blobCompression.cpp
    test_firebirdLease.cpp
    test_firebirdBatchInsert.cpp
    test_firebirdDates.cpp
    test_logger.cpp
    test_dbTaxonomy.cpp
    test_gitService.cpp
    test_metadataSerialize.cpp
    test_metadataPasteIdentity.cpp
    test_propertySerialized.cpp
    test_metadataTree.cpp
    test_commonAttribute.cpp
    test_roleComposition.cpp
    test_rolePolicyFold.cpp
    test_dataNode.cpp
    test_dataMover.cpp
    test_pointInTime.cpp
    test_jsonProvider.cpp
    test_valueJson.cpp
    test_queryRenderer.cpp
    test_queryLifecycle.cpp
    test_queryBuilderShowcase.cpp
    test_queryTotals.cpp
    test_queryL2.cpp
    test_queryComposer.cpp
    test_compositionSettings.cpp
    test_rpcMessage.cpp
    test_mcpToolContract.cpp
    test_spreadsheetCompose.cpp
    test_spreadsheetDocument.cpp
    test_spreadsheetDescriptionNode.cpp
    bench_spreadsheet.cpp
    test_queryAccessPolicy.cpp
    test_queryParity.cpp
    test_queryJoinParity.cpp
    test_queryL3Vocab.cpp
    test_queryL4Lexer.cpp
    test_queryL4Parser.cpp
    test_queryConstructor.cpp
    test_queryNaming.cpp
    test_queryRoundTrip.cpp
    test_jsonRoundTrip.cpp
    test_queryRewrite.cpp
    test_lambdaRecorder.cpp
    test_queryValueMembers.cpp
    test_queryUnload.cpp
    test_columnCodec.cpp
    test_queryPredicate.cpp
    test_queryColumnExpr.cpp
    test_LINQMethod.cpp
    test_methodArity.cpp
    test_queryLINQExec.cpp
    test_queryComputedDotWalk.cpp
    test_queryComputedServer.cpp
    test_scriptComplete.cpp
    test_typeDescription.cpp
    test_dialectDictionary.cpp
    test_materializationDialect.cpp
    test_databaseMaterializeBuilder.cpp
    test_periodFold.cpp
    test_registerReadRules.cpp
    test_totalsViewParity.cpp
    test_totalsNumericParity.cpp
    test_totalsKeyHash.cpp
    test_totalsFold.cpp
    test_accountingTotals.cpp
    test_sequenceBorder.cpp
    test_calcDisplacement.cpp
    test_calcRecalculation.cpp
    test_calcSchedule.cpp
    test_calcViews.cpp
    test_materializeCutRead.cpp
    test_accumulationRegisterSurface.cpp
    test_uniqueKey.cpp
    test_valueArray.cpp
    test_valueTableIndex.cpp
    test_valueTableWalk.cpp
    test_valueTableVerbs.cpp
    test_typeQualifiers.cpp
    test_valueContainer.cpp
    test_valueStructure.cpp
    test_valueGeometry.cpp
    test_valueTextFile.cpp
    test_valueHttp.cpp
    test_valueHttpSecure.cpp
    test_metaDescription.cpp
    test_backendColumnRawDB.cpp
    test_rowValues.cpp
    test_queryRamTable.cpp
    test_physicalFieldName.cpp
    test_databaseStringConverter.cpp
    test_readerWriterMemory.cpp
    test_passwordHash.cpp
    test_programFolder.cpp
    test_md5.cpp
    test_sha256.cpp
    test_stringUtils.cpp
    # test_guid.cpp  # API-rotted (ibGuidImpl::isValid/str/bytes gone) — excluded until updated
    test_dbTransaction.cpp
    test_propertyObject.cpp
    test_propertyDefaults.cpp
    test_sourceExplorer.cpp
    test_sourceDescription.cpp
    test_tabularHop.cpp
    test_sourceHopChain.cpp
    test_modelFetchDoor.cpp
    test_workerPoolDrop.cpp
    test_workerPoolFiber.cpp
    test_fiberTsan.cpp
    test_fiberAwait.cpp
    test_typeFamilyAdjust.cpp
    test_eventHandler.cpp
    test_functionalOption.cpp
    test_metadataMove.cpp
    test_sectionInterfaceItems.cpp
    test_socketLock.cpp
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

# ⭐⭐ ONE DISCOVERY TIMEOUT FOR EVERY SUITE, and it is not 5 seconds.
#
# `gtest_discover_tests` runs the freshly linked executable to list its tests, and CMake's default
# allows it 5 seconds. On 2026-08-26 that budget ran out for `oes_string_tests` on a Windows runner
# and the BUILD failed — empty output, "Process terminated due to timeout" — on a commit that had
# touched neither the tests nor anything they link.
#
# ⚠ THE FAILURE LOOKS LIKE A BROKEN TEST AND IS NOT ONE. Nothing ran; the list was being asked for.
#
# The five seconds are a budget for the MACHINE, not for the program: listing that same suite takes
# 0.02 s locally, and it is a runner building 1550 targets — with a freshly written binary being
# scanned before its first run — that eats the rest. The suites that link `backend` start heavier
# still, journal and all. So the number has to cover the worst machine, not the fastest program,
# and it belongs here, at the one place every suite is registered through, rather than repeated at
# nine call sites — where the tenth would be written without it.
function(oes_discover_tests target)
    gtest_discover_tests(${target} DISCOVERY_TIMEOUT 120)
endfunction()

oes_discover_tests(oes_tests)

# ⭐⭐ THE SAME DATES UNDER THREE CLOCKS. A date is a wall-clock reading with no zone in it (fdatetime.h),
# and that is a claim about EVERY machine - which one run, on whichever clock the runner happens to
# stand on, does not test. The C runtime reads TZ in its POSIX spelling on every platform (on Windows
# too, and only that spelling: a name like Europe/Kyiv is read as UTC with the American summer rule,
# and the rule written after a summer name is read on Linux and macOS but ignored on Windows, which
# applies the American one), so the date suites run again under UTC, two hours east of it with a
# summer name, and one hour east with one - three clocks that disagree about every reading and about
# which hour of which March morning does not exist. The hour itself is checked by a test that sets a
# clock of its own (DriverDates.TheSkippedHourIsHeldEvenWhereTheMachineSkipsIt).
set(oes_date_suites
    "ValueTest.*Date*:ValueTest.TheEmptyDate*:ValueTest.AText*:DateTime.*:DriverDates.*:DataNode.ADate*:DataNode.AFormat*:DataMover.*:JobSchedule.Buffer_ReadsAVersion*:PointInTime.*:*ScriptCorpusRun*date_reading*")
foreach(clock IN ITEMS "utc;UTC0" "east;EET-2EEST,M3.5.0/3,M10.5.0/4" "west;CET-1CEST,M3.5.0/2,M10.5.0/3")
    list(GET clock 0 zone_name)
    list(GET clock 1 zone_rule)
    add_test(NAME oes_dates_under_${zone_name} COMMAND oes_tests "--gtest_filter=${oes_date_suites}")
    set_tests_properties(oes_dates_under_${zone_name} PROPERTIES ENVIRONMENT "TZ=${zone_rule}")
endforeach()

# ⭐ A BASE WRITTEN UNDER ONE CLOCK AND READ UNDER ANOTHER - two processes, the writer under UTC and
# the reader two hours east: the empty date written by the one must be the empty date to the other,
# and every other reading must come back as it went in. This is the measurement of 2026-09-26 that
# found the old model out, kept as a test.
set(oes_cross_zone_base "${CMAKE_BINARY_DIR}/cross-zone-base.sqlite")
add_test(NAME oes_cross_zone_write COMMAND oes_tests --gtest_filter=CrossZoneBase.*)
set_tests_properties(oes_cross_zone_write PROPERTIES
    ENVIRONMENT "TZ=UTC0;OES_CROSS_ZONE_BASE=${oes_cross_zone_base};OES_CROSS_ZONE_STEP=write"
    FIXTURES_SETUP oes_cross_zone_base)
add_test(NAME oes_cross_zone_read COMMAND oes_tests --gtest_filter=CrossZoneBase.*)
set_tests_properties(oes_cross_zone_read PROPERTIES
    ENVIRONMENT "TZ=EET-2EEST,M3.5.0/3,M10.5.0/4;OES_CROSS_ZONE_BASE=${oes_cross_zone_base};OES_CROSS_ZONE_STEP=read"
    FIXTURES_REQUIRED oes_cross_zone_base)

# ---------------------------------------------------------------------------
# Lightweight string-layer tests — ibString/fstring only.
#
# fstring.h is the facade and fstring.cpp the module behind it; both depend on
# nothing but wxBase, so this target compiles the module IN (as if it were
# core, CORE_EXPORTS) and does NOT link (or recompile) core.dll. Use it
# for fast iteration on the string type — it builds in seconds even after
# touching core backend headers:
#
#   cmake --build build --target oes_string_tests
#   ./bin/oes_string_tests                                    # functional tests
#   ./bin/oes_string_tests --gtest_also_run_disabled_tests \
#       --gtest_filter=*IbStringBench*                        # benchmark
# ---------------------------------------------------------------------------
add_executable(oes_string_tests
    test_string.cpp
    bench_string.cpp
    "${CMAKE_SOURCE_DIR}/src/engine/core/fstring.cpp"
)
target_compile_definitions(oes_string_tests PRIVATE CORE_EXPORTS)

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

oes_discover_tests(oes_string_tests)

# ---------------------------------------------------------------------------
# Standalone ИТОГИ-tree test — backend + gtest only, no other test TUs (so it
# links even when an optional driver, e.g. ODBC, is OFF and a sibling test TU
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

oes_discover_tests(oes_query_totals_test)

# ---------------------------------------------------------------------------
# Standalone DB/RAM parity test — backend + gtest only (isolated, like the
# ИТОГИ-tree target above). Real in-memory SQLite vs the RAM filter core:
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

oes_discover_tests(oes_query_parity_test)

# ---------------------------------------------------------------------------
# Standalone JOIN parity test — RAM join core vs a live in-memory SQLite JOIN
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

oes_discover_tests(oes_query_join_parity_test)

# ---------------------------------------------------------------------------
# Temp-DB round-trip on the embedded SQLite driver (INTEGRATION scope). Brings
# up the global appData env + a SQLite-only pool (the temp manager pins a
# holder's connection), so it is a STANDALONE target — kept out of the main
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

oes_discover_tests(oes_temp_db_sqlite_test)

# ---------------------------------------------------------------------------
# Rented background runs (ibJobTenancy::Tenant) — integration scope.
#
# Needs the same bring-up as the temp-DB target (appData env + a SQLite pool)
# because a tenant mints a session, takes a pooled connection on the calling
# thread and gives it back when it dies — none of which exists without a live
# application. Kept as its OWN target for the same reason: the appData bring-up
# must not perturb the main oes_tests run.
#   cmake --build build --target oes_job_tenancy_test --config Debug
#   ./bin/oes_job_tenancy_test
# ---------------------------------------------------------------------------
add_executable(oes_job_tenancy_test test_jobTenancy.cpp)

target_link_libraries(oes_job_tenancy_test
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_job_tenancy_test PRIVATE OES_TESTING)

target_include_directories(oes_job_tenancy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_job_tenancy_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

oes_discover_tests(oes_job_tenancy_test)

# ---------------------------------------------------------------------------
# Several bases in one process (ibApplicationHost) — integration scope.
#
# Opens two bases in the test process, each with its own in-memory SQLite, and
# asks the rule that names "the current base" from bound and unbound threads.
# Its own target for the reason the tenancy test is: an appData bring-up must
# not perturb the main oes_tests run.
#   cmake --build build --target oes_multi_base_test --config Debug
#   ./bin/oes_multi_base_test
# ---------------------------------------------------------------------------
add_executable(oes_multi_base_test test_multiBase.cpp)

target_link_libraries(oes_multi_base_test
    PRIVATE
        backend
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_multi_base_test PRIVATE OES_TESTING)

target_include_directories(oes_multi_base_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_multi_base_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

oes_discover_tests(oes_multi_base_test)

# ---------------------------------------------------------------------------
# The client protocol's patch — made by the server's half (frmserver) and applied by the talking side's library
# (protocol: ibProtocolNode::Apply), in process and through the port's JSON. Its own target: it links the two
# libraries no other suite needs — and no GUI of the client's.
#   cmake --build build --target oes_protocol_test --config Debug
#   ./bin/oes_protocol_test
# ---------------------------------------------------------------------------
add_executable(oes_protocol_test test_clientFrameApply.cpp)

target_link_libraries(oes_protocol_test
    PRIVATE
        backend
        frmserver
        protocol
        wx::core
        wx::base
        GTest::gtest
        GTest::gtest_main
)

target_compile_definitions(oes_protocol_test PRIVATE OES_TESTING)

target_include_directories(oes_protocol_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/engine"
    "${CMAKE_SOURCE_DIR}/src/engine/backend"
)

set_target_properties(oes_protocol_test PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
)

oes_discover_tests(oes_protocol_test)

# ---------------------------------------------------------------------------
# Frontend GUI harness — STEP 1. Links the DESKTOP frontend.dll (real wx
# widgets: ibVisualHost : wxScrolledCanvas) + backend into a gtest target and
# proves a live GUI wxApp + a runtime-mode appData env come up together in the
# test process — the seam every later form/control test stands on.
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
    test_frontendQueryConstructor.cpp
    test_frontendTranslateConstructor.cpp
    test_frontendFormatConstructor.cpp
    test_frontendDebugIcons.cpp
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

oes_discover_tests(oes_frontend_runtime_test)

# ---------------------------------------------------------------------------
# PostgreSQL dialect — EXECUTED against a real server (integration scope).
#
# Everything else in the suite runs on SQLite, which pins semantics but cannot
# pin the half that depends on the server: numeric precision, timestamps, blob
# escaping, the dialect's own upsert. PostgreSQL is what production runs on, so
# that half was unchecked until this target.
#
# Skips itself when OES_PG_USER is unset, so it is a no-op locally and a real
# test in CI, where a postgres service container supplies the connection:
#
#   cmake --build build --target oes_pg_dialect_test --config Debug
#   OES_PG_USER=postgres OES_PG_PASSWORD=postgres ./bin/oes_pg_dialect_test
#
# Only built when the driver itself is compiled in — without OES_USE_POSTGRESQL
# the layer is absent and the target would not link.
# ---------------------------------------------------------------------------
if(OES_USE_POSTGRESQL)
    add_executable(oes_pg_dialect_test test_postgresDialect.cpp)

    target_link_libraries(oes_pg_dialect_test
        PRIVATE
            backend
            GTest::gtest
            GTest::gtest_main
    )

    target_compile_definitions(oes_pg_dialect_test PRIVATE OES_TESTING)

    target_include_directories(oes_pg_dialect_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/engine"
        "${CMAKE_SOURCE_DIR}/src/engine/backend"
    )

    set_target_properties(oes_pg_dialect_test PROPERTIES
        CXX_STANDARD          17
        CXX_STANDARD_REQUIRED ON
        BUILD_RPATH            "${CMAKE_BINARY_DIR}/bin"
        INSTALL_RPATH          "${CMAKE_BINARY_DIR}/bin"
    )

    oes_discover_tests(oes_pg_dialect_test)
endif()
