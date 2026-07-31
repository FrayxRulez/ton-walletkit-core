# The core library. Static for now (desktop tests link it in-process); a shared
# library exposing the C ABI comes with the packaging milestone (M6/M8).
add_library(twk STATIC
    engine/js_runtime.cpp
    engine/event_loop.cpp
    client.cpp
)

target_include_directories(twk
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../include   # public C ABI (twk.h, added in a later task)
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}              # internal headers (engine/, bridge/, shims/)
)

# QuickJS is an internal implementation detail — linked PRIVATE so it does not
# leak through the public ABI. Internal tests link `qjs` directly when they need it.
target_link_libraries(twk PRIVATE qjs)
