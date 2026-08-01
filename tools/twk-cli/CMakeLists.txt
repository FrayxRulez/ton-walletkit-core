#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#

add_executable(twk-cli main.cpp)
# Links the reference host so the CLI can perform real network calls.
target_link_libraries(twk-cli PRIVATE twk twk_refhost)
