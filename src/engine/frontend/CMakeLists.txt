# =============================================================================
# OES Enterprise — frontend shared library
# All wxWidgets GUI code: forms, visual controls, charts, dialogs, editors,
# themes, and the visual host framework.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt: without it a newly added source stays
# invisible until somebody re-runs cmake, and nothing announces that it is missing.
file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS
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

# ---------------------------------------------------------------------------
# The FORKED widget layer compiles quiet — same rule as the wxWidgets submodule
# (see the root CMakeLists), applied per-file because these live inside our target.
#
# win/ctrls/{dataview,grid,charts,treelistctrl}, win/theme and uikit are vendored
# forks: wxUniversal, wxAUI dock art, wxCharts, wx's own dataview and grid. Upstream
# style is not ours to correct, and the volume hides our own: 150 of the 168 -Wreorder
# lines in the 2026-08-02 CI run came from win/ctrls/dataview alone.
#
# NOTE the limit of this, learned from a CI log rather than guessed: COMPILE_OPTIONS on
# a SOURCE file applies to that .cpp only. A vendored HEADER with inline definitions --
# dataview/datavgen.h is the loud one -- warns inside every OUR .cpp that includes it,
# where this flag is not in effect. Those headers carry `#pragma GCC system_header`
# instead; that is the only thing that silences a header at its own definition site.
#
# docs/wx-fork.md is the inventory of what is forked and why.
# ---------------------------------------------------------------------------
set(FRONTEND_VENDORED_SOURCES ${FRONTEND_SOURCES})
list(FILTER FRONTEND_VENDORED_SOURCES INCLUDE REGEX "/(uikit|win/ctrls|win/theme)/")

if(FRONTEND_VENDORED_SOURCES)
    if(MSVC)
        set_source_files_properties(${FRONTEND_VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties(${FRONTEND_VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

target_include_directories(frontend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
        # win/theme/luna_dockart.cpp is a fork of wxAUI's dock art and reaches into wx's
        # GTK-private headers, which include <gtk/gtk.h> outright. wx links GTK privately,
        # so those paths never arrive through wx::core. Empty off Linux, hence unguarded.
        ${GTK3_INCLUDE_DIRS}
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
