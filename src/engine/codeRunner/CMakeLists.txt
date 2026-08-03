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
    ${CMAKE_SOURCE_DIR}/src/engine/frontend
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# frontend, like designer and enterprise: codeRunner hosts the FULL code editor
# (ibCodeEditor, plus the editor/font-colour settings types), which lives there —
# codeRunner.cpp includes frontend/ headers directly. The .vcxproj has always
# linked frontend.lib; this file had not, so the CMake build produced 21
# unresolved externals the moment anything actually tried to link it. Nothing
# did until CI started building the applications (2026-08-03).
target_link_libraries(codeRunner PRIVATE
    backend
    frontend
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
