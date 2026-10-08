# A vkmEngine project: every .cpp under src/, built into the module a host loads.
# `vkm build` runs this for you.
cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES CXX)

find_package(vkmEngine REQUIRED)

vkm_add_gameplay_module(game_module)

# The chess rules on their own, against the perft counts every chess engine is checked by.
add_executable(chess_tests tests/chess_tests.cpp src/chess/position.cpp)
target_include_directories(chess_tests PRIVATE src)
target_compile_features(chess_tests PRIVATE cxx_std_17)
