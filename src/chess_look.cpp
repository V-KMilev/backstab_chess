#include "chess_look.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/math/random.h"
#include "platform/threading/thread_pool.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"

#include "profile.h"
#include "textures.h"

namespace Game {

namespace {

constexpr const char* TEXTURES = "assets/chess_set/textures/chess_set_";

const char* const SET_KEYS[] = {"classic", "glass", "metal", "stone"};

std::string pieceName(PieceSet set, Chess::Color side) {
    return std::string("chess:") + SET_KEYS[static_cast<int>(set)]
        + (side == Chess::Color::White ? "_white" : "_black");
}

TextureHandle texture(ResourceManager& resources, const char* part, const char* map) {
    return resources.findByName<TextureAsset>(std::string(TEXTURES) + part + "_" + map + "_2k.jpg");
}

// The set's own look: its colour, normal and AO-roughness-metal maps, taken as they are.
MaterialAsset textured(ResourceManager& resources, const char* part) {
    MaterialAsset m;
    m.albedo                     = {1.0f, 1.0f, 1.0f, 1.0f};
    m.metallic                   = 1.0f;
    m.roughness                  = 1.0f;
    m.albedoTexture              = texture(resources, part, "diff");
    m.normalTexture              = texture(resources, part, "nor_gl");
    m.aoMetallicRoughnessTexture = texture(resources, part, "arm");
    return m;
}

// The sea's ripples, a tangent-space normal map that repeats: a few hundred small waves with
// sizes and headings spread round the wind's as real chop is, shorter ones gentler, each a
// whole number of cycles across the tile so its edges meet. Each wave's slope is summed from
// tables of its cosine and sine along each axis, so the sum costs no trigonometry per texel.
TextureAsset ripples() {
    constexpr int   SIZE  = 1024;
    constexpr int   WAVES = 240;
    constexpr float WIND  = 0.35f;  ///< The wind's heading, radians.
    const float     tau   = 6.28318530718f;

    struct Wave {
        int   fx, fy;
        float amplitude, phase;
    };
    std::vector<Wave> waves;
    Math::Rng rng(0x5EA, 9);
    while (static_cast<int>(waves.size()) < WAVES) {
        // Cycles across the tile, many more short than long; headings spread round the wind's.
        const float cycles  = 3.0f + std::pow(rng.nextFloat(), 1.8f) * 85.0f;
        const float heading = WIND + rng.nextFloat(-1.0f, 1.0f) * rng.nextFloat(0.0f, 1.6f);
        const int   fx      = static_cast<int>(std::round(std::cos(heading) * cycles));
        const int   fy      = static_cast<int>(std::round(std::sin(heading) * cycles));
        if (fx == 0 && fy == 0) continue;
        // A slope spectrum falling with frequency, as the sea's does.
        const float f = std::sqrt(static_cast<float>(fx * fx + fy * fy));
        waves.push_back({fx, fy, std::pow(f, -1.9f) * rng.nextFloat(0.5f, 1.0f), rng.nextFloat(0.0f, tau)});
    }

    // cos and sin of 2 pi f x / SIZE for every frequency used, along a row.
    constexpr int MAX_F = 96;
    std::vector<float> cosT(static_cast<size_t>((2 * MAX_F + 1) * SIZE));
    std::vector<float> sinT(cosT.size());
    for (int f = -MAX_F; f <= MAX_F; ++f) {
        for (int x = 0; x < SIZE; ++x) {
            const float a = tau * static_cast<float>(f * x) / SIZE;
            cosT[static_cast<size_t>((f + MAX_F) * SIZE + x)] = std::cos(a);
            sinT[static_cast<size_t>((f + MAX_F) * SIZE + x)] = std::sin(a);
        }
    }

    TextureAsset texture;
    texture.params.width          = SIZE;
    texture.params.height         = SIZE;
    texture.params.internalFormat = TextureInternalFormat::RG8;
    texture.params.format         = TexturePixelFormat::RG;
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.wrapT          = TextureWrapMode::Repeat;
    texture.pixelData.resize(static_cast<size_t>(SIZE * SIZE * 2));
    std::vector<float> dx(static_cast<size_t>(SIZE * SIZE), 0.0f);
    std::vector<float> dy(dx.size(), 0.0f);
    parallelFor(SIZE, 8, [&](size_t row) {
        const int y = static_cast<int>(row);
        float* rowX = &dx[static_cast<size_t>(y * SIZE)];
        float* rowY = &dy[static_cast<size_t>(y * SIZE)];
        for (const Wave& w : waves) {
            // slope = A * 2 pi f * cos(2 pi (fx x + fy y) + phase), split by angle addition.
            const float  cp = std::cos(w.phase);
            const float  sp = std::sin(w.phase);
            const float* cx = &cosT[static_cast<size_t>((w.fx + MAX_F) * SIZE)];
            const float* sx = &sinT[static_cast<size_t>((w.fx + MAX_F) * SIZE)];
            const float* cy = &cosT[static_cast<size_t>((w.fy + MAX_F) * SIZE)];
            const float* sy = &sinT[static_cast<size_t>((w.fy + MAX_F) * SIZE)];
            // cos(b + phase) and sin(b + phase) for the row's part b = 2 pi fy y.
            const float cb = cy[y] * cp - sy[y] * sp;
            const float sb = sy[y] * cp + cy[y] * sp;
            for (int x = 0; x < SIZE; ++x) {
                const float c = (cx[x] * cb - sx[x] * sb) * w.amplitude;
                rowX[x] += c * static_cast<float>(w.fx);
                rowY[x] += c * static_cast<float>(w.fy);
            }
        }
    });
    for (size_t i = 0; i < dx.size(); ++i) {
        const glm::vec3 n = glm::normalize(glm::vec3(-dx[i] * 0.35f, -dy[i] * 0.35f, 1.0f));
        texture.pixelData[i * 2 + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
        texture.pixelData[i * 2 + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
    }
    return texture;
}

void add(ResourceManager& resources, MaterialAsset material, const std::string& name) {
    resources.add(std::move(material), name);
}

} // namespace

const char* pieceSetName(PieceSet set) {
    switch (set) {
        case PieceSet::Classic: return "Classic wood";
        case PieceSet::Glass:   return "Glass";
        case PieceSet::Metal:   return "Gold and gunmetal";
        case PieceSet::Stone:   return "Marble and obsidian";
        default:                return "";
    }
}

namespace ChessLook {

void build(ResourceManager& resources) {
    using Chess::Color;

    add(resources, textured(resources, "pieces_white"), pieceName(PieceSet::Classic, Color::White));
    add(resources, textured(resources, "pieces_black"), pieceName(PieceSet::Classic, Color::Black));
    add(resources, textured(resources, "board"), "chess:board");

    // Clear glass, and a smoked glass that darkens with the thickness light crosses.
    MaterialAsset clear;
    clear.type            = MaterialType::Transparent;
    // Thick, like a paperweight: the body tints what it bends.
    clear.albedo              = {0.78f, 0.88f, 1.0f, 0.75f};
    clear.roughness           = 0.03f;
    clear.transmission        = 0.55f;
    clear.ior                 = 1.6f;
    clear.thicknessFactor     = 1.0f;
    clear.attenuationColor    = {0.55f, 0.78f, 0.95f};
    clear.attenuationDistance = 0.22f;
    add(resources, clear, pieceName(PieceSet::Glass, Color::White));

    MaterialAsset smoked = clear;
    smoked.albedo              = {0.07f, 0.08f, 0.11f, 0.9f};
    smoked.attenuationColor    = {0.08f, 0.08f, 0.11f};
    smoked.attenuationDistance = 0.12f;
    add(resources, smoked, pieceName(PieceSet::Glass, Color::Black));

    // Polished gold against a brushed gunmetal.
    MaterialAsset gold;
    gold.albedo    = {1.0f, 0.77f, 0.34f, 1.0f};
    gold.metallic  = 1.0f;
    gold.roughness = 0.16f;
    add(resources, gold, pieceName(PieceSet::Metal, Color::White));

    MaterialAsset gunmetal;
    gunmetal.albedo    = {0.22f, 0.23f, 0.25f, 1.0f};
    gunmetal.metallic  = 1.0f;
    gunmetal.roughness = 0.32f;
    add(resources, gunmetal, pieceName(PieceSet::Metal, Color::Black));

    // White marble, light through its edges, under a lacquer; obsidian under the same.
    MaterialAsset marble;
    marble.albedo             = {0.9f, 0.88f, 0.84f, 1.0f};
    marble.roughness          = 0.35f;
    marble.clearcoat          = 1.0f;
    marble.clearcoatRoughness = 0.04f;
    marble.subsurface         = 0.35f;
    marble.subsurfaceColor    = {1.0f, 0.86f, 0.72f};
    add(resources, marble, pieceName(PieceSet::Stone, Color::White));

    MaterialAsset obsidian;
    obsidian.albedo             = {0.035f, 0.035f, 0.042f, 1.0f};
    obsidian.roughness          = 0.12f;
    obsidian.clearcoat          = 1.0f;
    obsidian.clearcoatRoughness = 0.02f;
    add(resources, obsidian, pieceName(PieceSet::Stone, Color::Black));

    // Lacquered walnut: planks and grain under a clear coat.
    MaterialAsset table;
    table.albedoTexture      = resources.add(Textures::walnutColor(), "chess:walnut");
    table.normalTexture      = resources.add(Textures::walnutNormal(), "chess:walnut_normal");
    table.roughness          = 0.5f;
    table.clearcoat          = 0.7f;
    table.clearcoatRoughness = 0.12f;
    add(resources, table, "chess:table");

    // The sea: deep and glassy under ripples, foam streaking it, a mirror for the sky and what
    // floats on it.
    MaterialAsset sea;
    sea.albedo        = {1.0f, 1.0f, 1.0f, 1.0f};
    sea.albedoTexture = resources.add(Textures::seaColor(), "chess:sea_color");
    sea.roughness     = 1.0f;  // the surface map says how rough
    sea.aoMetallicRoughnessTexture = resources.add(Textures::seaSurface(), "chess:sea_surface");
    // Light through the crests, green as the sea's is.
    sea.subsurface      = 0.5f;
    sea.subsurfaceColor = {0.08f, 0.4f, 0.32f};
    sea.normalTexture = resources.add(ripples(), "chess:ripples");
    sea.normalScale   = 0.6f;
    add(resources, sea, "chess:sea");

    // Single squares of a board, adrift: white marble and black, polished.
    const TextureHandle stoneNormal = resources.add(Textures::marbleNormal(), "chess:stone_normal");
    for (const bool white : {true, false}) {
        MaterialAsset tile;
        tile.albedoTexture = resources.add(Textures::marbleColor(white), white ? "chess:marble" : "chess:black_marble");
        tile.normalTexture = stoneNormal;
        tile.roughness     = 0.12f;
        tile.clearcoat     = 0.5f;
        add(resources, tile, white ? "chess:tile_white" : "chess:tile_black");
    }

    // The foam where something afloat meets the water.
    MaterialAsset foam;
    foam.type          = MaterialType::Transparent;
    foam.albedoTexture = resources.add(Textures::foamLace(), "chess:foam_lace");
    foam.roughness     = 0.7f;
    add(resources, foam, "chess:foam");

    MaterialAsset brass;
    brass.albedo    = {0.86f, 0.64f, 0.32f, 1.0f};
    brass.metallic  = 1.0f;
    brass.roughness = 0.28f;
    add(resources, brass, "chess:brass");

    // The glow under the table's rim, and the lanterns adrift round it.
    MaterialAsset glow;
    glow.albedo           = {0.0f, 0.0f, 0.0f, 1.0f};
    glow.emission         = {1.0f, 0.62f, 0.28f};
    glow.emissiveStrength = 6.0f;
    add(resources, glow, "chess:glow");
    MaterialAsset lantern;
    lantern.albedo           = {1.0f, 0.85f, 0.6f, 1.0f};
    lantern.roughness        = 0.08f;
    lantern.clearcoat        = 1.0f;
    lantern.emission         = {1.0f, 0.7f, 0.38f};
    lantern.emissiveStrength = 5.0f;
    add(resources, lantern, "chess:lantern");


    // The hints glow faintly through the board's lacquer, rather than sit on it as decals.
    MaterialAsset hint;
    hint.type             = MaterialType::Transparent;
    hint.albedo           = {0.35f, 0.75f, 1.0f, 0.6f};
    hint.emission         = {0.35f, 0.75f, 1.0f};
    hint.emissiveStrength = 3.0f;
    hint.roughness        = 0.4f;
    add(resources, hint, "chess:hint");

    // The last move's two squares, faintly.
    MaterialAsset last = hint;
    last.albedo           = {1.0f, 0.82f, 0.45f, 0.22f};
    last.emission         = {1.0f, 0.82f, 0.45f};
    last.emissiveStrength = 0.6f;
    add(resources, last, "chess:last");

    MaterialAsset chosen = hint;
    chosen.albedo   = {1.0f, 0.72f, 0.3f, 0.65f};
    chosen.emission = {1.0f, 0.72f, 0.3f};
    add(resources, chosen, "chess:chosen");

    MaterialAsset duel = hint;
    duel.albedo   = {1.0f, 0.22f, 0.2f, 0.45f};
    duel.emission = {1.0f, 0.22f, 0.2f};
    add(resources, duel, "chess:duel");
}

// A player's pieces as they designed them, for one side: the side keeps them light or dark, so
// the board reads whatever anyone picked.
MaterialAsset designed(ResourceManager& resources, const PieceLook& look, Chess::Color side) {
    const bool      white = side == Chess::Color::White;
    const glm::vec3 tint  = hsv(look.hue, look.saturation, 1.0f);
    const float     smooth = glm::mix(0.65f, 0.04f, look.shine);  // roughness
    MaterialAsset   m;
    switch (look.finish) {
        case Finish::Wood: {
            m = textured(resources, white ? "pieces_white" : "pieces_black");
            m.albedo    = glm::vec4(glm::mix(glm::vec3(1.0f), tint, look.saturation * 0.7f), 1.0f);
            m.metallic  = 0.0f;
            m.roughness = glm::mix(1.0f, 0.5f, look.shine);
            m.clearcoat = look.shine * 0.8f;
            m.clearcoatRoughness = 0.15f;
            break;
        }
        case Finish::Stone: {
            m.albedoTexture = resources.findByName<TextureAsset>(white ? "chess:marble" : "chess:black_marble");
            m.normalTexture = resources.findByName<TextureAsset>("chess:stone_normal");
            m.albedo        = glm::vec4(glm::mix(glm::vec3(1.0f), tint, look.saturation * (white ? 0.6f : 0.9f)), 1.0f);
            m.roughness     = smooth;
            m.clearcoat     = look.shine * 0.7f;
            break;
        }
        case Finish::Metal: {
            m.albedo    = glm::vec4(hsv(look.hue, look.saturation * (white ? 0.75f : 0.6f), white ? 0.95f : 0.22f), 1.0f);
            m.metallic  = 1.0f;
            m.roughness = smooth;
            break;
        }
        case Finish::Glass:
        case Finish::Gem: {
            const bool gem = look.finish == Finish::Gem;
            m.type         = MaterialType::Transparent;
            m.albedo       = white ? glm::vec4(hsv(look.hue, look.saturation * (gem ? 0.9f : 0.4f), 0.95f), gem ? 0.5f : 0.6f)
                                   : glm::vec4(hsv(look.hue, look.saturation * (gem ? 0.9f : 0.5f), gem ? 0.35f : 0.1f), gem ? 0.7f : 0.85f);
            m.roughness    = glm::mix(0.3f, 0.02f, look.shine);
            m.transmission = white ? 0.7f : 0.55f;
            m.ior          = gem ? 2.2f : 1.6f;
            m.clearcoat    = gem ? 1.0f : 0.3f;
            m.thicknessFactor     = 0.8f;
            m.attenuationColor    = glm::mix(glm::vec3(1.0f), tint, white ? 0.6f : 0.9f) * (white ? 1.0f : 0.15f);
            m.attenuationDistance = 0.25f;
            break;
        }
        case Finish::Neon:
        default: {
            m.albedo           = white ? glm::vec4(0.82f, 0.82f, 0.85f, 1.0f) : glm::vec4(0.04f, 0.04f, 0.05f, 1.0f);
            m.roughness        = 0.25f;
            m.clearcoat        = 1.0f;
            m.emission         = hsv(look.hue, std::max(look.saturation, 0.7f), 1.0f);
            m.emissiveStrength = 1.5f + 4.0f * look.glow;
            return m;
        }
    }
    if (look.glow > 0.0f) {
        m.emission         = tint;
        m.emissiveStrength = 3.0f * look.glow;
    }
    return m;
}

MaterialHandle designed(ResourceManager& resources, const std::string& name, const PieceLook& look, Chess::Color side) {
    MaterialAsset material = designed(resources, look, side);
    // Made again in place: whatever wears it changes with it.
    if (const MaterialHandle found = resources.findByName<MaterialAsset>(name)) {
        resources.swapValue(found, material);
        return found;
    }
    return resources.add(std::move(material), name);
}

MaterialHandle glowing(ResourceManager& resources, const std::string& name, const glm::vec3& color, float alpha, float strength) {
    MaterialAsset m;
    m.type             = MaterialType::Transparent;
    m.albedo           = glm::vec4(color, alpha);
    m.emission         = color;
    m.emissiveStrength = strength;
    m.roughness        = 0.4f;
    m.doubleSided      = true;
    if (const MaterialHandle found = resources.findByName<MaterialAsset>(name)) {
        resources.swapValue(found, m);
        return found;
    }
    return resources.add(std::move(m), name);
}

MaterialHandle piece(ResourceManager& resources, PieceSet set, Chess::Color side) {
    return resources.findByName<MaterialAsset>(pieceName(set, side));
}

MaterialHandle board(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:board"); }
MaterialHandle table(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:table"); }
MaterialHandle hint(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:hint"); }
MaterialHandle chosen(ResourceManager& resources) { return resources.findByName<MaterialAsset>("chess:chosen"); }
MaterialHandle sea(ResourceManager& resources)    { return resources.findByName<MaterialAsset>("chess:sea"); }
MaterialHandle foam(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:foam"); }
MaterialHandle tile(ResourceManager& resources, bool white) {
    return resources.findByName<MaterialAsset>(white ? "chess:tile_white" : "chess:tile_black");
}
MaterialHandle brass(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:brass"); }
MaterialHandle glow(ResourceManager& resources)      { return resources.findByName<MaterialAsset>("chess:glow"); }
MaterialHandle lantern(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:lantern"); }

const char* pieceMesh(Chess::PieceType type) {
    switch (type) {
        case Chess::PieceType::Pawn:   return "chess:pawn";
        case Chess::PieceType::Knight: return "chess:knight";
        case Chess::PieceType::Bishop: return "chess:bishop";
        case Chess::PieceType::Rook:   return "chess:rook";
        case Chess::PieceType::Queen:  return "chess:queen";
        case Chess::PieceType::King:   return "chess:king";
        default:                       return "";
    }
}
MaterialHandle last(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:last"); }
MaterialHandle duel(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:duel"); }

} // namespace ChessLook

} // namespace Game
