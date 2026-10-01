# =============================================================================
# OES Enterprise — wenterprise-server (wes)
#
# The HTTP server the web client talks to: one translation unit plus the
# header-only cpp-httplib, over wfrontend (the OES_USE_WEB frontend).
# =============================================================================

add_executable(wenterprise-server main.cpp)

target_compile_definitions(wenterprise-server
    PRIVATE
        NOPCH
        OES_USE_WEB
)

target_include_directories(wenterprise-server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
        ${CMAKE_SOURCE_DIR}/src/engine/frontend
)

target_link_libraries(wenterprise-server
    PRIVATE
        wfrontend
        backend
        wx::base
        wx::net
        wx::xml
)

# cpp-httplib's sockets; the console subsystem so the banner and the shutdown
# lines have somewhere to go.
if(WIN32)
    target_link_libraries(wenterprise-server PRIVATE ws2_32 crypt32)
    set_target_properties(wenterprise-server PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

find_package(Threads REQUIRED)
target_link_libraries(wenterprise-server PRIVATE Threads::Threads)

set_target_properties(wenterprise-server PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
