#pragma once

#include <glm/glm.hpp>

// How a player's captures leave the board: a cosmetic each player picks, like their skin. Each
// style is a pure function of time, from the taken piece's square to its taker's trophy row.
namespace Game {

enum class TakeStyle : int {
    Float,   ///< Rises, turns once and drifts across.
    Sink,    ///< Sinks into the board with a ripple, and surfaces through the table.
    Beam,    ///< Lifted up a column of light, and beamed down at the trophies.
    Launch,  ///< Rockets out of sight, and drops back down to settle softly.
    Squash,  ///< Flattened by the piece landing on it, popped, and popped back in.
    Count
};

/// The display name of @p style.
const char* takeStyleName(TakeStyle style);

/// What a style's effect is drawn with, if it has one.
enum class TakeEffect : int { None, Ripple, Column };

/// Where a taken piece is, a moment into its trip, and its effect with it.
struct TakePose {
    glm::vec3 position    = {0.0f, 0.0f, 0.0f};
    float     spin        = 0.0f;                ///< Radians about +Y, over its own facing; whole turns at the end.
    glm::vec3 scale       = {1.0f, 1.0f, 1.0f};  ///< Times its own.
    bool      showEffect  = false;
    glm::vec3 effectAt    = {0.0f, 0.0f, 0.0f};  ///< The effect's centre, in world units.
    glm::vec3 effectScale = {1.0f, 1.0f, 1.0f};  ///< The effect mesh's scale, in world units.
};

/// How long @p style takes, in seconds.
float takeSeconds(TakeStyle style);

/// How far through the attacker's move @p style begins, 0..1: a squash waits for it to land.
float takeStart(TakeStyle style);

/// The effect @p style draws.
TakeEffect takeEffect(TakeStyle style);

/**
 * @brief The taken piece's pose a fraction of the way through its trip.
 *
 * @param style How it goes.
 * @param t     0..1 through takeSeconds(style).
 * @param from  Where it stood on the board.
 * @param to    Its place in the trophy row.
 * @return The pose: at @p from, unscaled, at 0; at @p to, unscaled and facing as it did, at 1.
 */
TakePose takePose(TakeStyle style, float t, const glm::vec3& from, const glm::vec3& to);

} // namespace Game
