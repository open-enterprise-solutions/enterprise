cmake_minimum_required(VERSION 3.16)

# The thin designer — designer.exe as a thin client (mainApp.h says what it is). It links the thin client's core
# alone; a file base's server half (fileserver) is loaded by it when one is opened.
set(DESIGNER_THIN_SOURCES
    mainApp.cpp
    mainAppEvent.cpp
    mainFrame/mainFrameDesigner.cpp
)

add_executable(designer-thin ${DESIGNER_THIN_SOURCES})

set_target_properties(designer-thin PROPERTIES
    CXX_STANDARD 17
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
    wx::core wx::aui wx::stc wx::adv wx::base
)
