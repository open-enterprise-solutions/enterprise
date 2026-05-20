# md4c — vendored CommonMark markdown→HTML parser.
# Upstream: https://github.com/mity/md4c  (MIT-equivalent ZLIB license)
# Used by frontend's ibPluginNativePane to render assistant replies.
#
# Two libraries:
#   md4c       — parses Markdown into a callback stream (parser core)
#   md4c-html  — sink that emits HTML; depends on md4c + entity table
#
# Both compile as C99. Exposed to the rest of the project via
# target_include_directories on the top-level md4c target.

add_library(md4c STATIC
    md4c.c
    md4c.h
)
target_include_directories(md4c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(md4c PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

add_library(md4c-html STATIC
    md4c-html.c
    md4c-html.h
    entity.c
    entity.h
)
target_link_libraries(md4c-html PUBLIC md4c)
set_target_properties(md4c-html PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)
