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

# =============================================================================
# wfrontend — the web variant of the frontend
#
# The same visual-view / control / form / docView sources as the desktop
# frontend, compiled with OES_USE_WEB: no native widget is created, the web/
# layer stands in for the platform one, and what a control would have drawn is
# reported as JSON instead. Linked by wenterprise-server.
#
# The list is HAND-PICKED and mirrors wfrontend.vcxproj — the MSBuild project is
# the one that has to agree with it, and a GLOB here would silently pull in the
# 400-odd desktop sources that do not compile in web mode.
# =============================================================================

if(OES_BUILD_WEB)

set(WFRONTEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/wfrontend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webSizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webTimer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webChildFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHostEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHostClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHostClientDocView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/controlEnum.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/docView/docManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/docView/docView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/control.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/controlProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/typeControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/sizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/boxsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/button.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/button_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/buttonEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/statictext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/statictextEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/statictext_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrlEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrlProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrl_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkboxEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkboxProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkbox_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/layers/commandBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/layers/commandReceiver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/layerObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBar_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarItem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarItem_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarItemAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBox_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBoxColumn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBoxColumn_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBoxColumnGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBoxColumnGroup_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/wrapsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/staticboxsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/gridsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/itemsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/form.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formAttribute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formAttributeProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formCommand.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settings/formSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formVisualHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frameEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frameID.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frameProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formMem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/form_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/boxsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/gridsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/staticboxsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/wrapsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webStubs.cpp
)

add_library(wfrontend SHARED ${WFRONTEND_SOURCES})

target_include_directories(wfrontend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
)

target_compile_definitions(wfrontend
    PRIVATE
        NOPCH
        OES_USE_WEB
)

# Same export rule as frontend: WFRONTEND_API is dllexport only while the DLL
# itself is being built (wfrontend.h).
if(WIN32)
    target_compile_definitions(wfrontend PRIVATE WFRONTEND_EXPORTS)
endif()

target_link_libraries(wfrontend
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

set_target_properties(wfrontend PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "wfrontend"
)

endif()
