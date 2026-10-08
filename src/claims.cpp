#include "claims.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Game {

namespace {

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

float bump(float t) { return std::sin(std::clamp(t, 0.0f, 1.0f) * glm::pi<float>()); }

} // namespace

float claimSeconds(ClaimStyle style) {
    switch (style) {
        case ClaimStyle::Instant: return 0.01f;
        case ClaimStyle::Flash:   return 0.7f;
        case ClaimStyle::Spin:    return 0.9f;
        case ClaimStyle::Rise:    return 1.0f;
        case ClaimStyle::Wave:    return 1.0f;
        default:                  return 0.5f;
    }
}

ClaimPose claimPose(ClaimStyle style, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    ClaimPose pose;
    switch (style) {
        case ClaimStyle::Flash:
            // A flash of its owner's colour, a swell with it, and the new skin as it fades.
            pose.flash   = t > 0.2f && t < 0.6f;
            pose.swapped = t >= 0.6f;
            pose.scale   = 1.0f + 0.14f * bump(t);
            break;
        case ClaimStyle::Spin:
            // A turn on the spot, lifting a little, the skin changing as it faces away.
            pose.spin    = glm::two_pi<float>() * smooth(t);
            pose.lift    = 0.2f * bump(t);
            pose.swapped = t >= 0.5f;
            break;
        case ClaimStyle::Rise:
            // Up, a beat at the top where it changes, and down again.
            pose.lift    = 0.7f * std::pow(bump(t), 0.6f);
            pose.scale   = 1.0f + 0.08f * bump(t);
            pose.swapped = t >= 0.5f;
            break;
        case ClaimStyle::Wave:
            // A ring of its owner's colour spreads from its foot; the skin changes as it passes.
            pose.ring    = t < 0.95f ? 0.3f + 1.6f * smooth(t) : 0.0f;
            pose.scale   = 1.0f + 0.06f * bump(t * 2.0f);
            pose.swapped = t >= 0.35f;
            break;
        case ClaimStyle::Instant:
        default:
            pose.swapped = true;
            break;
    }
    if (t >= 1.0f) {
        pose.flash   = false;
        pose.swapped = true;
        pose.lift    = 0.0f;
        pose.scale   = 1.0f;
        pose.ring    = 0.0f;
    }
    return pose;
}

} // namespace Game
