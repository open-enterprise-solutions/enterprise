# =============================================================================
# OES Enterprise — frmclient shared library
# The thin client's core, shared by the thin runtime and the thin designer: the communicator that keeps the frame, the
# main window and what is drawn in it, over the protocol's library (protocol/: the connections to a base's clients).
# =============================================================================

# CONFIGURE_DEPENDS — see the note in backend/CMakeLists.txt.
file(GLOB_RECURSE FRMCLIENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(frmclient SHARED ${FRMCLIENT_SOURCES})

if(APPLE)
    target_sources(frmclient PRIVATE mainFrame/mainFrameMac.mm)
    target_link_libraries(frmclient PRIVATE "-framework AppKit")
endif()

target_include_directories(frmclient
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
)

target_compile_definitions(frmclient
    PRIVATE
        NOPCH
)

# FRMCLIENT_API resolves to dllexport only when FRMCLIENT_EXPORTS is defined (frmclient.h).
if(WIN32)
    target_compile_definitions(frmclient PRIVATE FRMCLIENT_EXPORTS)
endif()

# Nothing of the server — not even the backend: the client talks through the protocol's library (protocol/), which
# carries the wire's tree and the connections; it speaks the core's meanings (core/), as the engine does; and its
# windows are drawn with the desktop's user interface library (gui/) - public, as the thin designer draws with it too.
target_link_libraries(frmclient
    PUBLIC
        protocol
        core
        gui
    PRIVATE
        wx::core
        wx::aui
        wx::html
        wx::propgrid
        wx::stc
        wx::base
        wx::xml
)

find_package(Threads REQUIRED)
target_link_libraries(frmclient PRIVATE Threads::Threads)

# WHAT IT EXPORTS, AND NOTHING ELSE — as a DLL does. A file base loads the backend into this process, and on an ELF
# system a name two libraries export is ONE: the backend's own calls would land in this library's copies (the model
# view, wx's event names). Hidden by default, FRMCLIENT_API shows what is meant to be seen.
set_target_properties(frmclient PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME "frmclient"
)
