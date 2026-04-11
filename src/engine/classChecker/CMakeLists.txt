cmake_minimum_required(VERSION 3.16)

set(CLASSCHECKER_SOURCES
    classCheckerWnd.cpp
    mainApp.cpp
)

add_executable(classChecker ${CLASSCHECKER_SOURCES})

set_target_properties(classChecker PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(classChecker PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_compile_definitions(classChecker PRIVATE NOPCH)

target_include_directories(classChecker PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(classChecker PRIVATE
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
