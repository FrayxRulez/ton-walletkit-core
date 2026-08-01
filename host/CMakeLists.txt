#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Test-only host implementation of the twk delegates (not shipped).
add_library(twk_refhost STATIC reference_host.cpp)

target_include_directories(twk_refhost
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

# HTTP/SSE for the tests. WinHTTP is in-box on Windows; elsewhere libcurl is used
# when it is installed, which is what lets the suite run on Linux/macOS CI (and
# with it the sanitizers MSVC lacks). Without either, the host reports
# httpAvailable() == false and the tests that need requests skip.
if(WIN32)
    target_link_libraries(twk_refhost PRIVATE winhttp)
else()
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_compile_definitions(twk_refhost PRIVATE TWK_HOST_CURL=1)
        target_link_libraries(twk_refhost PRIVATE CURL::libcurl)
        message(STATUS "twk: reference host transport = libcurl")
    else()
        message(STATUS "twk: reference host transport = none (libcurl not found; HTTP tests will skip)")
    endif()
endif()

target_link_libraries(twk_refhost PUBLIC twk)
