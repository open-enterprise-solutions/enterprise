cmake_minimum_required(VERSION 3.16)

# dumpHelp — CLI tool used by tools/help-skeleton.py (Phase 2) to
# enumerate built-in keywords and system functions as JSON. No GUI, no
# database; just walks ibValueSystemFunction and s_listKeyWord via the
# extern "C" hook ibDumpKeyWords.

set(DUMPHELP_SOURCES
    dumpHelp.cpp
)

add_executable(dumpHelp ${DUMPHELP_SOURCES})

set_target_properties(dumpHelp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Console tool on every platform — no WIN32_EXECUTABLE flag so Windows
# builds stay as console apps (stdout used for JSON).

target_compile_definitions(dumpHelp PRIVATE NOPCH)

target_include_directories(dumpHelp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dumpHelp PRIVATE
    backend
    wx::base
    wx::net
    wx::xml
)
