# The core library. Static for now (desktop tests link it in-process); a shared
# library exposing the C ABI comes with the packaging milestone (M6/M8).

# --- the embedded JS bundle header ---
# js/build.mjs emits js/dist/bundle.h (the bundle as base64 string literals);
# run `npm run build` in js/ first. Generation is done in Node (fast) and the
# base64-string form compiles quickly on MSVC. CI wiring comes in M8.
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/js/dist/bundle.h)
    message(FATAL_ERROR "twk: js/dist/bundle.h missing — run 'npm run build' in js/ first.")
endif()

add_library(twk STATIC
    engine/js_runtime.cpp
    engine/event_loop.cpp
    util/base64.cpp
    util/crypto.cpp
    shims/shims.cpp
    bridge/transport.cpp
    client.cpp
)

target_include_directories(twk
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../include      # public C ABI (twk.h)
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}                 # internal headers (engine/, bridge/, util/, client.h)
    PRIVATE ${CMAKE_SOURCE_DIR}/js/dist                 # generated bundle.h
)

# QuickJS is an internal implementation detail — linked PRIVATE so it does not
# leak through the public ABI. Internal tests link `qjs` directly when they need it.
target_link_libraries(twk PRIVATE qjs)

# Platform crypto for the RNG / PBKDF2 shims (M2 routes these through host delegates).
if(WIN32)
    target_link_libraries(twk PRIVATE bcrypt)
endif()
