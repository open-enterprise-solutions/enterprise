cmake_minimum_required(VERSION 3.16)

# oes-rag-local — local RAG (retrieval-augmented generation) fallback for
# oes-mcp. Standalone binary providing brute-force lexical retrieval over
# OES/BAS XML corpora + Ollama HTTP proxy, used when Pugi cloud is
# unreachable (air-gapped enterprise tier: banks, government, closed
# networks).
#
# v1 scope (this scaffold):
#   - cpp-httplib client + server (no GUI, no backend.dll link)
#   - JSON over HTTP
#   - Brute-force substring lexical retrieval against pre-ingested chunk
#     JSON files (no FAISS yet, no real embeddings yet)
#   - Subcommands: ingest, serve, health, --help
#
# v2 deferred:
#   - Real FAISS index (vendor faiss-cpp)
#   - Real Ollama embedding round-trips during ingest
#   - Streaming chat responses
#   - Multi-corpus indexing
#
# No wxWidgets — this binary is intentionally GUI-free and backend-free
# so air-gapped deployments can ship it without the full OES runtime.

set(OES_RAG_LOCAL_SOURCES
    main.cpp
    ollamaClient.cpp
    ragIndex.cpp
    ingestPipeline.cpp
    queryServer.cpp
)

add_executable(oes-rag-local ${OES_RAG_LOCAL_SOURCES})

set_target_properties(oes-rag-local PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Console subsystem on Windows — stdio + HTTP server both need it.
if(WIN32)
    set_target_properties(oes-rag-local PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_compile_definitions(oes-rag-local PRIVATE NOPCH)

target_include_directories(oes-rag-local PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/3rdparty
    ${CMAKE_SOURCE_DIR}/src/3rdparty/cpp-httplib
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# OpenSSL — optional. Ollama defaults to plain HTTP on localhost:11434, so
# OpenSSL is not strictly required. If found, we enable it so users with
# remote / TLS-fronted Ollama deployments can point at https:// hosts.
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "oes-rag-local: OpenSSL found — TLS Ollama endpoints supported")
    target_compile_definitions(oes-rag-local PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(oes-rag-local PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    if(APPLE)
        target_link_libraries(oes-rag-local PRIVATE
            "-framework CoreFoundation"
            "-framework Security")
    endif()
else()
    message(STATUS "oes-rag-local: no OpenSSL — only plain http:// endpoints reachable")
endif()

find_package(Threads REQUIRED)
target_link_libraries(oes-rag-local PRIVATE Threads::Threads)

include(GNUInstallDirs)
install(TARGETS oes-rag-local
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
