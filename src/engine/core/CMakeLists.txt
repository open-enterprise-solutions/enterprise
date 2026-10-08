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

target_include_directories(core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/engine
)

target_compile_definitions(core
    PRIVATE
        NOPCH
)

# CORE_API resolves to dllexport only when CORE_EXPORTS is defined (core.h) — frontend's rule.
if(WIN32)
    target_compile_definitions(core PRIVATE CORE_EXPORTS)
endif()

# wx::base PUBLIC: the core's headers hand wxString across (ibString's boundary, a guid's text).
target_link_libraries(core
    PUBLIC
        wx::base
)

# guid.cpp picks its generator by toolkit: Windows -> CoCreateGuid, macOS -> CFUUID
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
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "core"
)
