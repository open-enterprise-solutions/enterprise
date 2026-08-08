# =============================================================================
# OES Enterprise — Root CMake Build System
# =============================================================================
#
# Cross-platform build for the Open Enterprise Solutions platform.
# Windows also supports the MSBuild solution (enterprise.sln).
#
# Quick start:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#
# Optional database drivers (all OFF by default except SQLite which is embedded):
#   -DOES_USE_FIREBIRD=ON
#   -DOES_USE_POSTGRESQL=ON
#   -DOES_USE_MYSQL=ON
#   -DOES_USE_ODBC=ON
#
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(enterprise
    VERSION 1.0
    DESCRIPTION "Open Enterprise Solutions — cross-platform low-code enterprise application platform"
    LANGUAGES CXX C
)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Export compile_commands.json for IDE / clangd support
# ---------------------------------------------------------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Output directories — mirrors the MSBuild layout from Common.props
# ---------------------------------------------------------------------------

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Per-config output directories (multi-config generators like Xcode, VS)
foreach(CONFIG_TYPE IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${CONFIG_TYPE}" CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${CONFIG_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${CONFIG_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${CONFIG_TYPE}")
endforeach()

# ---------------------------------------------------------------------------
# Options — optional database drivers
# ---------------------------------------------------------------------------

option(BUILD_TESTING       "Build unit tests (Google Test)"     OFF)

option(OES_USE_FIREBIRD    "Enable Firebird database driver"    OFF)
option(OES_USE_POSTGRESQL  "Enable PostgreSQL database driver"  OFF)
option(OES_USE_MYSQL       "Enable MySQL database driver"       OFF)
option(OES_USE_ODBC        "Enable ODBC database driver"        OFF)

# Phase-6 local FB server — required for shared-file (leader-election)
# deployments so the leader process can expose a TCP listener that
# followers connect to. ON by default since leader-mode is the primary
# multi-user story for the FB driver. The `firebird.exe` binary is not
# vendored in the repo (~5 MB, version-specific); operator drops FB 5's
# `bin/firebird.exe` into `src/engine/backend/databaseLayer/firebird/
# engine/dll/` and it gets copied into `_fb/` at build time. Build still
# works without the binary, but leader-mode will degrade to embedded-
# only single-user. Turn OFF to exclude the spawn code entirely (saves
# ~20 KB, drops winsock dependency in the TU).
option(OES_FB_LOCALSERVER  "Enable Firebird out-of-process local server (Phase 6)" ON)

# SQLite is always available (embedded sources in the project).

# ---------------------------------------------------------------------------
# Link-time code generation (Release)
# ---------------------------------------------------------------------------
#
# The interpreter's hot loop crosses translation units on nearly every opcode:
# ibProcUnit::Execute (procUnit.cpp) calls ibNumber::Compare / operator+= /
# operator*= (fnumber.cpp) for the arithmetic and ibByteCode::Find* /
# ibValue::Find* for name resolution. Their fast paths are a handful of
# instructions each — ibNumber::Compare's is TryImmInts plus one int64
# three-way compare — but without whole-program view the compiler cannot see
# any of those bodies, so every one costs a real call plus the register spill
# a call forces.
#
# Measured 2026-08-08, Release x64 MSVC 19.42, min-of-4, quiet machine
# (tests/bench_runtime.cpp). Ranges did not overlap on any of these four:
#   arith loop      116.2 -> 95.0 ns   (-18%)
#   method resolve  297.8 -> 251.1 ns  (-16%)
#   LINQ            1029  -> 936 ns    (-9%)
#   recursion       364.1 -> 345.2 ns  (-5%)
# host->script and string concat landed inside their own run-to-run spread and
# are NOT claimed. Full oes_tests suite stayed green (1102 passed / 0 failed),
# which is the check that matters here: LTCG reorders and inlines across the
# module boundary, so the destruction order of ibApplicationData, the inline
# registry variables and the pointer-to-member in ibMemberTable are all
# exercised under it.
#
# ON for MSVC only, because that is where the numbers above were taken. GCC and
# Clang would use -flto, which is a different implementation with a different
# link-time cost, and the CI budget for the Linux / macOS jobs has not been
# measured against it — turn the option on there once it has been.
if(MSVC)
    option(OES_ENABLE_LTO "Link-time code generation for Release builds" ON)
