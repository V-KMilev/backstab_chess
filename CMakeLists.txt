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

# Backstab's rules over them: turns, ownership, duels and scores.
add_executable(match_tests tests/match_tests.cpp src/chess/match.cpp src/chess/position.cpp)
target_include_directories(match_tests PRIVATE src)
target_compile_features(match_tests PRIVATE cxx_std_17)

# The ways a taken piece leaves the board: each must start on its square and end in the trophy row.
add_executable(takes_tests tests/takes_tests.cpp src/takes.cpp)
target_include_directories(takes_tests PRIVATE src)
target_link_libraries(takes_tests PRIVATE vkmEngine::glm-header-only)
target_compile_features(takes_tests PRIVATE cxx_std_17)

# How a claimed piece changes into its owner's look: each style must end whole, in the new skin.
add_executable(claims_tests tests/claims_tests.cpp src/claims.cpp src/profile.cpp)
target_include_directories(claims_tests PRIVATE src)
target_link_libraries(claims_tests PRIVATE vkmEngine::vkm_core)
target_compile_features(claims_tests PRIVATE cxx_std_17)
