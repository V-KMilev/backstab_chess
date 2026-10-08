#include "chess_look.h"

#include <algorithm>
#include <string>

#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"

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

// Two squares by two of the plain's chessboard, to repeat: pale stone and dark, with a fine
// seam between them.
TextureAsset checker() {
    constexpr uint32_t SIZE = 256;
    constexpr uint32_t HALF = SIZE / 2;
    TextureAsset texture;
    texture.params.width          = SIZE;
    texture.params.height         = SIZE;
    texture.params.internalFormat = TextureInternalFormat::SRGBA8;
    texture.params.format         = TexturePixelFormat::RGBA;
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.wrapT          = TextureWrapMode::Repeat;
    texture.pixelData.resize(SIZE * SIZE * 4);
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const bool     light = ((x / HALF) + (y / HALF)) % 2 == 0;
            const uint32_t edgeX = std::min(x % HALF, HALF - 1 - x % HALF);
            const uint32_t edgeY = std::min(y % HALF, HALF - 1 - y % HALF);
            const bool     seam  = std::min(edgeX, edgeY) < 2;
            uint8_t v = light ? 92 : 30;
            if (seam) v = 20;
            uint8_t* texel = &texture.pixelData[(y * SIZE + x) * 4];
            texel[0] = v;
            texel[1] = static_cast<uint8_t>(v * 0.97f);
            texel[2] = static_cast<uint8_t>(v * 0.93f);
            texel[3] = 255;
        }
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

    MaterialAsset table;
    table.albedo             = {0.11f, 0.06f, 0.035f, 1.0f};
    table.roughness          = 0.6f;
    table.clearcoat          = 0.5f;
    table.clearcoatRoughness = 0.35f;  // satin: the lamp spreads into a sheen, not a second bulb
    add(resources, table, "chess:table");

    // The plain the table stands on: a chessboard to the horizon, under a skin of water that
    // mirrors the sky.
    MaterialAsset floor;
    floor.albedo        = {1.0f, 1.0f, 1.0f, 1.0f};
    floor.albedoTexture = resources.add(checker(), "chess:checker");
    floor.roughness     = 0.05f;
    add(resources, floor, "chess:floor");

    MaterialAsset brass;
    brass.albedo    = {0.86f, 0.64f, 0.32f, 1.0f};
    brass.metallic  = 1.0f;
    brass.roughness = 0.28f;
    add(resources, brass, "chess:brass");

    // The leather inlay the board sits on.
    MaterialAsset leather;
    leather.albedo         = {0.012f, 0.04f, 0.024f, 1.0f};
    leather.roughness      = 0.7f;
    leather.sheenColor     = {0.05f, 0.12f, 0.08f};
    leather.sheenRoughness = 0.4f;
    add(resources, leather, "chess:leather");

    // The far mountains: dark, rough stone the haze greys.
    MaterialAsset rock;
    rock.albedo    = {0.16f, 0.15f, 0.15f, 1.0f};
    rock.roughness = 0.9f;
    add(resources, rock, "chess:rock");

    // The hints glow faintly through the board's lacquer, rather than sit on it as decals.
    MaterialAsset hint;
    hint.type             = MaterialType::Transparent;
    hint.albedo           = {0.35f, 0.75f, 1.0f, 0.35f};
    hint.emission         = {0.35f, 0.75f, 1.0f};
    hint.emissiveStrength = 1.5f;
    hint.roughness        = 0.4f;
    add(resources, hint, "chess:hint");

    MaterialAsset chosen = hint;
    chosen.albedo   = {1.0f, 0.72f, 0.3f, 0.45f};
    chosen.emission = {1.0f, 0.72f, 0.3f};
    add(resources, chosen, "chess:chosen");

    MaterialAsset duel = hint;
    duel.albedo   = {1.0f, 0.22f, 0.2f, 0.45f};
    duel.emission = {1.0f, 0.22f, 0.2f};
    add(resources, duel, "chess:duel");
}

MaterialHandle piece(ResourceManager& resources, PieceSet set, Chess::Color side) {
    return resources.findByName<MaterialAsset>(pieceName(set, side));
}

MaterialHandle board(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:board"); }
MaterialHandle table(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:table"); }
MaterialHandle hint(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:hint"); }
MaterialHandle chosen(ResourceManager& resources) { return resources.findByName<MaterialAsset>("chess:chosen"); }
MaterialHandle floor(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:floor"); }
MaterialHandle brass(ResourceManager& resources)  { return resources.findByName<MaterialAsset>("chess:brass"); }
MaterialHandle rock(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:rock"); }
MaterialHandle leather(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:leather"); }
MaterialHandle tileLight(ResourceManager& resources) { return piece(resources, PieceSet::Stone, Chess::Color::White); }
MaterialHandle tileDark(ResourceManager& resources)  { return piece(resources, PieceSet::Stone, Chess::Color::Black); }

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
MaterialHandle duel(ResourceManager& resources)   { return resources.findByName<MaterialAsset>("chess:duel"); }

} // namespace ChessLook

} // namespace Game
