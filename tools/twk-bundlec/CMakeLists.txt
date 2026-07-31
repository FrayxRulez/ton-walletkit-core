# Build-time tool: compiles the JS bundle to QuickJS bytecode.
# Links qjs + the core's base64 helper directly (not the twk library, which would
# be circular: twk consumes this tool's output).
add_executable(twk-bundlec
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/util/base64.cpp
)
target_link_libraries(twk-bundlec PRIVATE qjs)
target_include_directories(twk-bundlec PRIVATE ${CMAKE_SOURCE_DIR}/src)
