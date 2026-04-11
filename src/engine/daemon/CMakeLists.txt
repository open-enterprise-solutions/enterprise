cmake_minimum_required(VERSION 3.16)

set(DAEMON_SOURCES
    daemon.cpp
)

add_executable(daemon ${DAEMON_SOURCES})

set_target_properties(daemon PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(daemon PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(daemon PRIVATE NOPCH)

target_include_directories(daemon PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(daemon PRIVATE
    backend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
