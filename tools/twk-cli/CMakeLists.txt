add_executable(twk-cli main.cpp)
# Links the reference host so the CLI can perform real network calls.
target_link_libraries(twk-cli PRIVATE twk twk_refhost)
