# =============================================================================
# OES Enterprise — fileserver shared library
# A file base for the thin client: LOADED by frmclient (never linked), and only when the base it opens is a file base.
# It links the server — the backend and frmserver — and hands the client the base's clients' host through three C
# doors (fileBase.h). Without it a thin client works with servers only.
# =============================================================================

file(GLOB FILESERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(fileserver SHARED ${FILESERVER_SOURCES})

target_include_directories(fileserver
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
)

target_compile_definitions(fileserver
    PRIVATE
        NOPCH
)

# FILESERVER_API resolves to dllexport only when FILESERVER_EXPORTS is defined (fileserver.h) — frontend's rule.
if(WIN32)
    target_compile_definitions(fileserver PRIVATE FILESERVER_EXPORTS)
endif()

target_link_libraries(fileserver
    PRIVATE
        frmserver
        backend
        wx::core
        wx::base
)

set_target_properties(fileserver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "fileserver"
)
