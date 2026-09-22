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
# CONFIGURE_DEPENDS: a plain GLOB is evaluated ONCE, at configure time, so a file added afterwards is
# invisible until somebody thinks to re-run cmake — and nothing says so. The build simply links without
# it and fails somewhere else entirely, on a machine that is usually not the one where the file was
# added (the MSBuild project lists files by hand and is never affected). With this, the glob is
# re-checked on every build and a new source pulls a reconfigure by itself.
file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS
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
        # The MCP server's transport — header-only, included by mcpServer.cpp alone.
        "${CMAKE_SOURCE_DIR}/src/3rdparty/cpp-httplib"
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

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(backend PRIVATE
    LUABIND_DYNAMIC_LINK
)

if(OES_USE_FIREBIRD)
    target_compile_definitions(backend PRIVATE OES_USE_FIREBIRD)
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
        # cpp-httplib's TLS (CPPHTTPLIB_MBEDTLS_SUPPORT is set for the whole build in the root CMakeLists.txt):
        # PUBLIC, because every consumer that includes httplib.h - the web server, the tests - compiles the
        # same TLS roads and has to link them.
        mbedtls
        mbedx509
        mbedcrypto
)
if(WIN32)
    # The system certificate store, read by the library for a secure connection that names no trusted file.
    # (ws2_32 and bcrypt come with mbedcrypto itself.)
    target_link_libraries(backend PUBLIC crypt32)
endif()
if(APPLE)
    # With a TLS backend and Apple's compiler, cpp-httplib reads the keychain for the system's trusted
    # certificates (CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN, which it defines itself) - and that is
    # SecTrustSettingsCopyCertificates, in the Security framework. PUBLIC for the reason the mbedtls
    # libraries are: everything that includes the header compiles the same call.
    target_link_libraries(backend PUBLIC "-framework Security" "-framework CoreFoundation")
endif()

# ---------------------------------------------------------------------------
# Platform libraries
# ---------------------------------------------------------------------------
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(backend PRIVATE
        ${CMAKE_DL_LIBS}     # dlopen / dlsym
        Threads::Threads      # pthread
    )

    # guid.cpp picks its generator by toolkit: Windows -> CoCreateGuid, macOS -> CFUUID
    # (CoreFoundation), everything else -> libuuid's uuid_generate. Only the last one is a
    # separate library, and only on non-Apple UNIX; Apple's CFUUID rides in CoreFoundation.
    if(NOT APPLE)
        find_library(UUID_LIBRARY NAMES uuid)
        if(UUID_LIBRARY)
            target_link_libraries(backend PRIVATE ${UUID_LIBRARY})
        else()
            message(FATAL_ERROR
                "libuuid not found. guid.cpp uses uuid_generate on this platform — "
                "install uuid-dev (Debian/Ubuntu) or libuuid-devel (Fedora/RHEL).")
        endif()
    else()
        target_link_libraries(backend PRIVATE "-framework CoreFoundation")
    endif()
endif()

if(WIN32 AND OES_USE_ODBC)
    target_link_libraries(backend PRIVATE odbc32 odbccp32)
endif()