else()
    option(OES_ENABLE_LTO "Link-time code generation for Release builds" OFF)
endif()

if(OES_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT OES_IPO_SUPPORTED OUTPUT OES_IPO_MESSAGE)
    if(OES_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        message(STATUS "LTO/LTCG ............ ON (Release)")
    else()
        message(STATUS "LTO/LTCG ............ unsupported: ${OES_IPO_MESSAGE}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
#
# The leak hunt on Windows leans on the MSVC debug CRT: an exit dump of every
# surviving block, plus the opt-in tracker in backend/diagnostics/leakTracker
# that names who allocated them. Neither exists here, and neither is worth
# reimplementing — LeakSanitizer already reports leaks with full stacks, and it
# rides in with AddressSanitizer.
#
#     cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOES_SANITIZE=address
#
# The procedure is the same on every platform (see
# docs/engineering-playbook/25-memory-leaks.md); only the instrument differs.
# Accepts anything the toolchain does: address, undefined, thread,
# address,undefined. OFF by default — ASan roughly doubles memory and costs
# ~2x runtime, which is not what an ordinary debug build should pay.
set(OES_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address or address,undefined")

if(OES_SANITIZE)
    if(MSVC)
        # MSVC ships ASan but not LSan — /fsanitize=address finds overflows and
        # use-after-free, never leaks. Leaks on Windows are the debug CRT's job,
        # so say so rather than let the flag imply cover it does not give.
        message(WARNING
            "OES_SANITIZE on MSVC: /fsanitize=address has no leak detector. "
            "Use a Debug build and the CRT dump (docs/engineering-playbook/25-memory-leaks.md).")
        add_compile_options(/fsanitize=${OES_SANITIZE})
    else()
        add_compile_options(-fsanitize=${OES_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${OES_SANITIZE})
    endif()
endif()

# ---------------------------------------------------------------------------
# Convenience paths
# ---------------------------------------------------------------------------

set(OES_SRC_DIR      "${CMAKE_SOURCE_DIR}/src")
set(OES_ENGINE_DIR   "${OES_SRC_DIR}/engine")
set(OES_3RDPARTY_DIR "${OES_SRC_DIR}/3rdparty")

# ---------------------------------------------------------------------------
# Platform detection and global compile definitions
# ---------------------------------------------------------------------------

# Common to all platforms
add_compile_definitions(_UNICODE UNICODE)

if(WIN32)
    add_compile_definitions(
        WIN32
        __WXMSW__
        WXUSINGDLL
        _CRT_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_DEPRECATE=1
        _CRT_NON_CONFORMING_SWPRINTFS=1
        _SCL_SECURE_NO_WARNINGS=1
    )
elseif(APPLE)
    add_compile_definitions(
        __WXOSX__
    )
elseif(UNIX)
    add_compile_definitions(
        __WXGTK__
    )
endif()

# Configuration-specific defines
add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:DEBUG>
    $<$<CONFIG:MinSizeRel>:NDEBUG>
)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

if(MSVC)
    # Match the MSBuild settings: fast floating point, RTTI enabled, W1, and
    # /MP multi-processor compilation (the .vcxproj files already set
    # <MultiProcessorCompilation>true</MultiProcessorCompilation>). Without /MP
    # a single big target like backend compiles its hundreds of .cpp serially
    # even under `cmake --build --parallel` (which only parallelises across
    # targets) — /MP fans them across cores.
    # /W4 matches the Debug configurations of the .vcxproj files. At /W1 this build saw
    # less than the shipping one, which made it useless as the control against GCC: a
    # warning the developer's own Visual Studio prints would have passed CI unnoticed.
    add_compile_options(/fp:fast /GR /W4 /MP /bigobj)
    # Same suppressions as Common.props, and for the same reason: they mirror what the
    # GCC build turns off, so a warning seen on one toolchain is seen on the other.
    add_compile_options(/wd4251 /wd4275 /wd4458 /wd4100 /wd4127 /wd4018 /wd4389 /wd4245)
else()
    # GCC / Clang
    add_compile_options(-Wall -Wno-unused-parameter -Wno-sign-compare)

    # Silence the two classes that BURY everything else. The first GCC build of the tree
    # produced ~48,000 warnings, of which ~44,000 were unknown-pragma and ~2,800 attributes
    # — and buried in them were three -Wreturn-local-addr lines naming the exact cause of 33
    # crashing tests. A warning nobody can find is a warning nobody acts on.
    #
    #   unknown-pragmas — `#pragma region` / `endregion`, an editor folding hint MSVC
    #                     understands and GCC does not. Nothing to fix in the code.
    #   attributes      — BACKEND_API (dllexport/dllimport) written on elaborated type
    #                     specifiers, e.g. `class BACKEND_API ibValue` inside a typedef,
    #                     where the attribute is simply ignored off-Windows.
    #
    # Deliberately NOT silenced: -Wreorder and -Wswitch. Both were cleaned at the SOURCE
    # on 2026-08-03 rather than switched off — the 83 -Wswitch lines became an explicit
    # `default:` (or an explicit TYPE_LAST case where the switch really is exhaustive, so
    # a new value still warns), and the -Wreorder ones left in our code were the three
    # ibCompileCode ctors, whose lists now follow declaration order. What remained came
    # from the vendored forks, which compile quiet per the rule above.
    add_compile_options(-Wno-unknown-pragmas -Wno-attributes)

    # Clang adds a third one of the same kind, and it is the loudest yet: the first
    # macOS build produced 67,318 warning lines, of which 66,563 — 99% — were
    # -Winconsistent-missing-override. GCC has no equivalent in -Wall, so this is
    # new information, but not information anyone can act on in that volume: the
    # property hierarchy marks roughly half its overrides, the headers reach most
    # of the backend, and every unmarked one reprints in every TU that includes
    # them. It buried the four classes that DID matter (an undefined inline, ten
    # NULL-vs-non-pointer comparisons, 649 hidden virtuals, and the one real
    # error). Silenced to keep the next real finding visible — marking the
    # overrides is a separate pass over ~100 declarations in 27 headers, and is
    # worth doing: `override` is what catches a base signature drifting.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -Wno-inconsistent-missing-override
            -Wno-deprecated-declarations
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Global include directories — mirrors AdditionalIncludeDirectories from
# Common.props: .;$(SolutionDir);$(oesSrcDir);$(oesEngine)
# ---------------------------------------------------------------------------

include_directories(
    "${CMAKE_SOURCE_DIR}"
    "${OES_SRC_DIR}"
    "${OES_ENGINE_DIR}"
)

# ---------------------------------------------------------------------------
# wxWidgets — build from submodule at src/3rdparty/wxWidgets
# ---------------------------------------------------------------------------

# Configure wxWidgets build options before add_subdirectory
set(wxBUILD_SHARED   ON  CACHE BOOL "Build wxWidgets as shared libraries" FORCE)
set(wxBUILD_MONOLITHIC OFF CACHE BOOL "Build wxWidgets as a single library" FORCE)

# Disable wxWidgets components we do not need to speed up the build
set(wxBUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES  OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(wxBUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

# Ensure the submodule directory exists
if(NOT EXISTS "${OES_3RDPARTY_DIR}/wxWidgets/CMakeLists.txt")
    message(FATAL_ERROR
        "wxWidgets submodule not found at ${OES_3RDPARTY_DIR}/wxWidgets.\n"
        "Run: git submodule update --init --recursive"
    )
endif()

add_subdirectory("${OES_3RDPARTY_DIR}/wxWidgets" "${CMAKE_BINARY_DIR}/wxWidgets")

# ---------------------------------------------------------------------------
# Vendored code compiles QUIET.
#
# wxWidgets and the C libraries it bundles (libwebp, libpng, libtiff, zlib, ...) are
# not ours to fix, and at /W4 they DROWN the build we can act on: of the 703 MSVC
# warnings in the 2026-08-02 CI run, 305 were a single C4244 family inside libwebp and
# not one came from src/engine. Same story for GCC's unused-variable / reorder counts.
#
# Their warnings are silenced at the TARGET level, not by lowering the project-wide
# level: our own code keeps /W4 and -Wall, so what is left in a log is ours.
# ---------------------------------------------------------------------------
function(oes_silence_directory_targets dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type STREQUAL "INTERFACE_LIBRARY" AND NOT _type STREQUAL "UTILITY")
            if(MSVC)
                target_compile_options(${_t} PRIVATE /W0)
            else()
                target_compile_options(${_t} PRIVATE -w)
            endif()
        endif()
    endforeach()

    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s IN LISTS _subdirs)
        oes_silence_directory_targets("${_s}")
    endforeach()
endfunction()

oes_silence_directory_targets("${OES_3RDPARTY_DIR}/wxWidgets")

# After add_subdirectory, wxWidgets targets (wx::core, wx::base, wx::aui, etc.)
# are available as imported targets.

# ---------------------------------------------------------------------------
# Optional database drivers — find system libraries
# ---------------------------------------------------------------------------

if(OES_USE_FIREBIRD)
    # fbclient is loaded dynamically at runtime (firebirdInterface.cpp ->
    # wxDynamicLibrary::Load("fbclient")) and the driver compiles against the
    # in-tree headers under databaseLayer/firebird/engine. A system fbclient is
    # therefore NOT required to build or link — locate it only opportunistically
    # so packagers that prefer an explicit dependency can still find it.
    find_library(FIREBIRD_LIBRARY NAMES fbclient fbclient_ms
        HINTS
            "$ENV{FIREBIRD_HOME}/lib"
            "/Library/Frameworks/Firebird.framework/Versions/A/Libraries"
            "/Library/Frameworks/Firebird.framework/Versions/A/Resources/lib"
            "/usr/lib"
            "/usr/local/lib"
            "/opt/firebird/lib"
    )
    add_compile_definitions(OES_USE_FIREBIRD)
    if(FIREBIRD_LIBRARY)
        message(STATUS "Firebird client found (optional): ${FIREBIRD_LIBRARY}")
    else()
        message(STATUS "Firebird client not found — driver loads fbclient dynamically at runtime")
    endif()
    if(OES_FB_LOCALSERVER)
        add_compile_definitions(OES_FB_LOCALSERVER)
        message(STATUS "OES_FB_LOCALSERVER: ON — firebird.exe must be present in _fb/ at runtime")
    endif()
endif()

if(OES_USE_POSTGRESQL)
    # libpq is likewise loaded dynamically at runtime and the driver compiles
    # against the in-tree headers under databaseLayer/postgres/engine, so a
    # system libpq is not required to build. Locate it only opportunistically.
    find_package(PostgreSQL QUIET)
    add_compile_definitions(OES_USE_POSTGRESQL)
    if(PostgreSQL_FOUND)
        message(STATUS "PostgreSQL client found (optional): ${PostgreSQL_LIBRARIES}")
    else()
        message(STATUS "PostgreSQL client not found — driver loads libpq dynamically at runtime")
    endif()
endif()

if(OES_USE_MYSQL)
    # CMake ships FindMySQL on some platforms; fall back to pkg-config
    find_library(MYSQL_LIBRARY NAMES mysqlclient mariadbclient
        HINTS
            "$ENV{MYSQL_HOME}/lib"
            "/usr/lib"
            "/usr/local/lib"
            "/usr/lib/x86_64-linux-gnu"
    )
    find_path(MYSQL_INCLUDE_DIR NAMES mysql.h
        HINTS
            "$ENV{MYSQL_HOME}/include"
            "/usr/include/mysql"
            "/usr/local/include/mysql"
    )
    if(MYSQL_LIBRARY AND MYSQL_INCLUDE_DIR)
        message(STATUS "MySQL found: ${MYSQL_LIBRARY}")
        add_library(MySQL::MySQL UNKNOWN IMPORTED)
        set_target_properties(MySQL::MySQL PROPERTIES
            IMPORTED_LOCATION "${MYSQL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MYSQL_INCLUDE_DIR}"
        )
        add_compile_definitions(OES_USE_MYSQL)
    else()
        message(FATAL_ERROR "OES_USE_MYSQL is ON but MySQL client library was not found.")
    endif()
endif()

if(OES_USE_ODBC)
    if(WIN32)
        # ODBC is a system library on Windows (odbc32.lib)
        set(ODBC_LIBRARIES odbc32 odbccp32)
    else()
        find_library(ODBC_LIBRARY NAMES odbc unixODBC
            HINTS "/usr/lib" "/usr/local/lib"
        )
        find_path(ODBC_INCLUDE_DIR NAMES sql.h
            HINTS "/usr/include" "/usr/local/include"
        )
        if(NOT ODBC_LIBRARY)
            message(FATAL_ERROR "OES_USE_ODBC is ON but ODBC library was not found.")
        endif()
        set(ODBC_LIBRARIES "${ODBC_LIBRARY}")
    endif()
    add_compile_definitions(OES_USE_ODBC)
    message(STATUS "ODBC enabled: ${ODBC_LIBRARIES}")
endif()

# ---------------------------------------------------------------------------
# Platform libraries
# ---------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 QUIET gtk+-3.0)
    endif()
endif()

# ---------------------------------------------------------------------------
# Engine subdirectories
#
# Each subdirectory must provide its own CMakeLists.txt defining its target.
# The expected target names and types are:
#
#   backend       — SHARED library  (defines BACKEND_EXPORTS)
#   frontend      — SHARED library  (defines FRONTEND_EXPORTS, links wx GUI)
#   daemon        — executable      (console subsystem)
#   enterprise    — executable      (GUI subsystem)
#   designer      — executable      (GUI subsystem)
#   launcher      — executable      (GUI subsystem)
#   codeRunner    — executable      (GUI subsystem)
#   simplePlugin  — SHARED library  (plugin example)
# ---------------------------------------------------------------------------

add_subdirectory("${OES_ENGINE_DIR}/backend")
add_subdirectory("${OES_ENGINE_DIR}/frontend")
add_subdirectory("${OES_ENGINE_DIR}/daemon")
add_subdirectory("${OES_ENGINE_DIR}/enterprise")
add_subdirectory("${OES_ENGINE_DIR}/designer")
add_subdirectory("${OES_ENGINE_DIR}/launcher")
add_subdirectory("${OES_ENGINE_DIR}/codeRunner")
add_subdirectory("${OES_ENGINE_DIR}/simplePlugin")

# ---------------------------------------------------------------------------
# Unit tests (Google Test)
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "=== OES Enterprise Build Configuration ===")
message(STATUS "  Version ........... ${PROJECT_VERSION}")
message(STATUS "  C++ Standard ...... ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type ........ ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform .......... ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Compiler .......... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Install prefix .... ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  wxWidgets ......... submodule (shared)")
message(STATUS "  Firebird .......... ${OES_USE_FIREBIRD}")
message(STATUS "  PostgreSQL ........ ${OES_USE_POSTGRESQL}")
message(STATUS "  MySQL ............. ${OES_USE_MYSQL}")
message(STATUS "  ODBC .............. ${OES_USE_ODBC}")
message(STATUS "  SQLite ............ ON (embedded)")
message(STATUS "  Tests ............. ${BUILD_TESTING}")
message(STATUS "  Sanitizers ........ ${OES_SANITIZE}")
message(STATUS "=============================================")
message(STATUS "")
