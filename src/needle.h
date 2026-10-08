#pragma once

#include <cmath>

// A duel's needle, the same on every machine: the server judges the stops clients send by it.
namespace Game::Needle {

/// Where a needle is, @p t seconds into its duel: 0..1 along its bar, swinging faster and faster.
inline float at(float t) { return 0.5f + 0.5f * std::sin(2.4f * t + 0.45f * t * t + 1.2f); }

/// How near the middle a needle stopped at @p t is: 1 dead centre, 0 at either end.
inline float score(float t) { return 1.0f - std::abs(at(t) - 0.5f) * 2.0f; }

/// The share of the bar the gold middle takes.
constexpr float ZONE = 0.14f;

} // namespace Game::Needle