# ---------------------------------------------------------------------------
# The database clients next to the binaries on Windows — THE SAME FILES, NAMES AND FOLDERS the Visual
# Studio project copies (backend.vcxproj, CopyFileToFolders). The in-tree kits carry both architectures
# under one name with `_x64` / `_x86` in it; the running engine loads them by the plain name, so each is
# copied for the build's own architecture and renamed on the way. Firebird goes into `_fb/` (the embedded
# engine, its plugins and ICU, and firebird.exe for the leader mode), the PostgreSQL client beside the
# executables. Without this a CMake-built Windows tree opened no Firebird base at all, which is the
# default one — and it is the tree the nightly release is packed from.
#
# The three plugins the vcxproj leaves out stay out here too: default_profiler, fbtrace, udr_engine.
# ---------------------------------------------------------------------------
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OES_WIN_ARCH "x64")
    else()
        set(OES_WIN_ARCH "x86")
    endif()
    set(OES_BIN_DIR "$<TARGET_FILE_DIR:backend>")
    set(OES_CLIENT_COPY)

    if(OES_USE_FIREBIRD)
        set(OES_FB_KIT "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/firebird/engine/dll")
        list(APPEND OES_CLIENT_COPY
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OES_BIN_DIR}/_fb/intl" "${OES_BIN_DIR}/_fb/plugins")
        # The same file for either architecture.
        foreach(OES_FB_FILE firebird.conf firebird.msg security5.fdb icudt63l.dat intl/fbintl.conf)
            list(APPEND OES_CLIENT_COPY
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OES_FB_KIT}/${OES_FB_FILE}" "${OES_BIN_DIR}/_fb/${OES_FB_FILE}")
        endforeach()
        # One per architecture: <dir>/<name>_<arch><ext> in the kit, <dir>/<name><ext> in _fb.
        foreach(OES_FB_FILE fbclient.dll ib_util.dll icudt63.dll icuin63.dll icuuc63.dll firebird.exe
                            intl/fbintl.dll plugins/engine13.dll plugins/srp.dll plugins/chacha.dll
                            plugins/legacy_auth.dll plugins/legacy_usermanager.dll)
            get_filename_component(OES_FB_SUBDIR "${OES_FB_FILE}" DIRECTORY)
            get_filename_component(OES_FB_NAME   "${OES_FB_FILE}" NAME_WE)
            get_filename_component(OES_FB_EXT    "${OES_FB_FILE}" LAST_EXT)
            if(OES_FB_SUBDIR)
                set(OES_FB_SUBDIR "${OES_FB_SUBDIR}/")
            endif()
            list(APPEND OES_CLIENT_COPY
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OES_FB_KIT}/${OES_FB_SUBDIR}${OES_FB_NAME}_${OES_WIN_ARCH}${OES_FB_EXT}"
                    "${OES_BIN_DIR}/_fb/${OES_FB_FILE}")
        endforeach()
    endif()

    if(OES_USE_POSTGRESQL)
        # ⚠ OpenSSL KEEPS ITS NAME ON x64. The x64 libpq imports `libssl-3-x64.dll` / `libcrypto-3-x64.dll` —
        # that is OpenSSL's own x64 naming — and only the x86 one imports the plain names, so the vcxproj
        # renames the pair for x86 alone. Renamed on x64, libpq.dll does not load.
        if(OES_WIN_ARCH STREQUAL "x64")
            set(OES_SSL_SUFFIX "-x64")
        else()
            set(OES_SSL_SUFFIX "")
        endif()
        set(OES_PG_KIT "${CMAKE_CURRENT_SOURCE_DIR}/databaseLayer/postgres/engine/dll")
        list(APPEND OES_CLIENT_COPY
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OES_PG_KIT}/libpq_${OES_WIN_ARCH}.dll"        "${OES_BIN_DIR}/libpq.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OES_PG_KIT}/libssl-3-${OES_WIN_ARCH}.dll"    "${OES_BIN_DIR}/libssl-3${OES_SSL_SUFFIX}.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OES_PG_KIT}/libcrypto-3-${OES_WIN_ARCH}.dll" "${OES_BIN_DIR}/libcrypto-3${OES_SSL_SUFFIX}.dll")
    endif()

    # backend.conf — the start-up settings (the interface language) the vcxproj copies beside the binaries too.
    list(APPEND OES_CLIENT_COPY
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/backend.conf" "${OES_BIN_DIR}/backend.conf")

    if(OES_CLIENT_COPY)
        add_custom_command(TARGET backend POST_BUILD
            ${OES_CLIENT_COPY}
            COMMENT "Copying the Firebird and PostgreSQL clients and backend.conf next to the binaries (as backend.vcxproj does)"
            VERBATIM)
    endif()
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

# ---------------------------------------------------------------------------
# Syntax-helper content stage — pack syntaxHelper/<locale>/*.json into
# $<TARGET_FILE_DIR:backend>/help/<locale>.hlk (zip, DEFLATE) so the
# runtime locator finds one file per locale next to the exe. Source-
# of-truth is the source tree; dev runs read from there directly via
# the walk-up fallback in ibHelpService::ResolvePlatformDir (loader's
# ResolveSource picks zip when present, dir otherwise).
#
# `cmake -E tar c ... --format=zip` is built into CMake 3.18+ (project
# requires 3.20). One custom command per locale so a touched-but-not-
# changed locale doesn't repack everything.
# ---------------------------------------------------------------------------
file(GLOB SYNTAX_HELPER_LOCALE_DIRS LIST_DIRECTORIES TRUE
    RELATIVE "${CMAKE_SOURCE_DIR}/syntaxHelper"
    "${CMAKE_SOURCE_DIR}/syntaxHelper/*")
add_custom_command(TARGET backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:backend>/help"
    COMMENT "Packing syntax-helper content into bin/help/*.hlk"
    VERBATIM)
foreach(LOC ${SYNTAX_HELPER_LOCALE_DIRS})
    if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/syntaxHelper/${LOC}")
        add_custom_command(TARGET backend POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E chdir
                "${CMAKE_SOURCE_DIR}/syntaxHelper/${LOC}"
                ${CMAKE_COMMAND} -E tar c
                "$<TARGET_FILE_DIR:backend>/help/${LOC}.hlk"
                --format=zip "."
            COMMENT "  → ${LOC}.hlk"
            VERBATIM)
    endif()
endforeach()
