# The core library. Static for now (desktop tests link it in-process); a shared
# library exposing the C ABI comes with the packaging milestone (M6/M8).

# --- embed the JS bundle -> generated header (stub in M0, esbuild output in M1+) ---
set(TWK_BUNDLE_IN  ${CMAKE_SOURCE_DIR}/js/src/stub-bundle.js)
set(TWK_BUNDLE_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/bundle.h)
add_custom_command(
    OUTPUT  ${TWK_BUNDLE_OUT}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${TWK_BUNDLE_IN} -DOUTPUT=${TWK_BUNDLE_OUT}
            -DSYMBOL=twk_bundle_js -P ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${TWK_BUNDLE_IN} ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
    COMMENT "Embedding JS bundle -> generated/bundle.h"
    VERBATIM
)

add_library(twk STATIC
    engine/js_runtime.cpp
    engine/event_loop.cpp
    shims/shims.cpp
    bridge/transport.cpp
    client.cpp
    ${TWK_BUNDLE_OUT}
)

target_include_directories(twk
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../include      # public C ABI (twk.h)
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}                 # internal headers (engine/, bridge/, client.h)
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated       # generated bundle.h
)

# QuickJS is an internal implementation detail — linked PRIVATE so it does not
# leak through the public ABI. Internal tests link `qjs` directly when they need it.
target_link_libraries(twk PRIVATE qjs)

# Platform crypto for the RNG / PBKDF2 shims (M2 routes these through host delegates).
if(WIN32)
    target_link_libraries(twk PRIVATE bcrypt)
endif()
