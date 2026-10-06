# =============================================================================
# OES Enterprise — sfrontend shared library
# The server's frontend: forms, controls and the document/view skeleton with no window, and the client
# protocol that draws them (client/). Linked by the application server.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build: the project
# (sfrontend.vcxproj) lists the same set.
file(GLOB_RECURSE SFRONTEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(sfrontend SHARED ${SFRONTEND_SOURCES})

target_include_directories(sfrontend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
)

target_compile_definitions(sfrontend
    PRIVATE
        NOPCH
)

# SFRONTEND_API resolves to dllexport only when SFRONTEND_EXPORTS is defined (sfrontend.h) — frontend's rule.
if(WIN32)
    target_compile_definitions(sfrontend PRIVATE SFRONTEND_EXPORTS)
endif()

target_link_libraries(sfrontend
    PRIVATE
        backend
        wx::core
        wx::html
        wx::adv
        wx::base
        wx::net
        wx::xml
)

set_target_properties(sfrontend PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "sfrontend"
)
