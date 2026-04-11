# =============================================================================
# OES Enterprise — frontend shared library
# All wxWidgets GUI code: forms, visual controls, charts, dialogs, editors,
# themes, and the visual host framework.
# =============================================================================

file(GLOB_RECURSE FRONTEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Exclude legacy/broken special dialog files (use old CValue naming)
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "special/valueColourDialog\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "special/valueFileDialog\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "special/valueFontDialog\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "special/valueGrid\\.cpp$")

add_library(frontend SHARED ${FRONTEND_SOURCES})

target_include_directories(frontend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
)

target_compile_definitions(frontend
    PRIVATE
        NOPCH
)

target_link_libraries(frontend
    PRIVATE
        backend
        wx::core
        wx::aui
        wx::html
        wx::propgrid
        wx::adv
        wx::stc
        wx::xrc
        wx::qa
        wx::base
        wx::net
        wx::xml
)

set_target_properties(frontend PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "frontend"
)
