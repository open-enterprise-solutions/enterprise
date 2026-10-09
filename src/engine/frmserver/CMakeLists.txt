# =============================================================================
# OES Enterprise — frmserver shared library
# The server's frontend: forms, controls and the document/view skeleton with no window, and the client
# protocol that draws them (client/). Linked by the application server.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build: the project
# (frmserver.vcxproj) lists the same set.
file(GLOB_RECURSE FRMSERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(frmserver SHARED ${FRMSERVER_SOURCES})

target_include_directories(frmserver
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
)

target_compile_definitions(frmserver
    PRIVATE
        NOPCH
)

# FRMSERVER_API resolves to dllexport only when FRMSERVER_EXPORTS is defined (frmserver.h) — frontend's rule.
if(WIN32)
    target_compile_definitions(frmserver PRIVATE FRMSERVER_EXPORTS)
endif()

target_link_libraries(frmserver
    PRIVATE
        backend
        wx::core
        wx::html
        wx::adv
        wx::base
        wx::net
        wx::xml
)

set_target_properties(frmserver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "frmserver"
)
