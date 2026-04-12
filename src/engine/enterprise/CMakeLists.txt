cmake_minimum_required(VERSION 3.16)

set(ENTERPRISE_SOURCES
    docManager/docManager.cpp
    docManager/templates/docViewDataProcessorFile.cpp
    docManager/templates/docViewDataReportFile.cpp
    mainApp.cpp
    mainAppEvent.cpp
    mainFrame/mainFrameEnterprise.cpp
    mainFrame/mainFrameEnterpriseParts.cpp
    mainFrame/mainFrameEnterpriseEvent.cpp
    mainFrame/mainFrameEnterpriseMenu.cpp
    mainFrame/mainFrameEnterpriseInterface.cpp
    mainFrame/output/outputWindow.cpp
    win/dlg/enterpriseOption.cpp
    win/dlg/functionAll.cpp
)

add_executable(enterprise ${ENTERPRISE_SOURCES})

set_target_properties(enterprise PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(enterprise PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(enterprise PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
    set(OES_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/oes.icns")
    set_source_files_properties(${OES_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(enterprise PRIVATE ${OES_ICON})
endif()

target_compile_definitions(enterprise PRIVATE NOPCH)

target_include_directories(enterprise PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_SOURCE_DIR}/src/engine/frontend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(enterprise PRIVATE
    backend
    frontend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
