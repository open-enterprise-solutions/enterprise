cmake_minimum_required(VERSION 3.16)

# oes-mcp — Model Context Protocol server. Standalone CLI binary that
# exposes Designer's metadata API over JSON-RPC 2.0 / stdio so any
# MCP-compatible client (Claude Code CLI, Cursor, JetBrains AI, ...) can
# drive an OES configuration locally.
#
# Links backend only — no frontend, no wxWidgets GUI. The backend itself
# transitively depends on wx::base for wxString/wxLog plumbing; we link
# wx::base + wx::net + wx::xml so the linker can satisfy backend's
# wx::*Dll references in shared-build mode. No wx GUI subsystems pulled.

set(OES_MCP_SOURCES
    main.cpp
    jsonrpc.cpp
    tools.cpp
    resources.cpp
    headless_app.cpp
)

add_executable(oes-mcp ${OES_MCP_SOURCES})

set_target_properties(oes-mcp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Console subsystem on Windows — stdio transport requires it.
if(WIN32)
    set_target_properties(oes-mcp PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(oes-mcp PRIVATE NOPCH)

target_include_directories(oes-mcp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_SOURCE_DIR}/src/3rdparty
    ${CMAKE_SOURCE_DIR}/src/3rdparty/cpp-httplib
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# backend brings in wx::base et al transitively, but we depend on the
# same wx libs the other backend-only consumers (daemon, codeRunner) link
# explicitly — keeps the linker happy on shared-wx builds where the
# imported wx targets only forward INTERFACE_INCLUDE_DIRECTORIES.
target_link_libraries(oes-mcp PRIVATE
    backend
    wx::base
    wx::net
    wx::xml
    wx::core
    wx::propgrid
    wx::aui
    wx::stc
)

# OpenSSL — required for the Pugi MCP proxy in sigma_check. Without it
# the tool falls back to its offline-mode envelope (fail-open) since the
# Pugi endpoint is https://. Same gating aiBridge uses.
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "oes-mcp: OpenSSL found — sigma_check HTTPS proxy enabled")
    target_compile_definitions(oes-mcp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(oes-mcp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    if(APPLE)
        target_link_libraries(oes-mcp PRIVATE
            "-framework CoreFoundation"
            "-framework Security")
    endif()
else()
    message(STATUS "oes-mcp: no OpenSSL — sigma_check will run in offline (fail-open) mode")
endif()

find_package(Threads REQUIRED)
target_link_libraries(oes-mcp PRIVATE Threads::Threads)

include(GNUInstallDirs)
install(TARGETS oes-mcp
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
