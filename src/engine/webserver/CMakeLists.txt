# =============================================================================
# OES Enterprise — the web server
# The bases' transport for browsers (webServer.h): over protocol, linking no backend — a file base's engine is
# fileserver, loaded when a publication opens one. Its HTTP is the core's (core/net): Asio, llhttp, Mbed TLS. Of wx —
# the base only.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(WEBSERVER_SOURCES
    main.cpp
    webServer.cpp
)

add_executable(webserver ${WEBSERVER_SOURCES})

set_target_properties(webserver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(webserver PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(webserver PRIVATE NOPCH)
if(WIN32)
    target_compile_definitions(webserver PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
endif()

target_include_directories(webserver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(webserver PRIVATE
    protocol
    core
    wx::base
)
if(WIN32)
    target_link_libraries(webserver PRIVATE ws2_32)
endif()
if(APPLE)
    target_link_libraries(webserver PRIVATE "-framework CoreFoundation")
endif()

find_package(Threads REQUIRED)
target_link_libraries(webserver PRIVATE Threads::Threads)
