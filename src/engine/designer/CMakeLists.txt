cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE DESIGNER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Exclude stale root-level files superseded by mainFrame/ versions
list(REMOVE_ITEM DESIGNER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/mainFrameDesignerCmd.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mainFrameDesignerEvent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mainFrameDesignerMenu.cpp"
)

add_executable(designer ${DESIGNER_SOURCES})

set_target_properties(designer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(designer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(designer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
    set(OES_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/oes.icns")
    set_source_files_properties(${OES_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(designer PRIVATE ${OES_ICON})
endif()

target_compile_definitions(designer PRIVATE NOPCH)

target_include_directories(designer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_SOURCE_DIR}/src/engine/frontend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(designer PRIVATE
    backend
    frontend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
