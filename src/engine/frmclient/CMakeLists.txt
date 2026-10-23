# =============================================================================
# OES Enterprise — frmclient shared library
# The thin client's core, shared by the thin runtime and the thin designer: the main window and what is drawn in it,
# over the protocol's library (protocol/: the connections to a base's clients and the communicator that keeps the
# frame).
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build but the listed uikit
# units — unfinished, not in the project (frmclient.vcxproj) either.
file(GLOB_RECURSE FRMCLIENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER FRMCLIENT_SOURCES EXCLUDE REGEX "uikit/ctrl/statusBar\\.cpp$")
list(FILTER FRMCLIENT_SOURCES EXCLUDE REGEX "uikit/dialog\\.cpp$")
list(FILTER FRMCLIENT_SOURCES EXCLUDE REGEX "uikit/settings\\.cpp$")
list(FILTER FRMCLIENT_SOURCES EXCLUDE REGEX "uikit/theme/theme_win32\\.cpp$")
list(FILTER FRMCLIENT_SOURCES EXCLUDE REGEX "uikit/topLevel\\.cpp$")
list(FILTER FRMCLIENT_SOURCES EXCLUDE REGEX "uikit/frame\\.cpp$")

add_library(frmclient SHARED ${FRMCLIENT_SOURCES})

if(APPLE)
    target_sources(frmclient PRIVATE mainFrame/mainFrameMac.mm)
    target_link_libraries(frmclient PRIVATE "-framework AppKit")
endif()

# The FORKED widget layer compiles quiet — frontend's rule, for the same forks copied here (win/ctrls: wx's dataview,
# grid, treelistctrl; win/theme: wxAUI dock art; uikit: wxUniversal). frontend/CMakeLists.txt has the why and the
# limit: this reaches the .cpp only, a vendored header silences itself with `#pragma GCC system_header`.
set(FRMCLIENT_VENDORED_SOURCES ${FRMCLIENT_SOURCES})
list(FILTER FRMCLIENT_VENDORED_SOURCES INCLUDE REGEX "/(uikit|win/ctrls|win/theme)/")

if(FRMCLIENT_VENDORED_SOURCES)
    if(MSVC)
        set_source_files_properties(${FRMCLIENT_VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties(${FRMCLIENT_VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

target_include_directories(frmclient
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        # win/theme/luna_dockart.cpp is a fork of wxAUI's dock art and reaches into wx's
        # GTK-private headers, which include <gtk/gtk.h> outright. wx links GTK privately,
        # so those paths never arrive through wx::core. Empty off Linux, hence unguarded.
        ${GTK3_INCLUDE_DIRS}
)

target_compile_definitions(frmclient
    PRIVATE
        NOPCH
)

# FRMCLIENT_API resolves to dllexport only when FRMCLIENT_EXPORTS is defined (frmclient.h) — frontend's rule.
if(WIN32)
    target_compile_definitions(frmclient PRIVATE FRMCLIENT_EXPORTS)
endif()

# Nothing of the server — not even the backend: the client talks through the protocol's library (protocol/), which
# carries the wire's tree and the connections; and it speaks the core's meanings (core/), as the engine does.
target_link_libraries(frmclient
    PUBLIC
        protocol
        core
    PRIVATE
        wx::core
        wx::aui
        wx::html
        wx::propgrid
        wx::stc
        wx::base
        wx::xml
)

find_package(Threads REQUIRED)
target_link_libraries(frmclient PRIVATE Threads::Threads)

# WHAT IT EXPORTS, AND NOTHING ELSE — as a DLL does. A file base loads the backend into this process, and on an ELF
# system a name two libraries export is ONE: the backend's own calls would land in this library's copies (the model
# view, wx's event names). Hidden by default, FRMCLIENT_API shows what is meant to be seen.
set_target_properties(frmclient PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "frmclient"
)
