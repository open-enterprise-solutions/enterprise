# =============================================================================
# deskgui — the desktop's user interface library: what the desktop's programs draw their windows with (deskgui.h).
# The thin client's windows (frmclient), the launcher and the code runner link it; of the engine, the core alone - no
# protocol, no backend.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build but the listed unit —
# the staged settings, unfinished, not in the project (deskgui.vcxproj) either.
file(GLOB_RECURSE DESKGUI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER DESKGUI_SOURCES EXCLUDE REGEX "/settings\\.cpp$")
# the code editor's host draws through Cocoa on macOS, as wx's own (STC_OSX_COCOA in its files.cmake)
if(APPLE)
    list(APPEND DESKGUI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/univ/stc/PlatWXcocoa.mm")
endif()

add_library(deskgui SHARED ${DESKGUI_SOURCES})

# THE FORKS COMPILE QUIET — the wxWidgets submodule's rule (the root CMakeLists), applied per file because they live
# inside our target: the wxUniversal engine, wx's dataview, grid, treelistctrl, propgrid and AUI. Upstream style is
# not ours to correct, and the volume hides our own: 150 of the 168 -Wreorder lines in the 2026-08-02 CI run came from
# dataview alone. The limit, learned from a CI log: this reaches the .cpp only; a vendored header with inline
# definitions warns inside every one of ours that includes it, and silences itself with `#pragma GCC system_header`.
# docs/private/wx-fork.md is the inventory of the forks.
if(MSVC)
    set_source_files_properties(${DESKGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(${DESKGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(deskgui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
)

target_compile_definitions(deskgui
    PRIVATE
        NOPCH
)

# DESKGUI_API resolves to dllexport only when DESKGUI_EXPORTS is defined (deskgui.h).
if(WIN32)
    target_compile_definitions(deskgui PRIVATE DESKGUI_EXPORTS)
endif()

target_link_libraries(deskgui
    PUBLIC
        core
    PRIVATE
        wx::core
        wx::base
)

# THE CODE EDITOR'S HOST (univ/stc) — over Scintilla and Lexilla, src/3rdparty's, the root CMakeLists builds them; their
# headers arrive with the targets.
target_link_libraries(deskgui PRIVATE scintilla lexilla)
if(APPLE)
    target_link_libraries(deskgui PRIVATE "-framework AppKit")
endif()
if(WIN32)
    # a top-level window kept off the screen until it is painted whole (ibWindowShowLocker: DWMWA_CLOAK)
    target_link_libraries(deskgui PRIVATE dwmapi)
endif()

# WHAT IT EXPORTS, AND NOTHING ELSE — as a DLL does, like frmclient: hidden by default, DESKGUI_API shows what is meant
# to be seen.
set_target_properties(deskgui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "deskgui"
)
