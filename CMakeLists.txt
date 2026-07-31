cmake_minimum_required(VERSION 3.20)

project(ton_walletkit_core
    VERSION 0.0.1
    DESCRIPTION "Cross-platform TON WalletKit core: canonical @ton/walletkit JS embedded in QuickJS behind a C ABI"
    LANGUAGES C CXX)

# ---- options --------------------------------------------------------------
option(TWK_BUILD_CLI   "Build the twk-cli test driver"   ON)
option(TWK_BUILD_TESTS "Build unit / integration tests"  ON)

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

# ---- core library --------------------------- (M0: js_runtime/event_loop/client)
add_subdirectory(src)

# ---- test driver ------------------------------------------ (M0: twk-cli)
# if(TWK_BUILD_CLI)
#     add_subdirectory(tools/twk-cli)
# endif()

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
endif()

message(STATUS "ton-walletkit-core ${PROJECT_VERSION} configured (cli=${TWK_BUILD_CLI} tests=${TWK_BUILD_TESTS})")
