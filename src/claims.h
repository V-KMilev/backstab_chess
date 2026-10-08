#pragma once

#include "profile.h"

// How a piece changes into its new owner's look, a pure function of time like the takes: the
// game and the customize screen's preview both play it.
namespace Game {

/// A claimed piece a moment into its change.
struct ClaimPose {
    float spin    = 0.0f;   ///< Radians about +Y; whole turns at the end.
    float lift    = 0.0f;   ///< Metres up off its square.
    float scale   = 1.0f;   ///< Times its own.
    bool  flash   = false;  ///< Wears its owner's flash rather than a skin.
    bool  swapped = false;  ///< Wears its new skin already.
    float ring    = 0.0f;   ///< The wave's radius, times the square's half; 0 for none.
};

/// How long @p style takes at speed 1, in seconds.
float claimSeconds(ClaimStyle style);

/// The pose @p t of the way, 0..1, through a claim in @p style.
ClaimPose claimPose(ClaimStyle style, float t);

} // namespace Game
