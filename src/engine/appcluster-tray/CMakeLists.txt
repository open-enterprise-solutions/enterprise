# =============================================================================
# OES Enterprise — the cluster's tray
# The monitor of the application cluster (appClusterTray.h): it shows what the cluster holds and steers it over its
# control socket, and starts it when it is not running. It links the core alone. Of wx — the base and the core (the
# tray icon).
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(APPCLUSTER_TRAY_SOURCES
    appClusterTray.cpp
    clusterTray.cpp
)

add_executable(appcluster-tray ${APPCLUSTER_TRAY_SOURCES})

set_target_properties(appcluster-tray PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(appcluster-tray PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_compile_definitions(appcluster-tray PRIVATE NOPCH)
if(WIN32)
    target_compile_definitions(appcluster-tray PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
endif()

target_include_directories(appcluster-tray PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(appcluster-tray PRIVATE
    core
    wx::core wx::base
)

find_package(Threads REQUIRED)
target_link_libraries(appcluster-tray PRIVATE Threads::Threads)
