# The core library. Static for now (desktop tests link it in-process); a shared
# library exposing the C ABI comes with the packaging milestone (M6/M8).

# --- the embedded JS bundle ---
# js/build.mjs emits js/dist/bundle.{js,h}; run `npm run build` in js/ first.
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/js/dist/bundle.h)
    message(FATAL_ERROR "twk: js/dist/bundle.h missing — run 'npm run build' in js/ first.")
endif()

# Precompile the bundle to QuickJS bytecode so clients don't re-parse ~1.7MB of
# JS on startup (1380 -> 1100 ms). twk-bundlec is built for the *target*, so on a
# cross build it cannot run here: point TWK_BUNDLEC at a host-native one (the
# arm64 build reuses the x64 build's), or fall back to embedding the source.
#
# MSVC builds arm64 from an x64 host without setting CMAKE_CROSSCOMPILING, and
# leaves CMAKE_SYSTEM_PROCESSOR at the host's value — so the *compiler's* target
# is what to compare against.
function(twk_normalize_arch value out)
    string(TOLOWER "${value}" lowered)
    if(lowered MATCHES "^(amd64|x86_64|x64)$")
        set(${out} "x64" PARENT_SCOPE)
    elseif(lowered MATCHES "^(arm64|aarch64)$")
        set(${out} "arm64" PARENT_SCOPE)
    else()
        set(${out} "${lowered}" PARENT_SCOPE)
    endif()
endfunction()

if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    twk_normalize_arch("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" TWK_TARGET_ARCH)
else()
    twk_normalize_arch("${CMAKE_SYSTEM_PROCESSOR}" TWK_TARGET_ARCH)
endif()
twk_normalize_arch("${CMAKE_HOST_SYSTEM_PROCESSOR}" TWK_HOST_ARCH)
if(CMAKE_CROSSCOMPILING OR NOT TWK_TARGET_ARCH STREQUAL TWK_HOST_ARCH)
    set(TWK_CROSS_BUILD ON)
else()
    set(TWK_CROSS_BUILD OFF)
endif()

set(TWK_BUNDLEC "" CACHE FILEPATH "Host-native twk-bundlec, for cross builds")

set(TWK_BC_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/bundle_bc.h)
set(TWK_BUNDLEC_COMMAND "")
set(TWK_BUNDLEC_DEPENDS "")

if(NOT TWK_CROSS_BUILD)
    set(TWK_BUNDLEC_COMMAND twk-bundlec)
    set(TWK_BUNDLEC_DEPENDS twk-bundlec)
elseif(TWK_BUNDLEC AND EXISTS ${TWK_BUNDLEC})
    message(STATUS "twk: cross build (${TWK_HOST_ARCH} -> ${TWK_TARGET_ARCH}) using host twk-bundlec: ${TWK_BUNDLEC}")
    set(TWK_BUNDLEC_COMMAND ${TWK_BUNDLEC})
    set(TWK_BUNDLEC_DEPENDS ${TWK_BUNDLEC})
else()
    message(STATUS "twk: cross build (${TWK_HOST_ARCH} -> ${TWK_TARGET_ARCH}) - embedding JS source; "
                   "pass -DTWK_BUNDLEC=<host twk-bundlec> for bytecode")
endif()

