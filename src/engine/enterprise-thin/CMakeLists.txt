cmake_minimum_required(VERSION 3.16)

# The thin runtime — enterprise.exe as a thin client (mainApp.h says what it is). It links the thin client's core
# alone; a file base's server half (fileserver) is loaded by it when one is opened.
set(ENTERPRISE_THIN_SOURCES
    mainApp.cpp
    mainAppEvent.cpp
    mainFrame/mainFrameEnterprise.cpp
    mainFrame/mainFrameEnterpriseInterface.cpp
    win/dlg/functionAll.cpp
)

add_executable(enterprise-thin ${ENTERPRISE_THIN_SOURCES})

set_target_properties(enterprise-thin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(enterprise-thin PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(enterprise-thin PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
    set(OES_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/oes.icns")
    set_source_files_properties(${OES_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(enterprise-thin PRIVATE ${OES_ICON})
endif()

target_compile_definitions(enterprise-thin PRIVATE NOPCH)

target_include_directories(enterprise-thin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(enterprise-thin PRIVATE
    protocol
    frmclient
    wx::core wx::aui wx::stc wx::adv wx::base
)
