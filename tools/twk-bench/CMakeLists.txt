#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#

add_executable(twk-bench main.cpp)
target_link_libraries(twk-bench PRIVATE twk)
