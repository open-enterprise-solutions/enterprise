# =============================================================================
# OES Enterprise — core shared library
# The shared meanings under the engine (backend) and its clients (frmclient): the class ids and the ids of the
# configuration, the kinds of a value, a text, a number, a date, a guid, a block of bytes read and written, the core's
# refusal, the build's number. Of wx — the base only.
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt. Every source here is in the build: the project
# (core.vcxproj) lists the same set.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(core SHARED ${CORE_SOURCES})

# The fiber switch (fiber/) is a few instructions with a fixed frame. It lives in its own file so the C++ around it
# cannot reorder the saves. Windows uses CreateFiberEx and does not compile these.
if(NOT WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        enable_language(ASM)
        target_sources(core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/fiber/fiber_x64.S")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        enable_language(ASM)
        target_sources(core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/fiber/fiber_arm64.S")
    else()
        message(FATAL_ERROR "ibFiber has no stack switcher for ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

target_include_directories(core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/engine
)

target_compile_definitions(core
    PRIVATE
        NOPCH
        wxUSE_GUI=0   # of wx the base only — a GUI type here is a compile error
    # wx's _() stays off for every target that links core: the engine's _() is core/localization/dictionary.h,
    # and a unit that reaches for _() without it fails to compile rather than translating through wx unseen.
    PUBLIC
        WXINTL_NO_GETTEXT_MACRO
)

# THE NETWORK (net/) — HTTP and WebSocket on Asio, llhttp and Mbed TLS. Asio compiled once, in net/asio.cpp: every other
# file only declares what the header-only library would define in each. No Boost. All of it PRIVATE: no header of
# net/ includes Asio, since what its headers make in every file that includes them is made here only.
target_compile_definitions(core PRIVATE ASIO_STANDALONE ASIO_SEPARATE_COMPILATION ASIO_NO_DEPRECATED)
if(WIN32)
    file(GLOB CORE_NET_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp")
    set_source_files_properties(${CORE_NET_SOURCES} PROPERTIES COMPILE_DEFINITIONS WIN32_LEAN_AND_MEAN)
endif()
target_include_directories(core PRIVATE ${OES_3RDPARTY_DIR}/asio/asio/include)
# MBED TLS IS THE CORE'S (core/tls declares it): a random byte, a cipher, TLS — whatever else needs one asks the core
# (crypto/, net/). PRIVATE and hidden (the root CMakeLists.txt), so nothing reaches it past the core.
target_link_libraries(core PRIVATE llhttp lodepng mbedtls mbedx509 mbedcrypto)
if(WIN32)
    # crypt32 — the system's certificate store, a client's trust when it names none (net/tlsStream.cpp).
    target_link_libraries(core PRIVATE ws2_32 crypt32 bcrypt)
endif()
if(APPLE)
    # …and the keychain's (SecTrustSettingsCopyCertificates); CoreFoundation is linked below, for the guid.
    target_link_libraries(core PRIVATE "-framework Security")
endif()

# CORE_API resolves to dllexport only when CORE_EXPORTS is defined (coreApi.h).
if(WIN32)
    target_compile_definitions(core PRIVATE CORE_EXPORTS)
endif()

# wx::base PUBLIC: the core's headers hand wx types across - ibString's boundary (fstring.h), the tabular model's wxRefCounter.
target_link_libraries(core
    PUBLIC
        wx::base
)

# pthread_getattr_np (fiber.cpp, the thread-stack bound). On Windows this is empty.
find_package(Threads REQUIRED)
target_link_libraries(core PRIVATE Threads::Threads)

# dlopen / dlsym (dynamicLibrary.cpp, a database's client loaded at run time). On Windows this is empty.
target_link_libraries(core PRIVATE ${CMAKE_DL_LIBS})

# guid.cpp picks its generator by system: Windows -> CoCreateGuid, macOS -> CFUUID
# (CoreFoundation), everything else -> libuuid's uuid_generate. Only the last one is a
# separate library, and only on non-Apple UNIX; Apple's CFUUID rides in CoreFoundation.
if(UNIX)
    if(NOT APPLE)
        find_library(UUID_LIBRARY NAMES uuid)
        if(UUID_LIBRARY)
            target_link_libraries(core PRIVATE ${UUID_LIBRARY})
        else()
            message(FATAL_ERROR
                "libuuid not found. guid.cpp uses uuid_generate on this platform — "
                "install uuid-dev (Debian/Ubuntu) or libuuid-devel (Fedora/RHEL).")
        endif()
    else()
        target_link_libraries(core PRIVATE "-framework CoreFoundation")
    endif()
endif()

# WHAT IT EXPORTS, AND NOTHING ELSE — as a DLL does: hidden by default, CORE_API shows what is meant to be seen.
set_target_properties(core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "core"
)