if(TWK_BUNDLEC_COMMAND)
    set(TWK_USE_BYTECODE ON)
    add_custom_command(
        OUTPUT  ${TWK_BC_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND ${TWK_BUNDLEC_COMMAND} ${CMAKE_SOURCE_DIR}/js/dist/bundle.js ${TWK_BC_HEADER} twk_bundle_bc
        DEPENDS ${TWK_BUNDLEC_DEPENDS} ${CMAKE_SOURCE_DIR}/js/dist/bundle.js
        COMMENT "Compiling JS bundle -> QuickJS bytecode"
        VERBATIM
    )
    add_custom_target(twk_bundle_bytecode DEPENDS ${TWK_BC_HEADER})
else()
    set(TWK_USE_BYTECODE OFF)
endif()

# --- platform crypto backend ---
# The system implementation wherever there is one worth having, measured rather
# than assumed: BCrypt beats the portable code ~6x at PBKDF2 (0.63 vs 3.87 ms per
# 390-iteration derivation), which is the primitive the mnemonic loop leans on.
#
#   bcrypt        Windows, in-box
#   commoncrypto  macOS / iOS, in libSystem
#   openssl       when CMake finds it (Linux desktop, Telegram Desktop)
#   portable      everywhere else — notably Android, whose NDK exposes no stable
#                 crypto C API, and any platform where OpenSSL is not a given
#
# Force one with -DTWK_CRYPTO_BACKEND=<name>; twk_crypto_kat must pass whichever
# is selected, because a backend that is fast and wrong produces wrong keys.
set(TWK_CRYPTO_BACKEND "auto" CACHE STRING "Crypto backend: auto | bcrypt | commoncrypto | openssl | portable")
set_property(CACHE TWK_CRYPTO_BACKEND PROPERTY STRINGS auto bcrypt commoncrypto openssl portable)

if(TWK_CRYPTO_BACKEND STREQUAL "auto")
    if(WIN32)
        set(TWK_CRYPTO_BACKEND_RESOLVED bcrypt)
    elseif(APPLE)
        set(TWK_CRYPTO_BACKEND_RESOLVED commoncrypto)
    elseif(ANDROID)
        # The NDK ships no public crypto headers; linking the system BoringSSL is
        # unsupported and breaks between OS versions.
        set(TWK_CRYPTO_BACKEND_RESOLVED portable)
    else()
        find_package(OpenSSL QUIET COMPONENTS Crypto)
        if(OPENSSL_FOUND)
            set(TWK_CRYPTO_BACKEND_RESOLVED openssl)
        else()
            set(TWK_CRYPTO_BACKEND_RESOLVED portable)
        endif()
    endif()
else()
    set(TWK_CRYPTO_BACKEND_RESOLVED ${TWK_CRYPTO_BACKEND})
endif()

if(TWK_CRYPTO_BACKEND_RESOLVED STREQUAL "bcrypt" AND NOT WIN32)
    message(FATAL_ERROR "twk: the bcrypt crypto backend needs Windows")
endif()
if(TWK_CRYPTO_BACKEND_RESOLVED STREQUAL "commoncrypto" AND NOT APPLE)
    message(FATAL_ERROR "twk: the commoncrypto backend needs macOS or iOS")
endif()
if(TWK_CRYPTO_BACKEND_RESOLVED STREQUAL "openssl")
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
endif()
message(STATUS "twk: crypto backend = ${TWK_CRYPTO_BACKEND_RESOLVED}")

set(TWK_CRYPTO_SOURCES util/random.cpp util/crypto_${TWK_CRYPTO_BACKEND_RESOLVED}.cpp)

set(TWK_SOURCES
    engine/js_runtime.cpp
    engine/event_loop.cpp
    util/base64.cpp
    ${TWK_CRYPTO_SOURCES}
    shims/shims.cpp
    bridge/transport.cpp
    client.cpp
)

add_library(twk STATIC ${TWK_SOURCES})
# The DLL owns the plain `twk` name (its import library is twk.lib), so the
# static lib is suffixed to avoid colliding with it.
set_target_properties(twk PROPERTIES OUTPUT_NAME twk_static)

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

# bcrypt is needed on Windows even with the portable backend: the entropy source
# is BCryptGenRandom either way.
if(WIN32)
    target_link_libraries(twk PRIVATE bcrypt)
endif()
if(TWK_CRYPTO_BACKEND_RESOLVED STREQUAL "openssl")
    target_link_libraries(twk PRIVATE OpenSSL::Crypto)
endif()

# ---- shared library (twk.dll) ----------------------------------------------
# The bindings (C#/Swift/Kotlin) load this, so it must be built separately rather
# than wrapped around the static lib: MSVC only emits exports for translation
# units compiled with __declspec(dllexport), which the static build lacks.
add_library(twk_shared SHARED ${TWK_SOURCES})
set_target_properties(twk_shared PROPERTIES OUTPUT_NAME twk)

target_compile_definitions(twk_shared PRIVATE TWK_BUILDING_SHARED=1)
target_include_directories(twk_shared
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/js/dist
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(twk_shared PRIVATE qjs)

if(TWK_USE_BYTECODE)
    add_dependencies(twk_shared twk_bundle_bytecode)
    target_compile_definitions(twk_shared PRIVATE TWK_BUNDLE_BYTECODE=1)
endif()

if(WIN32)
    target_link_libraries(twk_shared PRIVATE bcrypt)
endif()
if(TWK_CRYPTO_BACKEND_RESOLVED STREQUAL "openssl")
    target_link_libraries(twk_shared PRIVATE OpenSSL::Crypto)
endif()

# Non-Windows: hide everything except the TWK_EXPORT-marked ABI.
if(NOT WIN32)
    set_target_properties(twk_shared PROPERTIES C_VISIBILITY_PRESET hidden
                                                CXX_VISIBILITY_PRESET hidden
                                                VISIBILITY_INLINES_HIDDEN ON)
endif()
