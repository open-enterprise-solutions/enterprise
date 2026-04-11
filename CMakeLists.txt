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

# SQLite is always available (embedded sources in the project).

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
    # Match the MSBuild settings: fast floating point, RTTI enabled, W1
    add_compile_options(/fp:fast /GR /W1)
    # Suppress the same warnings disabled in the .vcxproj files
    add_compile_options(/wd4251 /wd4275 /wd4458)
else()
    # GCC / Clang
    add_compile_options(-Wall -Wno-unused-parameter -Wno-sign-compare)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-deprecated-declarations)
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

# After add_subdirectory, wxWidgets targets (wx::core, wx::base, wx::aui, etc.)
# are available as imported targets.

# ---------------------------------------------------------------------------
# Optional database drivers — find system libraries
# ---------------------------------------------------------------------------

if(OES_USE_FIREBIRD)
    # Firebird client library (fbclient)
    find_library(FIREBIRD_LIBRARY NAMES fbclient fbclient_ms
        HINTS
            "$ENV{FIREBIRD_HOME}/lib"
            "/Library/Frameworks/Firebird.framework/Versions/A/Libraries"
            "/Library/Frameworks/Firebird.framework/Versions/A/Resources/lib"
            "/usr/lib"
            "/usr/local/lib"
            "/opt/firebird/lib"
    )
    find_path(FIREBIRD_INCLUDE_DIR NAMES ibase.h
        HINTS
            "$ENV{FIREBIRD_HOME}/include"
            "/Library/Frameworks/Firebird.framework/Versions/A/Headers"
            "/usr/include"
            "/usr/local/include"
            "/opt/firebird/include"
    )
    if(FIREBIRD_LIBRARY AND FIREBIRD_INCLUDE_DIR)
        message(STATUS "Firebird found: ${FIREBIRD_LIBRARY}")
        add_library(Firebird::Firebird UNKNOWN IMPORTED)
        set_target_properties(Firebird::Firebird PROPERTIES
            IMPORTED_LOCATION "${FIREBIRD_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FIREBIRD_INCLUDE_DIR}"
        )
        add_compile_definitions(OES_USE_FIREBIRD)
    else()
        message(FATAL_ERROR "OES_USE_FIREBIRD is ON but Firebird client library was not found.")
    endif()
endif()

if(OES_USE_POSTGRESQL)
    find_package(PostgreSQL REQUIRED)
    add_compile_definitions(OES_USE_POSTGRESQL)
    message(STATUS "PostgreSQL found: ${PostgreSQL_LIBRARIES}")
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
#   classChecker  — executable      (GUI subsystem)
#   simplePlugin  — SHARED library  (plugin example)
# ---------------------------------------------------------------------------

add_subdirectory("${OES_ENGINE_DIR}/backend")
add_subdirectory("${OES_ENGINE_DIR}/frontend")
add_subdirectory("${OES_ENGINE_DIR}/daemon")
add_subdirectory("${OES_ENGINE_DIR}/enterprise")
add_subdirectory("${OES_ENGINE_DIR}/designer")
add_subdirectory("${OES_ENGINE_DIR}/launcher")
add_subdirectory("${OES_ENGINE_DIR}/codeRunner")
add_subdirectory("${OES_ENGINE_DIR}/classChecker")
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
message(STATUS "=============================================")
message(STATUS "")
