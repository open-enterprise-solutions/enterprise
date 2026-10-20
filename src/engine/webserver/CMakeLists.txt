# =============================================================================
# OES Enterprise — the web server
# The bases' transport for browsers (webServer.h): over protocol, linking no backend — a file base's engine is
# fileserver, loaded when a publication opens one. Its HTTP is its own (http/): Asio, llhttp, Mbed TLS. Of wx — the
# base and its sockets (the engine's debug server, in this process when a file base is debugged).
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(WEBSERVER_SOURCES
    main.cpp
    webServer.cpp
    http/asio.cpp
    http/httpClient.cpp
    http/httpServer.cpp
    http/httpStream.cpp
    http/tlsStream.cpp
    http/webSocketFrame.cpp
)

add_executable(webserver ${WEBSERVER_SOURCES})

set_target_properties(webserver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32)
    set_target_properties(webserver PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

# Asio compiled once, in http/asio.cpp: every other file only declares what the header-only library would define in
# each. No Boost.
target_compile_definitions(webserver PRIVATE NOPCH ASIO_STANDALONE ASIO_SEPARATE_COMPILATION ASIO_NO_DEPRECATED)
if(WIN32)
    target_compile_definitions(webserver PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
endif()

target_include_directories(webserver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OES_3RDPARTY_DIR}/asio/asio/include
)

target_link_libraries(webserver PRIVATE
    protocol
    core
    llhttp
    mbedtls
    mbedx509
    mbedcrypto
    wx::base wx::net
)
if(WIN32)
    target_link_libraries(webserver PRIVATE ws2_32 crypt32 bcrypt)
endif()
if(APPLE)
    target_link_libraries(webserver PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

find_package(Threads REQUIRED)
target_link_libraries(webserver PRIVATE Threads::Threads)
