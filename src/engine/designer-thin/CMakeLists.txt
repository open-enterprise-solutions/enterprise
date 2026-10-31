cmake_minimum_required(VERSION 3.16)

# The thin designer — designer-thin.exe, the designer as a thin client (mainApp.h says what it is). It links the thin client's core
# alone; a file base's server half (fileserver) is loaded by it when one is opened.
set(DESIGNER_THIN_SOURCES
    mainApp.cpp
    mainAppEvent.cpp
    backend/debugger/debugClient.cpp
    backend/debugger/debugClientAsync.cpp
    backend/debugger/debugClientBridge.cpp
    backend/debugger/debugClientQuery.cpp
    backend/vcs/gitService.cpp
    mainFrame/debugger/debugClientImpl.cpp
    win/editor/codeEditor/codeEditorDesigner.cpp
    win/editor/commonAttributeEditor/commonAttributeEditor.cpp
    win/editor/functionalOptionEditor/functionalOptionEditor.cpp
    win/editor/interfaceEditor/interfaceEditor.cpp
    win/editor/roleEditor/roleEditor.cpp
    win/editor/visualEditor/innerFrame.cpp
    win/editor/visualEditor/titleFrame.cpp
    win/editor/visualEditor/visualEditor.cpp
    win/editor/visualEditor/visualEditorAttributeTree.cpp
    win/editor/visualEditor/visualEditorCmdProc.cpp
    win/editor/visualEditor/visualEditorCommandTree.cpp
    win/editor/visualEditor/visualEditorEvent.cpp
    win/editor/visualEditor/visualEditorNotebook.cpp
    win/editor/visualEditor/visualEditorObjectTree.cpp
    win/editor/visualEditor/visualEditorPanel.cpp
    docManager/templates/docViewDataProcessorFile.cpp
    docManager/templates/docViewDataReportFile.cpp
    docManager/templates/docViewModuleEditor.cpp
    docManager/templates/docViewFormEditor.cpp
    docManager/templates/docViewAssistant.cpp
    docManager/templates/docViewCommonAttribute.cpp
    docManager/templates/docViewFunctionalOption.cpp
    docManager/templates/docViewComposer.cpp
    docManager/templates/docViewInterface.cpp
    docManager/templates/docViewRole.cpp
    docManager/templates/docViewConfigCompare.cpp
    docManager/templates/docViewMetaFile.cpp
    mainFrame/configCompare/configCompareModel.cpp
    mainFrame/local/localWindow.cpp
    mainFrame/mainFrameDesigner.cpp
    mainFrame/mainFrameDesignerEvent.cpp
    mainFrame/mainFrameDesignerMenu.cpp
    mainFrame/mainFrameDesignerParts.cpp
    mainFrame/metaTree/treeConfiguration.cpp
    mainFrame/metaTree/treeConfigurationEvent.cpp
    mainFrame/metaTree/treeConfiguration_impl.cpp
    mainFrame/metaTree/treeDataProcessor.cpp
    mainFrame/metaTree/treeDataProcessorEvent.cpp
    mainFrame/metaTree/treeDataProcessor_impl.cpp
    mainFrame/metaTree/treeDataReport.cpp
    mainFrame/metaTree/treeDataReportEvent.cpp
    mainFrame/metaTree/treeDataReport_impl.cpp
    mainFrame/stack/stackWindow.cpp
    mainFrame/vcs/gitPanel.cpp
    mainFrame/watch/watchWindow.cpp
    syntaxHelper/helpChooserDialog.cpp
    syntaxHelper/helpDetailView.cpp
    syntaxHelper/helpDragSource.cpp
    syntaxHelper/helpIndexView.cpp
    syntaxHelper/helpPaneView.cpp
    syntaxHelper/helpSearchView.cpp
    syntaxHelper/helpTreeView.cpp
    win/dlg/debugItem/debugItem.cpp
    win/dlg/homePageEditor.cpp
    win/dlg/predefinedEditor.cpp
    win/dlg/textEditor.cpp
)

add_executable(designer-thin ${DESIGNER_THIN_SOURCES})

set_target_properties(designer-thin PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(designer-thin PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(designer-thin PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
    set(OES_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/oes.icns")
    set_source_files_properties(${OES_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(designer-thin PRIVATE ${OES_ICON})
endif()

target_compile_definitions(designer-thin PRIVATE NOPCH)

target_include_directories(designer-thin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(designer-thin PRIVATE
    protocol
    frmclient
    wx::core wx::aui wx::stc wx::html wx::qa wx::xml wx::base
)
