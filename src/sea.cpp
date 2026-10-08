#include "sea.h"

#include <array>
#include <cmath>
#include <iterator>

#include <glm/gtc/constants.hpp>

#include "platform/threading/thread_pool.h"

namespace Game::Sea {

namespace {

constexpr int   RINGS     = 120;      ///< Out to NEAR_RADIUS, close together at the table.
constexpr int   FAR_RINGS = 12;       ///< Beyond it, flat, out to the horizon.
constexpr float HORIZON   = 1200.0f;
constexpr int   SEGMENTS  = 256;
constexpr float STEEPNESS = 0.45f;    ///< How far the crests lean together: 0 is a sine, 1 a cusp.
constexpr float GRAVITY   = 9.81f;
constexpr float CALM      = 150.0f;   ///< Where the swell begins to die away toward NEAR_RADIUS.

struct Wave {
    float amplitude;   ///< Metres.
    float wavelength;  ///< Metres.
    float heading;     ///< Degrees round from +Z toward +X it rolls toward.
    float phase;
};

// Long rollers down to short chop, from a spread of headings round the wind's; the ripples'
// normal map carries everything shorter.
const Wave WAVES[] = {
    {0.90f, 80.0f, 20.0f, 0.0f},
    {0.55f, 52.0f, -15.0f, 1.9f},
    {0.32f, 33.0f, 55.0f, 4.1f},
    {0.18f, 21.0f, -40.0f, 2.6f},
    {0.10f, 13.0f, 85.0f, 5.3f},
};
constexpr size_t COUNT = std::size(WAVES);

// Each wave worked out once: its number, direction, speed and the lean of its crests.
struct Rolled {
    float     k;
    glm::vec2 dir;
    float     omega;
    float     q;
};

const std::array<Rolled, COUNT>& rolled() {
    static const std::array<Rolled, COUNT> waves = [] {
        std::array<Rolled, COUNT> out{};
        for (size_t i = 0; i < COUNT; ++i) {
            const Wave& w = WAVES[i];
            const float k = glm::two_pi<float>() / w.wavelength;
            const float a = glm::radians(w.heading);
            // Deep water's speed, slowed a little: the swell is lazy.
            out[i] = {k, {std::sin(a), std::cos(a)}, std::sqrt(GRAVITY * k) * 0.8f, STEEPNESS / (k * w.amplitude * COUNT)};
        }
        return out;
    }();
    return waves;
}

// How much of the swell there is this far from the table: all of it, out to CALM, then less.
float strength(float r) {
    if (r <= CALM) return 1.0f;
    const float u = std::fmin((r - CALM) / (NEAR_RADIUS - CALM), 1.0f);
    return 1.0f - u * u * (3.0f - 2.0f * u);
}

// How much of a wave @p wavelength long to draw this far out: short chop fades with distance,
// where the rings are too far apart to hold it and it would only shimmer.
float detail(float wavelength, float r) {
    const float reach = wavelength * 7.0f;
    return 1.0f - std::fmin(std::fmax((r - reach) / reach, 0.0f), 1.0f);
}

} // namespace

float height(float x, float z, float t) {
    const float r    = std::sqrt(x * x + z * z);
    const float fade = strength(r);
    if (fade <= 0.0f) return 0.0f;
    float h = 0.0f;
    for (size_t i = 0; i < COUNT; ++i) {
        const Rolled& w = rolled()[i];
        h += WAVES[i].amplitude * detail(WAVES[i].wavelength, r) * std::sin(w.k * (w.dir.x * x + w.dir.y * z) - w.omega * t + WAVES[i].phase);
    }
    return h * fade;
}

glm::vec3 normal(float x, float z, float t) {
    const float r    = std::sqrt(x * x + z * z);
    const float fade = strength(r);
    if (fade <= 0.0f) return {0.0f, 1.0f, 0.0f};
    glm::vec3 n = {0.0f, 1.0f, 0.0f};
    for (size_t i = 0; i < COUNT; ++i) {
        const Rolled& w   = rolled()[i];
        const float   amp = WAVES[i].amplitude * detail(WAVES[i].wavelength, r) * fade;
        const float   c   = amp * w.k * std::cos(w.k * (w.dir.x * x + w.dir.y * z) - w.omega * t + WAVES[i].phase);
        n.x -= c * w.dir.x;
        n.z -= c * w.dir.y;
    }
    return glm::normalize(n);
}

MeshAsset mesh(std::vector<glm::vec2>& rest) {
    MeshAsset       sea;
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);
    rest.clear();
    for (int j = 0; j <= RINGS + FAR_RINGS; ++j) {
        const float r = j <= RINGS ? NEAR_RADIUS * std::pow(static_cast<float>(j) / RINGS, 1.7f)
                                   : NEAR_RADIUS * std::pow(HORIZON / NEAR_RADIUS, static_cast<float>(j - RINGS) / FAR_RINGS);
        for (int s = 0; s <= SEGMENTS; ++s) {
            const float a = glm::two_pi<float>() * static_cast<float>(s) / SEGMENTS;
            rest.push_back({std::sin(a) * r, std::cos(a) * r});
            sea.vertices.push_back({{rest.back().x, 0.0f, rest.back().y}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, tangent});
        }
    }
    // Ring j to ring j+1, two triangles a segment, wound to face up.
    const auto at = [](int j, int s) { return static_cast<uint32_t>(j * (SEGMENTS + 1) + s); };
    for (int j = 0; j < RINGS + FAR_RINGS; ++j) {
        for (int s = 0; s < SEGMENTS; ++s) {
            sea.indices.insert(sea.indices.end(), {at(j, s), at(j + 1, s), at(j, s + 1), at(j, s + 1), at(j + 1, s), at(j + 1, s + 1)});
        }
    }
    sea.boundsMin = {-HORIZON, -2.0f, -HORIZON};
    sea.boundsMax = {HORIZON, 2.0f, HORIZON};
    return sea;
}

void shape(MeshAsset& mesh, const std::vector<glm::vec2>& rest, float t, float tile) {
    const glm::vec2 slide = glm::vec2(0.9f, 0.5f) * t / tile;
    // Spread over the workers: every vertex is its own.
    parallelFor(std::min(rest.size(), mesh.vertices.size()), 2048, [&](size_t i) {
        const glm::vec2 p    = rest[i];
        const float     r    = glm::length(p);
        const float     fade = strength(r);
        // Gerstner: each wave also carries the water toward its crests, which sharpens them.
        glm::vec3 at = {p.x, 0.0f, p.y};
        glm::vec3 n  = {0.0f, 1.0f, 0.0f};
        if (fade > 0.0f) {
            for (size_t w = 0; w < COUNT; ++w) {
                const Rolled& wave = rolled()[w];
                const float   amp  = WAVES[w].amplitude * detail(WAVES[w].wavelength, r) * fade;
                if (amp <= 0.0f) continue;
                const float theta = wave.k * glm::dot(wave.dir, p) - wave.omega * t + WAVES[w].phase;
                const float c     = std::cos(theta);
                const float s     = std::sin(theta);
                at.x += wave.q * amp * wave.dir.x * c;
                at.z += wave.q * amp * wave.dir.y * c;
                at.y += amp * s;
                n.x  -= wave.dir.x * wave.k * amp * c;
                n.z  -= wave.dir.y * wave.k * amp * c;
                n.y  -= wave.q * wave.k * amp * s;
            }
        }
        mesh.vertices[i].position = at;
        mesh.vertices[i].normal   = glm::normalize(n);
        mesh.vertices[i].uv       = p / tile + slide;
    });
}

} // namespace Game::Sea
