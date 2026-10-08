#pragma once

// The world's measures, which the game and the scenery around it share.
namespace Game {

// The set is modelled life size, its board 55 cm across; the world is ten times that.
constexpr float WORLD_SCALE = 10.0f;

// The table, round, 1.2 metres across and 75 cm high, its top at y = 0.
constexpr float TABLE_RADIUS    = 6.0f;
constexpr float TABLE_THICKNESS = 0.45f;

// The plain the table stands on, a chessboard of ten-metre squares.
constexpr float FLOOR_DEPTH  = 7.5f;
constexpr float FLOOR_SIZE   = 2400.0f;
constexpr float FLOOR_SQUARE = 10.0f;

} // namespace Game
