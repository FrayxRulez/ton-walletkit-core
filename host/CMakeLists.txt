# Test-only host implementation of the twk delegates (not shipped).
add_library(twk_refhost STATIC reference_host.cpp)

target_include_directories(twk_refhost
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

if(WIN32)
    # WinHTTP is in-box, so the reference host needs no external dependency.
    target_link_libraries(twk_refhost PRIVATE winhttp)
endif()

target_link_libraries(twk_refhost PUBLIC twk)
