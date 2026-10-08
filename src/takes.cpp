#include "takes.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Game {

namespace {

const glm::vec3 UP = {0.0f, 1.0f, 0.0f};

constexpr float SINK_DEPTH    = 1.0f;   ///< Deep enough to hide the tallest piece under the board.
constexpr float BEAM_HEIGHT   = 3.2f;   ///< How high the column lifts it.
constexpr float COLUMN_HEIGHT = 14.0f;
constexpr float COLUMN_RADIUS = 0.32f;
constexpr float LAUNCH_HEIGHT = 30.0f;  ///< Out of sight from any seat.

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// 0..1 through the part of [from, to] that @p t is in.
float phase(float t, float from, float to) { return std::clamp((t - from) / (to - from), 0.0f, 1.0f); }

// Overshoots past 1 and settles back, for a pop.
float backOut(float t) {
    constexpr float S = 1.70158f;
    const float     u = t - 1.0f;
    return 1.0f + (S + 1.0f) * u * u * u + S * u * u;
}

TakePose floatAway(float t, const glm::vec3& from, const glm::vec3& to) {
    TakePose pose;
    pose.position = glm::mix(from, to, smooth(t)) + UP * (1.4f * std::sin(t * glm::pi<float>()));
    pose.spin     = glm::two_pi<float>() * smooth(t);
    return pose;
}

// Down through the board, a beat out of sight, and up through the table; a ripple spreads
// from each surface it crosses.
TakePose sink(float t, const glm::vec3& from, const glm::vec3& to) {
    TakePose pose;
    const bool  down = t < 0.5f;
    const float u    = down ? phase(t, 0.0f, 0.42f) : phase(t, 0.58f, 1.0f);
    const float r    = 0.2f + 0.6f * u;
    pose.position    = down ? from - UP * (SINK_DEPTH * smooth(u)) : to - UP * (SINK_DEPTH * (1.0f - smooth(u)));
    pose.showEffect  = u > 0.0f && u < 1.0f;
    pose.effectAt    = (down ? from : to) + UP * 0.008f;
    pose.effectScale = {r, 1.0f, r};
    return pose;
}

// Up a column of light, shrinking and turning, and down another at the trophies.
TakePose beam(float t, const glm::vec3& from, const glm::vec3& to) {
    TakePose pose;
    const bool  up    = t < 0.5f;
    const float u     = up ? phase(t, 0.0f, 0.5f) : phase(t, 0.5f, 1.0f);
    const float lift  = up ? smooth(u) : 1.0f - smooth(u);
    const glm::vec3 base = up ? from : to;
    pose.position    = base + UP * (BEAM_HEIGHT * lift);
    pose.scale       = glm::vec3(glm::mix(1.0f, 0.25f, lift));
    pose.spin        = glm::two_pi<float>() * (up ? u : 1.0f + u);  // two turns in all
    // The column opens and closes over each half.
    const float width = 2.0f * COLUMN_RADIUS * std::sqrt(std::sin(u * glm::pi<float>()));
    pose.showEffect  = width > 0.0f;
    pose.effectAt    = base + UP * (COLUMN_HEIGHT * 0.5f);
    pose.effectScale = {width, COLUMN_HEIGHT, width};
    return pose;
}

// Straight up out of sight, spinning, and down onto the trophy spot, settling softly.
TakePose launch(float t, const glm::vec3& from, const glm::vec3& to) {
    TakePose pose;
    if (t < 0.3f) {
        const float u = phase(t, 0.0f, 0.3f);
        pose.position = from + UP * (LAUNCH_HEIGHT * u * u);
        pose.spin     = glm::two_pi<float>() * 3.0f * u;
        return pose;
    }
    // Down out of the sky and settling softly onto its place.
    const float u = phase(t, 0.3f, 1.0f);
    pose.position = to + UP * (LAUNCH_HEIGHT * std::pow(1.0f - u, 3.0f));
    return pose;
}

// Flattened, held, popped to nothing, and popped back in at the trophies.
TakePose squash(float t, const glm::vec3& from, const glm::vec3& to) {
    TakePose        pose;
    const glm::vec3 flat = {1.45f, 0.15f, 1.45f};
    if (t < 0.45f) {
        const float s = smooth(phase(t, 0.0f, 0.12f));
        pose.position = from;
        pose.scale    = glm::mix(glm::vec3(1.0f), flat, s) * (1.0f - smooth(phase(t, 0.35f, 0.45f)));
        return pose;
    }
    pose.position = to;
    pose.scale    = glm::vec3(std::max(backOut(phase(t, 0.45f, 1.0f)), 0.0f));
    return pose;
}

} // namespace

const char* takeStyleName(TakeStyle style) {
    switch (style) {
        case TakeStyle::Float:  return "Float";
        case TakeStyle::Sink:   return "Sink";
        case TakeStyle::Beam:   return "Beam";
        case TakeStyle::Launch: return "Launch";
        case TakeStyle::Squash: return "Squash";
        default:                return "";
    }
}

float takeSeconds(TakeStyle style) {
    switch (style) {
        case TakeStyle::Sink:   return 1.7f;
        case TakeStyle::Beam:   return 1.8f;
        case TakeStyle::Launch: return 1.4f;
        case TakeStyle::Squash: return 1.0f;
        default:                return 1.1f;
    }
}

float takeStart(TakeStyle style) { return style == TakeStyle::Squash ? 1.0f : 0.55f; }

TakeEffect takeEffect(TakeStyle style) {
    switch (style) {
        case TakeStyle::Sink: return TakeEffect::Ripple;
        case TakeStyle::Beam: return TakeEffect::Column;
        default:              return TakeEffect::None;
    }
}

TakePose takePose(TakeStyle style, float t, const glm::vec3& from, const glm::vec3& to) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (style) {
        case TakeStyle::Sink:   return sink(t, from, to);
        case TakeStyle::Beam:   return beam(t, from, to);
        case TakeStyle::Launch: return launch(t, from, to);
        case TakeStyle::Squash: return squash(t, from, to);
        default:                return floatAway(t, from, to);
    }
}

} // namespace Game
