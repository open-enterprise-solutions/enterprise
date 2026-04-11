################################################################################
# OES Enterprise — backend shared library
#
# The backend is the core engine: compiler, metadata, database layer, debugger.
# It has NO GUI code. It links against wxBase (wxString, wxDateTime, wxFile,
# wxSocket, wxThread, etc.) but never against wxCore or any GUI toolkit.
################################################################################

cmake_minimum_required(VERSION 3.20)

# Database driver options and OES_USE_TBB are defined in the root CMakeLists.txt.

# ---------------------------------------------------------------------------
# Collect every .cpp and .c file under the backend tree
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# ---------------------------------------------------------------------------
# Remove platform-specific / optional sources that must be gated
# ---------------------------------------------------------------------------

# valueOLE.cpp uses COM/OLE — Windows only
if(NOT WIN32)
    list(FILTER BACKEND_SOURCES EXCLUDE REGEX "system/value/valueOLE\\.cpp$")
endif()

# Firebird driver
if(NOT OES_USE_FIREBIRD)
    list(FILTER BACKEND_SOURCES EXCLUDE REGEX "databaseLayer/firebird/.*\\.(cpp|c)$")
endif()

# MySQL driver
if(NOT OES_USE_MYSQL)
    list(FILTER BACKEND_SOURCES EXCLUDE REGEX "databaseLayer/mysql/.*\\.(cpp|c)$")
endif()

# PostgreSQL driver
if(NOT OES_USE_POSTGRESQL)
    list(FILTER BACKEND_SOURCES EXCLUDE REGEX "databaseLayer/postgres/.*\\.(cpp|c)$")
endif()

# ODBC driver
if(NOT OES_USE_ODBC)
    list(FILTER BACKEND_SOURCES EXCLUDE REGEX "databaseLayer/odbc/.*\\.(cpp|c)$")
endif()

# ---------------------------------------------------------------------------
# Embedded SQLite — compiled as C with thread-safety enabled
# ---------------------------------------------------------------------------
set(SQLITE3_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/sqllite/engine/sqlite3.c")

set_source_files_properties(${SQLITE3_SOURCE} PROPERTIES
    LANGUAGE C
    COMPILE_DEFINITIONS "SQLITE_THREADSAFE=1"
)

# ---------------------------------------------------------------------------
# Shared library target
# ---------------------------------------------------------------------------
add_library(backend SHARED ${BACKEND_SOURCES})

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(backend
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/engine"
        # Embedded database engine headers
        "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/sqllite/engine"
)

# Firebird headers (bundled in-tree)
if(OES_USE_FIREBIRD)
    target_include_directories(backend PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/firebird/engine"
    )
endif()

# PostgreSQL headers (bundled in-tree)
if(OES_USE_POSTGRESQL)
    target_include_directories(backend PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/postgres/engine"
    )
endif()

# MySQL headers (bundled in-tree)
if(OES_USE_MYSQL)
    target_include_directories(backend PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/mysql/engine"
    )
endif()

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(backend PRIVATE
    LUABIND_DYNAMIC_LINK
)

if(OES_USE_FIREBIRD)
    target_compile_definitions(backend PRIVATE OES_USE_FIREBIRD)
endif()
if(OES_USE_MYSQL)
    target_compile_definitions(backend PRIVATE OES_USE_MYSQL)
endif()
if(OES_USE_POSTGRESQL)
    target_compile_definitions(backend PRIVATE OES_USE_POSTGRESQL)
endif()
if(OES_USE_ODBC)
    target_compile_definitions(backend PRIVATE OES_USE_ODBC)
endif()
if(OES_USE_TBB)
    target_compile_definitions(backend PRIVATE USE_TBB_PARALLEL)
endif()

# Windows DLL export macro
if(WIN32)
    target_compile_definitions(backend PRIVATE BACKEND_EXPORTS)
endif()

# ---------------------------------------------------------------------------
# wxWidgets — the backend code currently uses some GUI types (wxIcon, wxFont,
# wxPoint, wxBitmap) inherited from the Windows build, so we link wxCore too.
# TODO: refactor backend to truly separate GUI types behind interfaces.
# ---------------------------------------------------------------------------
target_link_libraries(backend
    PUBLIC
        wx::base
        wx::core
        wx::net
        wx::xml
        wx::propgrid
        wx::aui
        wx::stc
)

# ---------------------------------------------------------------------------
# Platform libraries
# ---------------------------------------------------------------------------
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(backend PRIVATE
        ${CMAKE_DL_LIBS}     # dlopen / dlsym
        Threads::Threads      # pthread
    )
endif()

if(WIN32 AND OES_USE_ODBC)
    target_link_libraries(backend PRIVATE odbc32 odbccp32)
endif()

# ---------------------------------------------------------------------------
# TBB (optional)
# ---------------------------------------------------------------------------
if(OES_USE_TBB)
    find_package(TBB REQUIRED)
    target_link_libraries(backend PRIVATE TBB::tbb)
endif()

# ---------------------------------------------------------------------------
# Target properties
# ---------------------------------------------------------------------------
set_target_properties(backend PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
    C_STANDARD            11
    C_STANDARD_REQUIRED   ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME           "backend"
)

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS backend
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
