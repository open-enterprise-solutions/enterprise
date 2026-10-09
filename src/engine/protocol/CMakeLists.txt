# =============================================================================
# OES Enterprise — protocol shared library
# The protocol: its contract (protocol.h — the names and numbers, depending on nothing), which the server's forms
# include, and the library of the sides that talk it — a node of the wire, the connections to a base's clients (a file
# base opened in process, an application server over its port), the communicator and the protocol's journal — which
# the GUI client (frmclient) and the web server link. Of wx — the base only.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build: the project
# (protocol.vcxproj) lists the same set.
file(GLOB_RECURSE PROTOCOL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(protocol SHARED ${PROTOCOL_SOURCES})

target_include_directories(protocol
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/engine
)

target_compile_definitions(protocol
    PRIVATE
        NOPCH
)

# PROTOCOL_API resolves to dllexport only when PROTOCOL_EXPORTS is defined (protocolApi.h) — frontend's rule.
if(WIN32)
    target_compile_definitions(protocol PRIVATE PROTOCOL_EXPORTS)
endif()

# Nothing of the server: a side speaks the wire's text (its tree is nlohmann's, behind protocolNode.h), and a file base
# is opened by fileserver, LOADED when one is (connectionFile). The connection to a server is cpp-httplib's WebSocket
# client, compiled with the build's TLS — which the backend carries for everyone else, and this library, linking no
# backend, carries itself.
target_link_libraries(protocol
    PUBLIC
        # The core's journal — one per process, the client's and the protocol's lines alike (core/diagnostics).
        core
    PRIVATE
        wx::base
        mbedtls
        mbedx509
        mbedcrypto
)
if(WIN32)
    target_link_libraries(protocol PRIVATE crypt32 ws2_32)
endif()
if(APPLE)
    target_link_libraries(protocol PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

find_package(Threads REQUIRED)
target_link_libraries(protocol PRIVATE Threads::Threads)

# WHAT IT EXPORTS, AND NOTHING ELSE — as a DLL does: hidden by default, PROTOCOL_API shows what is meant to be seen;
# the static Mbed TLS is kept in as well (see frmclient/CMakeLists.txt).
set_target_properties(protocol PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "protocol"
)
if(UNIX AND NOT APPLE)
    target_link_options(protocol PRIVATE "LINKER:--exclude-libs,ALL")
endif()
