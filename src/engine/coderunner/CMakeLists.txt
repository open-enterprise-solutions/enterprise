cmake_minimum_required(VERSION 3.16)

set(CODERUNNER_SOURCES
    coderunner.cpp
    mainApp.cpp
    outputMessage.cpp
    mainFrame/settings/editorsettings.cpp
    mainFrame/settings/fontcolorsettings.cpp
    win/dlgs/functionSearcher/functionSearcher.cpp
    win/dlgs/lineInput/lineInput.cpp
    win/editor/codeEditor/codeEditor.cpp
    win/editor/codeEditor/codeEditorLoader.cpp
    win/editor/codeEditor/components/autoComplete.cpp
    win/editor/codeEditor/components/callTip.cpp
    win/editor/codeEditor/components/helpers/listBoxVisualData.cpp
    win/editor/codeEditor/components/helpers/listBoxWnd.cpp
    win/editor/codeEditor/components/helpers/popupWnd.cpp
)

add_executable(coderunner ${CODERUNNER_SOURCES})

set_target_properties(coderunner PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(coderunner PROPERTIES WIN32_EXECUTABLE TRUE)
    # its resources - the icon wxICON(oes) loads - and the programs' manifest, as its project has them (coderunner.vcxproj)
    target_sources(coderunner PRIVATE coderunner.rc ${CMAKE_SOURCE_DIR}/src/engine/core/program.manifest)
endif()

target_compile_definitions(coderunner PRIVATE NOPCH)

target_include_directories(coderunner PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# THE CODE EDITOR IS ITS OWN — copied whole from the old desktop front, with the art, the settings and the two
# dialogs it opens (win/, mainFrame/settings/); the Scintilla host under it and the pictures are the desktop's user
# interface library's (deskgui/univ/stc, the theme's), as the launcher's windows are.
target_link_libraries(coderunner PRIVATE
    deskgui
    backend
    wx::core wx::base
)
