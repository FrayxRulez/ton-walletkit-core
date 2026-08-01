#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Build-time tool: compiles the JS bundle to QuickJS bytecode.
# Links qjs + the core's base64 helper directly (not the twk library, which would
# be circular: twk consumes this tool's output).
add_executable(twk-bundlec
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/util/base64.cpp
)
target_link_libraries(twk-bundlec PRIVATE qjs)
target_include_directories(twk-bundlec PRIVATE ${CMAKE_SOURCE_DIR}/src)

# --deflate needs zlib here, on the machine that runs the tool. Independent of
# whether the target has it: in a cross build this is the host and the core is
# somewhere else entirely, so the tool is built with the capability whenever it
# can be and refuses --deflate loudly when it cannot.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(twk-bundlec PRIVATE ZLIB::ZLIB)
    target_compile_definitions(twk-bundlec PRIVATE TWK_BUNDLEC_ZLIB=1)
endif()
