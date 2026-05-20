# =============================================================================
# OES Enterprise — frontend shared library
# All wxWidgets GUI code: forms, visual controls, charts, dialogs, editors,
# themes, and the visual host framework.
# =============================================================================

file(GLOB_RECURSE FRONTEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Exclude web-only sources (built by wfrontend target with OES_USE_WEB)
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "/web/")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "wfrontend\\.cpp$")

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
        wx::webview
        md4c-html        # vendored CommonMark→HTML renderer
)

set_target_properties(frontend PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "frontend"
)

# =============================================================================
# wfrontend — web variant of frontend (OES_USE_WEB)
#
# Same visual-view / control / form / docView sources but compiled with
# OES_USE_WEB. Native win/ code excluded; web/ stubs supply the platform
# layer. Linked by wenterprise-server.
# =============================================================================

set(WFRONTEND_SOURCES
    # Entry point / public C API
    ${CMAKE_CURRENT_SOURCE_DIR}/wfrontend.cpp

    # Web-mode platform layer
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webChildFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webFrame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webSizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webStubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webTimer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/web/webWindow.cpp

    # Visual host framework
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHostEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHostClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/visualHostClientDocView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/controlEnum.cpp

    # Doc/view
    ${CMAKE_CURRENT_SOURCE_DIR}/docView/docView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/docView/docViewCmd.cpp

    # Controls — only files with ibFrontendWindow* signatures. The
    # excluded ctrl/*.cpp files still declare Create/OnCreated with
    # bare wxWindow* and would not match the header on web builds
    # where ibFrontendWindow = ibWebWindow.
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/control.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/controlProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/typeControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frameEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frameID.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/frameProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/sizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/boxsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/boxsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/wrapsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/staticboxsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/staticboxsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/gridsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/gridsizer_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/itemsizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/form.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/form_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formControl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formVisualHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/formMem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/button.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/button_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/buttonEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/statictext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/statictext_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrl_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrlEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/textctrlProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkbox_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkboxEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/checkboxProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBar_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarItem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarItem_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarAction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/toolBarItemAction.cpp

    # tableBox subset — formObject references ms_classInfo / RefreshModel /
    # GetIcon / GetIconGroup on ibValueModelTableBox and ibValueModelTableBoxColumn.
    # tableBox*.cpp themselves are #ifndef-guarded against wxDataView so they
    # compile cleanly with OES_USE_WEB. Files that include
    # tableBoxColumnRenderer.h directly (tableBoxColumnRenderer.cpp itself)
    # must stay out — its header pulls dataview.h whose enums conflict with
    # the web-stub enums in tableBox.h.
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBox_res.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBoxColumn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/visualView/ctrl/tableBoxColumn_res.cpp
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
        WFRONTEND_EXPORTS
)

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
