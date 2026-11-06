# =============================================================================
# gui — the desktop's user interface library: what the desktop's programs draw their windows with (gui.h).
# The thin client's windows (frmclient) and the launcher link it; of the engine, the core alone - no protocol, no backend.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build but the listed units —
# the frame layer staged for the custom-drawn shell and the canon's win32 theme, unfinished, not in the project
# (gui.vcxproj) either.
file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER GUI_SOURCES EXCLUDE REGEX "/statusBar\\.cpp$")
list(FILTER GUI_SOURCES EXCLUDE REGEX "/dialog\\.cpp$")
list(FILTER GUI_SOURCES EXCLUDE REGEX "/settings\\.cpp$")
list(FILTER GUI_SOURCES EXCLUDE REGEX "theme/theme_win32\\.cpp$")
list(FILTER GUI_SOURCES EXCLUDE REGEX "/topLevel\\.cpp$")
list(FILTER GUI_SOURCES EXCLUDE REGEX "/frame\\.cpp$")

add_library(gui SHARED ${GUI_SOURCES})

# THE FORKS COMPILE QUIET — the wxWidgets submodule's rule (the root CMakeLists), applied per file because they live
# inside our target: the wxUniversal engine, wx's dataview, grid and treelistctrl, wxAUI's dock art. Upstream style is
# not ours to correct, and the volume hides our own: 150 of the 168 -Wreorder lines in the 2026-08-02 CI run came from
# dataview alone. The limit, learned from a CI log: this reaches the .cpp only; a vendored header with inline
# definitions warns inside every one of ours that includes it, and silences itself with `#pragma GCC system_header`.
# docs/private/wx-fork.md is the inventory of the forks.
if(MSVC)
    set_source_files_properties(${GUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(${GUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(gui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        # univ/theme/luna_dockart.cpp is a fork of wxAUI's dock art and reaches into wx's GTK-private headers, which
        # include <gtk/gtk.h> outright. wx links GTK privately, so those paths never arrive through wx::core. Empty
        # off Linux, hence unguarded.
        ${GTK3_INCLUDE_DIRS}
)

target_compile_definitions(gui
    PRIVATE
        NOPCH
)

# GUI_API resolves to dllexport only when GUI_EXPORTS is defined (gui.h).
if(WIN32)
    target_compile_definitions(gui PRIVATE GUI_EXPORTS)
endif()

target_link_libraries(gui
    PUBLIC
        core
    PRIVATE
        wx::core
        wx::aui
        wx::base
)

# WHAT IT EXPORTS, AND NOTHING ELSE — as a DLL does, like frmclient: hidden by default, GUI_API shows what is meant
# to be seen.
set_target_properties(gui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "gui"
)
