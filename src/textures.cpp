#include "textures.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "platform/threading/thread_pool.h"

namespace Game::Textures {

namespace {

constexpr int   SIZE = 1024;
constexpr float TAU  = 6.28318530718f;

// A value at each lattice point, the lattice wrapping every @p period points so the noise tiles.
float lattice(int x, int y, int period, uint32_t seed) {
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

// Smooth value noise at (@p u, @p v) in 0..1, @p period cells across, tiling.
float noise(float u, float v, int period, uint32_t seed) {
    const float x  = u * static_cast<float>(period);
    const float y  = v * static_cast<float>(period);
    const int   x0 = static_cast<int>(std::floor(x));
    const int   y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a  = glm::mix(lattice(x0, y0, period, seed), lattice(x0 + 1, y0, period, seed), sx);
    const float b  = glm::mix(lattice(x0, y0 + 1, period, seed), lattice(x0 + 1, y0 + 1, period, seed), sx);
    return glm::mix(a, b, sy);
}

// Octaves of noise, each twice as fine and half as strong: 0..1.
float fbm(float u, float v, int period, uint32_t seed, int octaves = 5) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum  += amp * noise(u, v, period << o, seed + static_cast<uint32_t>(o) * 17u);
        norm += amp;
        amp  *= 0.5f;
    }
    return sum / norm;
}

TextureAsset blank(TextureInternalFormat internal, TexturePixelFormat format, int channels) {
    TextureAsset texture;
    texture.params.width          = SIZE;
    texture.params.height         = SIZE;
    texture.params.internalFormat = internal;
    texture.params.format         = format;
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.wrapT          = TextureWrapMode::Repeat;
    texture.pixelData.resize(static_cast<size_t>(SIZE * SIZE * channels));
    return texture;
}

uint8_t byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

TextureAsset colorMap(const std::function<glm::vec3(float, float)>& color) {
    TextureAsset texture = blank(TextureInternalFormat::SRGBA8, TexturePixelFormat::RGBA, 4);
    parallelFor(SIZE, 16, [&](size_t row) {
        const int y = static_cast<int>(row);
        for (int x = 0; x < SIZE; ++x) {
            const glm::vec3 c = color((static_cast<float>(x) + 0.5f) / SIZE, (static_cast<float>(y) + 0.5f) / SIZE);
            uint8_t* out = &texture.pixelData[static_cast<size_t>((y * SIZE + x) * 4)];
            out[0] = byte(c.r);
            out[1] = byte(c.g);
            out[2] = byte(c.b);
            out[3] = 255;
        }
    });
    return texture;
}

// A normal map from a height field, by its slope between neighbours, @p strength steep.
TextureAsset normalMap(const std::function<float(float, float)>& height, float strength) {
    std::vector<float> h(static_cast<size_t>(SIZE * SIZE));
    parallelFor(SIZE, 16, [&](size_t row) {
        const int y = static_cast<int>(row);
        for (int x = 0; x < SIZE; ++x) h[static_cast<size_t>(y * SIZE + x)] = height((x + 0.5f) / SIZE, (y + 0.5f) / SIZE);
    });
    const auto at = [&](int x, int y) { return h[static_cast<size_t>(((y + SIZE) % SIZE) * SIZE + (x + SIZE) % SIZE)]; };
    TextureAsset texture = blank(TextureInternalFormat::RG8, TexturePixelFormat::RG, 2);
    for (int y = 0; y < SIZE; ++y) {
        for (int x = 0; x < SIZE; ++x) {
            const float     dx = (at(x + 1, y) - at(x - 1, y)) * strength;
            const float     dy = (at(x, y + 1) - at(x, y - 1)) * strength;
            const glm::vec3 n  = glm::normalize(glm::vec3(-dx, -dy, 1.0f));
            texture.pixelData[static_cast<size_t>((y * SIZE + x) * 2 + 0)] = byte(n.x * 0.5f + 0.5f);
            texture.pixelData[static_cast<size_t>((y * SIZE + x) * 2 + 1)] = byte(n.y * 0.5f + 0.5f);
        }
    }
    return texture;
}

// Walnut: eight planks across V, each with its own shade and its grain shifted along, the
// grain a ring pattern bent by noise.
constexpr int PLANKS = 8;

float plankOf(float v) { return std::floor(v * PLANKS); }

float grain(float u, float v) {
    const float plank = plankOf(v);
    const float local = v * PLANKS - plank;
    const float shift = lattice(static_cast<int>(plank), 3, PLANKS, 11u);
    const float warp  = fbm(u + shift, v, 6, 23u, 4) * 5.0f;
    const float rings = local * 14.0f + warp * 0.6f + std::sin((u + shift) * TAU * 2.0f) * 0.4f;
    return 0.5f + 0.5f * std::sin(rings * TAU);
}

// How far into the groove between planks: 1 in it, 0 clear of it.
float groove(float v) {
    const float local = v * PLANKS - plankOf(v);
    const float edge  = std::min(local, 1.0f - local) * static_cast<float>(SIZE) / PLANKS;
    return 1.0f - std::clamp(edge / 2.5f, 0.0f, 1.0f);
}

// Marble's veins: thin dark lines along a noise-bent wave, 0 to 1.
float vein(float u, float v) {
    const float bend = fbm(u, v, 4, 41u, 5) * 7.0f;
    const float wave = std::abs(std::sin((u * 2.0f + v * 1.0f + bend) * TAU * 0.5f));
    const float fine = std::abs(std::sin((u * 5.0f - v * 3.0f + bend * 1.6f) * TAU * 0.5f));
    return std::pow(1.0f - wave, 10.0f) + 0.5f * std::pow(1.0f - fine, 18.0f);
}

} // namespace

