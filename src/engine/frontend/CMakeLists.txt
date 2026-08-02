# =============================================================================
# OES Enterprise — frontend shared library
# All wxWidgets GUI code: forms, visual controls, charts, dialogs, editors,
# themes, and the visual host framework.
# =============================================================================

file(GLOB_RECURSE FRONTEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# GLOB grabs every .cpp, but the desktop frontend.dll (frontend.vcxproj) builds
# a HAND-PICKED subset. The files below are NOT in that project, so building
# them here breaks the CMake build:
#   * web/ + wfrontend.cpp — web sources; they belong to wfrontend.dll and only
#     compile with OES_USE_WEB defined.
#   * the listed uikit units — unfinished, not part of the desktop build.
# Excluding them realigns the GLOB with the real .sln source set.
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "/web/")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "wfrontend\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/ctrl/statusBar\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/dialog\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/settings\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/theme/theme_win32\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/topLevel\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/frame\\.cpp$")

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

# Windows DLL export macro — FRONTEND_API resolves to dllexport only when
# FRONTEND_EXPORTS is defined (frontend.h); without it every FRONTEND_API class
# is dllimport and its wx event-table / class-info static members fail to define
# (C2491). Mirrors backend's BACKEND_EXPORTS.
if(WIN32)
    target_compile_definitions(frontend PRIVATE FRONTEND_EXPORTS)
endif()

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
