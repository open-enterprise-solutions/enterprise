cmake_minimum_required(VERSION 3.16)

# aiBridge — first-party ABI v4 plugin. Real LLM round-trip via cpp-httplib
# + OpenSSL (when present). Without OpenSSL only http:// endpoints work,
# which is enough for local LLM servers (ollama, llama.cpp, lm-studio).

set(AIBRIDGE_SOURCES
    aiBridgeDLL.cpp
)

add_library(aiBridge SHARED ${AIBRIDGE_SOURCES})

set_target_properties(aiBridge PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
)

target_compile_definitions(aiBridge PRIVATE NOPCH)

target_include_directories(aiBridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/backend
    ${CMAKE_SOURCE_DIR}/src/3rdparty/cpp-httplib
    ${CMAKE_SOURCE_DIR}/src/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# OpenSSL — optional. When present cpp-httplib gains https://. Without it
# only http:// endpoints work (local LLMs).
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "aiBridge: OpenSSL found — HTTPS enabled")
    target_compile_definitions(aiBridge PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(aiBridge PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    if(APPLE)
        # OpenSSL pulls in SecTrust* + CFArrayGetCount via its trust-
        # anchor-from-keychain support. Link the system frameworks they
        # live in.
        target_link_libraries(aiBridge PRIVATE
            "-framework CoreFoundation"
            "-framework Security")
    endif()
else()
    message(STATUS "aiBridge: no OpenSSL — only http:// endpoints will work")
endif()

# Threads — cpp-httplib spawns worker pools internally; our chat-request
# code also uses std::thread directly.
find_package(Threads REQUIRED)
target_link_libraries(aiBridge PRIVATE Threads::Threads)
