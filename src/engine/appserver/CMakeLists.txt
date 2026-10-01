cmake_minimum_required(VERSION 3.16)

set(APPSERVER_SOURCES
    main.cpp
    appServer.cpp
    serverConfig.cpp
)

add_executable(appserver ${APPSERVER_SOURCES})

set_target_properties(appserver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(appserver PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(appserver PRIVATE NOPCH)

target_include_directories(appserver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(appserver PRIVATE
    backend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)

# The program was called `daemon` until 2026-10-01. The nightly workflow is run from master, which still asks
# for `--target … daemon …`; this keeps that request building the application server until master moves.
add_custom_target(daemon DEPENDS appserver)
