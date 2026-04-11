cmake_minimum_required(VERSION 3.16)

set(SIMPLEPLUGIN_SOURCES
    simplePluginDLL.cpp
)

add_library(simplePlugin SHARED ${SIMPLEPLUGIN_SOURCES})

set_target_properties(simplePlugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
)

target_compile_definitions(simplePlugin PRIVATE NOPCH)

target_include_directories(simplePlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(simplePlugin PRIVATE
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
