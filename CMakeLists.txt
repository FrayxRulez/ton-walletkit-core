cmake_minimum_required(VERSION 3.20)

project(ton_walletkit_core
    VERSION 0.0.1
    DESCRIPTION "Cross-platform TON WalletKit core: canonical @ton/walletkit JS embedded in QuickJS behind a C ABI"
    LANGUAGES C CXX)

# ---- options --------------------------------------------------------------
option(TWK_BUILD_CLI   "Build the twk-cli test driver"   ON)
option(TWK_BUILD_TESTS "Build unit / integration tests"  ON)
set(TWK_SANITIZE "" CACHE STRING "Sanitizer to build with: address | undefined | thread (empty = none)")

if(TWK_SANITIZE)
    message(STATUS "twk: building with sanitizer '${TWK_SANITIZE}'")
    if(MSVC)
        if(NOT TWK_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "twk: MSVC only supports TWK_SANITIZE=address")
        endif()
        add_compile_options(/fsanitize=address /Zi)
        # ASan is incompatible with incremental linking and edit-and-continue.
        add_link_options(/INCREMENTAL:NO)
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    else()
        add_compile_options(-fsanitize=${TWK_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${TWK_SANITIZE})
    endif()
endif()

# ---- global settings ------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# ---- dependencies ------------------------------------ (M0: vendor QuickJS-ng)
# QuickJS-ng (pinned submodule, third_party/quickjs @ v0.15.1). We consume only the
# `qjs` static library; its CLI (qjs_exe), bytecode compiler (qjsc), examples and
# tests are kept out via EXCLUDE_FROM_ALL, and its install rules are disabled.
set(QJS_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/quickjs EXCLUDE_FROM_ALL)

# ---- build-time tools ------------------------------------------------------
# twk-bundlec compiles the JS bundle to QuickJS bytecode; declared before src/,
# which consumes it, and not gated on TWK_BUILD_CLI.
add_subdirectory(tools/twk-bundlec)

# ---- core library --------------------------- (M0: js_runtime/event_loop/client)
add_subdirectory(src)

# ---- reference host (test-only delegate implementation) --------------------
add_subdirectory(host)

# ---- test driver ------------------------------------------ (M0: twk-cli)
if(TWK_BUILD_CLI)
    add_subdirectory(tools/twk-cli)
    add_subdirectory(tools/twk-bench)
endif()

# ---- tests ------------------------------------------------ (M0: exit tests)
if(TWK_BUILD_TESTS)
    enable_testing()
    # QuickJS-ng link/eval sanity (grows into the full suite in the twk-cli/tests task).
    add_executable(twk_qjs_smoke test/quickjs_smoke.c)
    target_link_libraries(twk_qjs_smoke PRIVATE qjs)
    add_test(NAME quickjs_smoke COMMAND twk_qjs_smoke)

    # JsRuntime wrapper sanity (native global + eval + exception path).
    add_executable(twk_engine_smoke test/engine_smoke.cpp)
    target_link_libraries(twk_engine_smoke PRIVATE twk qjs)
    target_include_directories(twk_engine_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME engine_smoke COMMAND twk_engine_smoke)

    # EventLoop sanity (tasks, timers, cancellation, JS microtask pump).
    add_executable(twk_event_loop_smoke test/event_loop_smoke.cpp)
    target_link_libraries(twk_event_loop_smoke PRIVATE twk qjs)
    target_include_directories(twk_event_loop_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME event_loop_smoke COMMAND twk_event_loop_smoke)

    # C ABI sanity (send/receive correlation, timeout, ordering, churn).
    add_executable(twk_client_smoke test/client_smoke.cpp)
    target_link_libraries(twk_client_smoke PRIVATE twk)
    add_test(NAME client_smoke COMMAND twk_client_smoke)

    # Bridge round-trip through JS (params normalized by a JSON parse/stringify).
    add_executable(twk_bridge_smoke test/bridge_smoke.cpp)
    target_link_libraries(twk_bridge_smoke PRIVATE twk)
    add_test(NAME bridge_smoke COMMAND twk_bridge_smoke)

    # M1 exit: createMnemonic through the real walletkit bundle -> 24 words.
    add_executable(twk_mnemonic_smoke test/mnemonic_smoke.cpp)
    target_link_libraries(twk_mnemonic_smoke PRIVATE twk)
    add_test(NAME mnemonic_smoke COMMAND twk_mnemonic_smoke)

    # M2 exit: init + getBalance through walletkit's own client, against a
    # recorded toncenter response (deterministic, no network).
    add_executable(twk_balance_smoke test/balance_smoke.cpp)
    target_link_libraries(twk_balance_smoke PRIVATE twk)
    add_test(NAME balance_smoke COMMAND twk_balance_smoke)

    # M4 exit: TON Connect connect request -> update -> approve.
    add_executable(twk_tonconnect_smoke test/tonconnect_smoke.cpp)
    target_link_libraries(twk_tonconnect_smoke PRIVATE twk)
    add_test(NAME tonconnect_smoke COMMAND twk_tonconnect_smoke)

    # Robustness: malformed input, misbehaving hosts, oversized payloads.
    add_executable(twk_robustness_smoke test/robustness_smoke.cpp)
    target_link_libraries(twk_robustness_smoke PRIVATE twk)
    add_test(NAME robustness_smoke COMMAND twk_robustness_smoke)
    # Short runaway-script budget so the watchdog case does not take a minute.
    set_tests_properties(robustness_smoke PROPERTIES ENVIRONMENT "TWK_JS_BUDGET_MS=3000")

    # Concurrency stress (many senders + one receiver; destroy racing completions).
    add_executable(twk_concurrency_stress test/concurrency_stress.cpp)
    target_link_libraries(twk_concurrency_stress PRIVATE twk)
    add_test(NAME concurrency_stress COMMAND twk_concurrency_stress)

    # Loads twk.dll dynamically by name, as a binding (P/Invoke) does.
    add_executable(twk_shared_lib_smoke test/shared_lib_smoke.cpp)
    add_dependencies(twk_shared_lib_smoke twk_shared)
    add_test(NAME shared_lib_smoke COMMAND twk_shared_lib_smoke)
    # The DLL sits next to the test binary in bin/, so the default search path finds it.

    # SSE framing parser (multi-line data, comments, CRLF, chunk boundaries).
    add_executable(twk_sse_parser_test test/sse_parser_test.cpp)
    target_link_libraries(twk_sse_parser_test PRIVATE twk_refhost)
    add_test(NAME sse_parser_test COMMAND twk_sse_parser_test)

    # SSE seam (multi-shot: EventSource -> sse_open delegate -> events + close).
    add_executable(twk_sse_smoke test/sse_smoke.cpp)
    target_link_libraries(twk_sse_smoke PRIVATE twk)
    add_test(NAME sse_smoke COMMAND twk_sse_smoke)

    # Unsolicited updates (request_id=0) and their interleaving with responses.
    add_executable(twk_event_smoke test/event_smoke.cpp)
    target_link_libraries(twk_event_smoke PRIVATE twk)
    add_test(NAME event_smoke COMMAND twk_event_smoke)

    # M3 exit: full wallet lifecycle + restore-from-mnemonic, against fixtures.
    add_executable(twk_lifecycle_smoke test/lifecycle_smoke.cpp)
    target_link_libraries(twk_lifecycle_smoke PRIVATE twk)
    add_test(NAME lifecycle_smoke COMMAND twk_lifecycle_smoke)

    # Storage delegate contract + persistence across clients.
    add_executable(twk_storage_smoke test/storage_smoke.cpp)
    target_link_libraries(twk_storage_smoke PRIVATE twk_refhost)
    add_test(NAME storage_smoke COMMAND twk_storage_smoke)

    # Live network through the reference host (opt-in: TWK_LIVE_NETWORK_TESTS=1).
    add_executable(twk_refhost_smoke test/refhost_smoke.cpp)
    target_link_libraries(twk_refhost_smoke PRIVATE twk_refhost)
    add_test(NAME refhost_smoke COMMAND twk_refhost_smoke)

    # JS fetch shim end to end (fetch -> __twk_http -> delegate -> Response).
    add_executable(twk_fetch_smoke test/fetch_smoke.cpp)
    target_link_libraries(twk_fetch_smoke PRIVATE twk)
    add_test(NAME fetch_smoke COMMAND twk_fetch_smoke)

    # Host-delegate seam (http_request round trip, cancel, teardown races).
    add_executable(twk_delegate_smoke test/delegate_smoke.cpp)
    target_link_libraries(twk_delegate_smoke PRIVATE twk)
    target_include_directories(twk_delegate_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME delegate_smoke COMMAND twk_delegate_smoke)

    # Known-answer tests for the native crypto (RFC vectors + OpenSSL-generated).
    add_executable(twk_crypto_kat test/crypto_kat.cpp)
    target_link_libraries(twk_crypto_kat PRIVATE twk)
    target_include_directories(twk_crypto_kat PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME crypto_kat COMMAND twk_crypto_kat)

    # Host shims (console / getRandomValues / Pbkdf2 / timers).
    add_executable(twk_shims_smoke test/shims_smoke.cpp)
    target_link_libraries(twk_shims_smoke PRIVATE twk qjs)
    target_include_directories(twk_shims_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME shims_smoke COMMAND twk_shims_smoke)

    # Teardown across varied in-flight states.
    add_executable(twk_teardown_stress test/teardown_stress.cpp)
    target_link_libraries(twk_teardown_stress PRIVATE twk)
    add_test(NAME teardown_stress COMMAND twk_teardown_stress)

    # M0 exit criterion: twk-cli drives an echo end to end through QuickJS.
    if(TARGET twk-cli)
        add_test(NAME cli_echo COMMAND twk-cli ${CMAKE_SOURCE_DIR}/test/m0_echo.twk)
        set_tests_properties(cli_echo PROPERTIES PASS_REGULAR_EXPRESSION "<< id=2 .*\"n\":1000")
    endif()
endif()

message(STATUS "ton-walletkit-core ${PROJECT_VERSION} configured (cli=${TWK_BUILD_CLI} tests=${TWK_BUILD_TESTS})")
