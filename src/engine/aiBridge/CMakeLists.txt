cmake_minimum_required(VERSION 3.16)

# aiBridge — first-party ABI v4 plugin. Real LLM round-trip via cpp-httplib
# + OpenSSL (when present). Without OpenSSL only http:// endpoints work,
# which is enough for local LLM servers (ollama, llama.cpp, lm-studio).

set(AIBRIDGE_SOURCES
    aiBridgeDLL.cpp
)

# MODULE (not SHARED) so macOS gets a .bundle extension matching what
# wxDynamicLibrary::GetDllExt(wxDL_MODULE) returns — that's the file
# pattern ibPluginManager::LoadAll scans for.
add_library(aiBridge MODULE ${AIBRIDGE_SOURCES})

set_target_properties(aiBridge PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    PREFIX "lib"
    SUFFIX ".bundle"
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

# Copy sample.html next to the .bundle (root CMakeLists.txt forces our
# LIBRARY_OUTPUT_DIRECTORY back to ${CMAKE_BINARY_DIR}/bin/${CONFIG}/, so
# the .bundle lands there alongside designer/launcher binaries). Then
# also stage both files into designer.app/Contents/MacOS/plugins/ on
# macOS so Designer's plugin scanner — which reads from <exe>/plugins/
# — actually finds the freshly built plugin. Without this step a stale
# copy from a previous build keeps loading.
set(AIBRIDGE_SAMPLE_HTML "${CMAKE_SOURCE_DIR}/assets/pluginWebPane/sample.html")
if(EXISTS "${AIBRIDGE_SAMPLE_HTML}")
    add_custom_command(TARGET aiBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AIBRIDGE_SAMPLE_HTML}"
            "$<TARGET_FILE_DIR:aiBridge>/sample.html"
        COMMENT "aiBridge: stage sample.html next to plugin binary"
        VERBATIM)
else()
    message(WARNING "aiBridge: ${AIBRIDGE_SAMPLE_HTML} not found — pane will lack UI")
endif()

# On macOS the designer target builds a .app bundle. Mirror the plugin
# binary + asset into its plugins/ subdir so a fresh `cmake --build` puts
# the just-compiled .bundle where the loader actually scans. depends-on
# designer so the .app directory tree exists before we copy into it.
if(APPLE AND TARGET designer)
    add_custom_command(TARGET aiBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:designer>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:aiBridge>"
            "$<TARGET_FILE_DIR:designer>/plugins/$<TARGET_FILE_NAME:aiBridge>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AIBRIDGE_SAMPLE_HTML}"
            "$<TARGET_FILE_DIR:designer>/plugins/sample.html"
        COMMENT "aiBridge: install into designer.app/Contents/MacOS/plugins/"
        VERBATIM)
endif()