TextureAsset walnutColor() {
    return colorMap([](float u, float v) {
        const glm::vec3 dark  = {0.22f, 0.12f, 0.065f};
        const glm::vec3 light = {0.42f, 0.25f, 0.13f};
        const float     shade = 0.85f + 0.3f * lattice(static_cast<int>(plankOf(v)), 7, PLANKS, 5u);
        const float     g     = std::pow(grain(u, v), 3.0f);
        const float     speck = fbm(u * 3.0f, v * 3.0f, 32, 61u, 3);
        glm::vec3       c     = glm::mix(light, dark, g * 0.5f) * shade * (0.9f + 0.2f * speck);
        return glm::mix(c, c * 0.35f, groove(v));
    });
}

TextureAsset walnutNormal() {
    return normalMap([](float u, float v) { return grain(u, v) * 0.025f - groove(v) * 0.8f; }, 2.0f);
}

// Foam on the sea: streaks drawn out along the wind (U), broken into patches, lacy within.
float foam(float u, float v) {
    const float streak = fbm(u * 2.0f, v * 9.0f, 4, 101u, 5);
    const float patch  = fbm(u, v, 3, 103u, 4);
    const float lace   = fbm(u * 8.0f, v * 8.0f, 8, 107u, 4);
    const float edge   = std::clamp((streak - 0.6f) / 0.12f, 0.0f, 1.0f);
    const float where  = std::clamp((patch - 0.45f) / 0.2f, 0.0f, 1.0f);
    return edge * where * (0.55f + 0.45f * lace);
}

TextureAsset marbleColor(bool white) {
    return colorMap([white](float u, float v) {
        const float     cloud = fbm(u, v, 3, white ? 71u : 73u, 5);
        const float     veins = std::min(vein(u, v), 1.0f);
        if (white) {
            const glm::vec3 base = glm::mix(glm::vec3(0.93f, 0.92f, 0.89f), glm::vec3(0.84f, 0.83f, 0.81f), cloud);
            return glm::mix(base, glm::vec3(0.45f, 0.45f, 0.48f), veins * 0.75f);
        }
        const glm::vec3 base = glm::mix(glm::vec3(0.035f, 0.035f, 0.045f), glm::vec3(0.08f, 0.08f, 0.09f), cloud);
        return glm::mix(base, glm::vec3(0.55f, 0.55f, 0.58f), veins * 0.45f);
    });
}

TextureAsset seaColor() {
    return colorMap([](float u, float v) {
        const glm::vec3 deep = {0.006f, 0.018f, 0.03f};
        return glm::mix(deep, glm::vec3(0.82f, 0.86f, 0.86f), foam(u, v));
    });
}

TextureAsset seaSurface() {
    TextureAsset texture = blank(TextureInternalFormat::RGBA8, TexturePixelFormat::RGBA, 4);
    parallelFor(SIZE, 16, [&](size_t row) {
        const int y = static_cast<int>(row);
        for (int x = 0; x < SIZE; ++x) {
            const float f   = foam((x + 0.5f) / SIZE, (y + 0.5f) / SIZE);
            uint8_t*    out = &texture.pixelData[static_cast<size_t>((y * SIZE + x) * 4)];
            out[0] = 255;                              // no occlusion
            out[1] = byte(glm::mix(0.025f, 0.7f, f));  // glassy water, rough foam
            out[2] = 0;                                // not metal
            out[3] = 255;
        }
    });
    return texture;
}

TextureAsset foamLace() {
    TextureAsset texture = blank(TextureInternalFormat::SRGBA8, TexturePixelFormat::RGBA, 4);
    parallelFor(SIZE, 16, [&](size_t row) {
        const int y = static_cast<int>(row);
        for (int x = 0; x < SIZE; ++x) {
            const float u    = (x + 0.5f) / SIZE;
            const float v    = (y + 0.5f) / SIZE;
            // Thickest at the waterline (V's middle), thinning out lacy to either edge.
            const float band = 1.0f - std::abs(v * 2.0f - 1.0f);
            const float lace = fbm(u * 6.0f, v, 8, 113u, 5);
            const float a    = std::clamp((lace * band * 1.6f - 0.35f) * 2.5f, 0.0f, 1.0f);
            uint8_t*    out  = &texture.pixelData[static_cast<size_t>((y * SIZE + x) * 4)];
            out[0] = out[1] = out[2] = byte(0.88f);
            out[3] = byte(a);
        }
    });
    return texture;
}

TextureAsset marbleNormal() {
    return normalMap([](float u, float v) { return fbm(u, v, 8, 83u, 5) * 0.15f - std::min(vein(u, v), 1.0f) * 0.2f; }, 4.0f);
}

} // namespace Game::Textures
