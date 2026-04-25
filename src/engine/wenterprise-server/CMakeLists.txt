# =============================================================================
# OES Enterprise — wenterprise-server (standalone HTTP server for web client)
#
# Single-source executable: main.cpp + cpp-httplib (header-only).
# Links against wfrontend (web variant of frontend.dll).
# =============================================================================

cmake_minimum_required(VERSION 3.16)

add_executable(wenterprise-server main.cpp)

set_target_properties(wenterprise-server PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(wenterprise-server PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(wenterprise-server PRIVATE
    NOPCH
    OES_USE_WEB
)

target_include_directories(wenterprise-server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_SOURCE_DIR}/src/engine/frontend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(wenterprise-server PRIVATE
    wfrontend
    backend
    wx::base
    wx::net
    wx::xml
)

# Windows needs winsock for cpp-httplib
if(WIN32)
    target_link_libraries(wenterprise-server PRIVATE ws2_32 crypt32)
endif()
