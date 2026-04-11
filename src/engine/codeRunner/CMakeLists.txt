cmake_minimum_required(VERSION 3.16)

set(CODERUNNER_SOURCES
    codeRunner.cpp
    mainApp.cpp
    outputMessage.cpp
)

add_executable(codeRunner ${CODERUNNER_SOURCES})

set_target_properties(codeRunner PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(codeRunner PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_compile_definitions(codeRunner PRIVATE NOPCH)

target_include_directories(codeRunner PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(codeRunner PRIVATE
    backend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
