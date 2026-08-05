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

# NO `backend` HERE, ON PURPOSE — and this target is the test that the plugin
# boundary still holds. A plugin reaches the host through the capability vtable
# it is handed in oes_plugin_init (backend/plugin/pluginApi.h), never through
# our symbols; linking backend's import library would make that boundary
# decorative and tie every plugin to one build of backend.dll.
#
# So an unresolved external from this target is NOT a missing link line: it
# means something a plugin must derive from or instantiate got marked
# BACKEND_API. Fix it at the declaration (drop the export attribute), the way
# ibDiagnosticSink was fixed after it broke this link on 2026-08-05.
target_link_libraries(simplePlugin PRIVATE
    wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa wx::base wx::net wx::xml
)
