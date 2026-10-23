# =============================================================================
# OES Enterprise — the application cluster
# The server folder served by application servers, one or more for each base (appCluster.h): it starts them and
# carries the clients' sockets on to them (clusterRoute.h) — by hand in a console, or as a service of the system
# (clusterService.h). It links the core alone — no backend, and of the protocol only its names (protocol/protocol.h,
# a header). Of wx — the base. Its tray is appcluster-tray.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(APPCLUSTER_SOURCES
    appCluster.cpp
    clusterRoute.cpp
    clusterService.cpp
    clusterWorker.cpp
)

add_executable(appcluster ${APPCLUSTER_SOURCES})

set_target_properties(appcluster PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(appcluster PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(appcluster PRIVATE NOPCH)
if(WIN32)
    target_compile_definitions(appcluster PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
endif()

target_include_directories(appcluster PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(appcluster PRIVATE
    core
    wx::base
)

find_package(Threads REQUIRED)
target_link_libraries(appcluster PRIVATE Threads::Threads)
