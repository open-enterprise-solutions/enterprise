cmake_minimum_required(VERSION 3.16)

# The thin runtime — appworkspace.exe, the runtime as a thin client (mainApp.h says what it is). It links the thin client's core
# alone; a file base's server half (fileserver) is loaded by it when one is opened.
set(ENTERPRISE_THIN_SOURCES
    mainApp.cpp
    mainAppEvent.cpp
    mainFrame/mainFrameEnterprise.cpp
    mainFrame/mainFrameEnterpriseInterface.cpp
    win/dlg/functionAll.cpp
)

add_executable(appworkspace ${ENTERPRISE_THIN_SOURCES})

set_target_properties(appworkspace PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(appworkspace PROPERTIES WIN32_EXECUTABLE TRUE)
    # its resources - the icon wxICON(oes) loads - and the programs' manifest, as its project has them (appworkspace.vcxproj)
    target_sources(appworkspace PRIVATE appworkspace.rc ${CMAKE_SOURCE_DIR}/src/engine/core/program.manifest)
endif()

if(APPLE)
    set_target_properties(appworkspace PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
    set(OES_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/oes.icns")
    set_source_files_properties(${OES_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(appworkspace PRIVATE ${OES_ICON})
endif()

target_compile_definitions(appworkspace PRIVATE NOPCH)

target_include_directories(appworkspace PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(appworkspace PRIVATE
    protocol
    frmclient
    wx::core wx::base
)
