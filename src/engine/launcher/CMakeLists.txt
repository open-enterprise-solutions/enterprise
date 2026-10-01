cmake_minimum_required(VERSION 3.16)

set(LAUNCHER_SOURCES
    connectionDB.cpp
    launcher.cpp
    launcher_res.cpp
    mainApp.cpp
)

add_executable(launcher ${LAUNCHER_SOURCES})

set_target_properties(launcher PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(launcher PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# A BUNDLE OF ITS OWN, like the designer's and the enterprise's: CMake's default Info.plist leaves the bundle
# identifier EMPTY, and the launcher was the only one of the three apps built that way.
if(APPLE)
    set_target_properties(launcher PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    )
    # The platform's one icon, the file the designer's bundle carries.
    set(OES_ICON "${CMAKE_SOURCE_DIR}/src/engine/designer/resources/oes.icns")
    set_source_files_properties(${OES_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(launcher PRIVATE ${OES_ICON})
endif()

target_compile_definitions(launcher PRIVATE NOPCH)

target_include_directories(launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(launcher PRIVATE
    backend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)

# Copy Firebird client library next to binaries on macOS
if(APPLE AND OES_USE_FIREBIRD AND FIREBIRD_LIBRARY)
    add_custom_command(TARGET launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FIREBIRD_LIBRARY}"
            "$<TARGET_FILE_DIR:launcher>/libfbclient.dylib"
        COMMENT "Copying libfbclient.dylib to output directory"
    )
endif()
