# The core library. Static for now (desktop tests link it in-process); a shared
# library exposing the C ABI comes with the packaging milestone (M6/M8).

# --- the embedded JS bundle ---
# js/build.mjs emits js/dist/bundle.{js,h}; run `npm run build` in js/ first.
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/js/dist/bundle.h)
    message(FATAL_ERROR "twk: js/dist/bundle.h missing — run 'npm run build' in js/ first.")
endif()

# Precompile the bundle to QuickJS bytecode so clients don't re-parse ~1.7MB of
# JS on startup. Requires running twk-bundlec, which is built for the *target*;
# when cross-compiling it can't run here, so fall back to embedding the source.
if(CMAKE_CROSSCOMPILING)
    message(STATUS "twk: cross-compiling — embedding JS source (bytecode needs a host-native twk-bundlec)")
    set(TWK_USE_BYTECODE OFF)
else()
    set(TWK_USE_BYTECODE ON)
    set(TWK_BC_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/bundle_bc.h)
    add_custom_command(
        OUTPUT  ${TWK_BC_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND twk-bundlec ${CMAKE_SOURCE_DIR}/js/dist/bundle.js ${TWK_BC_HEADER} twk_bundle_bc
        DEPENDS twk-bundlec ${CMAKE_SOURCE_DIR}/js/dist/bundle.js
        COMMENT "Compiling JS bundle -> QuickJS bytecode"
        VERBATIM
    )
    add_custom_target(twk_bundle_bytecode DEPENDS ${TWK_BC_HEADER})
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
    PRIVATE ${CMAKE_SOURCE_DIR}/js/dist                 # generated bundle.h (source form)
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated       # generated bundle_bc.h (bytecode form)
)

if(TWK_USE_BYTECODE)
    add_dependencies(twk twk_bundle_bytecode)
    target_compile_definitions(twk PRIVATE TWK_BUNDLE_BYTECODE=1)
endif()

# QuickJS is an internal implementation detail — linked PRIVATE so it does not
# leak through the public ABI. Internal tests link `qjs` directly when they need it.
target_link_libraries(twk PRIVATE qjs)

# Platform crypto for the RNG / PBKDF2 shims (M2 routes these through host delegates).
if(WIN32)
    target_link_libraries(twk PRIVATE bcrypt)
endif()
