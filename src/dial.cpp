#include "dial.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace Game::Dial {

namespace {

const glm::vec3 SUN_CORE  = {1.0f, 0.93f, 0.62f};
const glm::vec3 SUN_RIM   = {1.0f, 0.52f, 0.1f};
const glm::vec3 MOON_LIT  = {0.86f, 0.89f, 0.96f};
const glm::vec3 MOON_DARK = {0.5f, 0.56f, 0.72f};
const glm::vec3 RIM_CALM  = {0.08f, 0.08f, 0.12f};
const glm::vec3 RIM_HURRY = {1.0f, 0.35f, 0.25f};

struct Crater {
    glm::vec2 at;  ///< In the face's own frame, radii: +y toward the sun's side.
    float     radius;
};

// The moon's craters, all on its half (y below zero).
const Crater CRATERS[] = {
    {{0.32f, -0.38f}, 0.17f}, {{-0.38f, -0.5f}, 0.12f}, {{-0.1f, -0.74f}, 0.1f},
    {{0.58f, -0.12f}, 0.08f}, {{-0.62f, -0.16f}, 0.07f},
};

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The sun's half: a hot core going orange to its edge, and soft rays fanning out round it.
glm::vec3 sun(const glm::vec2& p, float r) {
    glm::vec3 color = glm::mix(SUN_CORE, SUN_RIM, smooth(0.1f, 1.0f, r));
    const float rays = 0.5f + 0.5f * std::cos(std::atan2(p.x, p.y) * 14.0f);
    color += glm::vec3(1.0f, 0.8f, 0.4f) * (rays * 0.16f * smooth(0.45f, 0.85f, r));
    return glm::min(color, glm::vec3(1.0f));
}

// The moon's half: lit toward the dividing line and darker to its edge, with shaded craters,
// each lit on its upper lip.
glm::vec3 moon(const glm::vec2& p, float r) {
    glm::vec3 color = glm::mix(MOON_LIT, MOON_DARK, smooth(0.2f, 1.0f, r));
    for (const Crater& c : CRATERS) {
        const glm::vec2 d    = p - c.at;
        const float     dist = glm::length(d) / c.radius;
        if (dist > 1.15f) continue;
        const float inside = 1.0f - smooth(0.85f, 1.0f, dist);
        const float lip    = smooth(0.7f, 1.0f, dist) * (1.0f - smooth(1.0f, 1.15f, dist)) * std::max(d.y / c.radius, 0.0f);
        color = glm::mix(color, color * 0.72f, inside);
        color += glm::vec3(0.12f) * lip;
    }
    return glm::min(color, glm::vec3(1.0f));
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

} // namespace

void paint(std::vector<uint8_t>& rgba, float turn, float alarm) {
    rgba.resize(static_cast<size_t>(SIZE * SIZE * 4));
    const float     half  = SIZE * 0.5f;
    const float     edge  = half - 1.5f;                  // the disc's radius in pixels
    const float     rim   = 5.0f;                         // the rim's width in pixels
    const glm::vec2 up    = {std::sin(turn), std::cos(turn)};   // toward the sun, y up
    const glm::vec2 right = {std::cos(turn), -std::sin(turn)};
    const glm::vec3 band  = glm::mix(RIM_CALM, RIM_HURRY, alarm);

    for (int y = 0; y < SIZE; ++y) {
        for (int x = 0; x < SIZE; ++x) {
            const glm::vec2 screen = {static_cast<float>(x) + 0.5f - half, half - static_cast<float>(y) - 0.5f};
            const float     dist   = glm::length(screen);
            uint8_t*        out    = &rgba[static_cast<size_t>((y * SIZE + x) * 4)];
            const float     cover  = std::clamp(edge - dist + 0.5f, 0.0f, 1.0f);
            if (cover <= 0.0f) {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            // The face's own frame: +y toward the sun's side, in radii.
            const glm::vec2 face  = glm::vec2(glm::dot(screen, right), glm::dot(screen, up)) / (edge - rim);
            const float     r     = glm::length(face);
            const float     blend = smooth(-1.2f, 1.2f, face.y * (edge - rim));  // a soft line between the halves
            glm::vec3       color = glm::mix(moon(face, r), sun(face, r), blend);
            color *= 1.0f - 0.35f * (1.0f - smooth(0.0f, 2.5f, std::abs(face.y * (edge - rim))));
            // The rim over the face's edge.
            const float onRim = smooth(edge - rim - 1.0f, edge - rim + 0.5f, dist);
            color = glm::mix(color, band, onRim);

            out[0] = toByte(color.r);
            out[1] = toByte(color.g);
            out[2] = toByte(color.b);
            out[3] = toByte(cover);
        }
    }
}

} // namespace Game::Dial
