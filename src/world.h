#pragma once

// The world's measures, which the game and the scenery around it share.
namespace Game {

// The set is modelled life size, its board 55 cm across; the world is ten times that.
constexpr float WORLD_SCALE = 10.0f;

// The table, round, 1.2 metres across, its top at y = 0.
constexpr float TABLE_RADIUS    = 6.0f;
constexpr float TABLE_THICKNESS = 0.45f;

// The sea the table stands in, and how far its ripples repeat.
constexpr float SEA_LEVEL = -4.5f;
constexpr float SEA_TILE  = 18.0f;

} // namespace Game
