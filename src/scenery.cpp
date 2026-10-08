#include "scenery.h"

#include <cmath>
#include <iterator>

#include "core/math/random.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

#include "chess_look.h"
#include "world.h"

namespace Game {

namespace {

using Chess::Color;
using Chess::PieceType;

constexpr uint64_t SEED     = 0xBAC5AB;  ///< The world's layout; every game has the same one.
constexpr int      LANTERNS = 9;
constexpr float    CLEAR    = 10.0f;     ///< Nothing afloat comes nearer the table's middle.

// A piece's height and radius, life size, for floating it at its waterline.
struct Build {
    float height;
    float radius;
};

Build buildOf(PieceType type) {
    switch (type) {
        case PieceType::Pawn:   return {0.054f, 0.014f};
        case PieceType::Knight: return {0.075f, 0.019f};
        case PieceType::Bishop: return {0.086f, 0.016f};
        case PieceType::Rook:   return {0.061f, 0.018f};
        case PieceType::Queen:  return {0.090f, 0.020f};
        default:                return {0.095f, 0.021f};
    }
}

// A flat square facing up, @p size across, its UVs repeating every @p tile metres.
MeshAsset seaMesh(float size, float tile) {
    MeshAsset       mesh;
    const float     h  = size * 0.5f;
    const float     uv = h / tile;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);
    mesh.vertices = {
        {{-h, 0.0f, -h}, up, {-uv, -uv}, tangent},
        {{h, 0.0f, -h}, up, {uv, -uv}, tangent},
        {{h, 0.0f, h}, up, {uv, uv}, tangent},
        {{-h, 0.0f, h}, up, {-uv, uv}, tangent},
    };
    mesh.indices   = {0, 2, 1, 0, 3, 2};
    mesh.boundsMin = {-h, -0.01f, -h};
    mesh.boundsMax = {h, 0.01f, h};
    return mesh;
}

// A direction in the horizontal plane, @p angle round from +Z toward +X.
glm::vec3 round(float angle) { return {std::sin(angle), 0.0f, std::cos(angle)}; }

// A random unit vector, a little biased up so nothing turns about a level axis only.
glm::vec3 anyAxis(Math::Rng& rng) {
    return glm::normalize(glm::vec3(rng.nextFloat(-1.0f, 1.0f), rng.nextFloat(-1.0f, 1.0f) + 0.05f, rng.nextFloat(-1.0f, 1.0f)));
}

} // namespace

void Scenery::onStart() {
    ResourceManager& res = resources();
    res.add(generateCylinder(0.5f, 1.0f, 96), "scenery:drum");
    res.add(generateSphere(32, 16), "scenery:ball");
    res.add(generateCube(), "scenery:block");
    res.add(seaMesh(SEA_SIZE, SEA_TILE), "scenery:sea");

    spawnTable();
    spawnLanterns();
    spawnSea();
    spawnFlotsam();
}

void Scenery::onUpdate(float dt) {
    m_time += dt;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    for (const Drifter& d : m_drifters) {
        Transform* t = scene().tryGet<Transform>(d.id);
        if (!t) continue;
        const float     wave  = m_time * d.rate + d.phase;
        const glm::quat round = glm::angleAxis(d.swirl * m_time, up);
        t->position = round * d.at + up * (d.bob * std::sin(wave));
        t->rotation = round * glm::angleAxis(d.rock * std::sin(wave * 0.8f + 1.3f), d.rockAxis)
            * glm::angleAxis(d.spin * m_time, d.spinAxis) * d.facing;
    }

    // The sea slides on under its own ripples, a tile at a time so it never jumps.
    if (Transform* sea = scene().tryGet<Transform>(m_sea)) {
        sea->position.x = std::fmod(m_time * 0.7f, SEA_TILE);
        sea->position.z = std::fmod(m_time * 0.4f, SEA_TILE);
    }
}

EntityId Scenery::place(const char* name, MeshHandle mesh, MaterialHandle material, const glm::vec3& at,
                        const glm::quat& facing, const glm::vec3& scale, bool shadows) {
    const EntityId id = spawn(name);
    scene().add(id, Transform{at, facing, scale});
    Mesh drawn{mesh, material};
    drawn.castShadows = shadows;
    scene().add(id, std::move(drawn));
    return id;
}

// A round walnut table with a brass rim and a glowing ring under its edge, on a column that
// goes down into the sea.
void Scenery::spawnTable() {
    ResourceManager&     res     = resources();
    const MeshHandle     drum    = res.findByName<MeshAsset>("scenery:drum");
    const MaterialHandle walnut  = ChessLook::table(res);
    const glm::quat      upright = {1.0f, 0.0f, 0.0f, 0.0f};
    const float          wide    = TABLE_RADIUS * 2.0f;

    place("Table Top", drum, walnut, {0.0f, -TABLE_THICKNESS * 0.5f, 0.0f}, upright, {wide, TABLE_THICKNESS, wide});
    place("Table Rim", drum, ChessLook::brass(res), {0.0f, -TABLE_THICKNESS * 0.5f, 0.0f}, upright, {wide + 0.16f, TABLE_THICKNESS * 0.42f, wide + 0.16f});
    place("Table Glow", drum, ChessLook::glow(res), {0.0f, -TABLE_THICKNESS - 0.05f, 0.0f}, upright, {wide - 0.3f, 0.08f, wide - 0.3f}, false);
    place("Table Apron", drum, walnut, {0.0f, -TABLE_THICKNESS - 0.25f, 0.0f}, upright, {wide * 0.55f, 0.5f, wide * 0.55f});
    const float top    = -TABLE_THICKNESS - 0.5f;
    const float bottom = SEA_LEVEL - 3.0f;
    place("Table Column", drum, walnut, {0.0f, (top + bottom) * 0.5f, 0.0f}, upright, {1.5f, top - bottom, 1.5f});
}

// Lanterns adrift round the table, warm, each lighting what is near it: the table's edge, the
// heads, the board's rim. They show little by day and carry the night.
void Scenery::spawnLanterns() {
    ResourceManager& res  = resources();
    const MeshHandle ball = res.findByName<MeshAsset>("scenery:ball");
    Math::Rng        rng(SEED, 3);
    for (int i = 0; i < LANTERNS; ++i) {
        const float     a  = glm::two_pi<float>() * (static_cast<float>(i) + rng.nextFloat(-0.3f, 0.3f)) / static_cast<float>(LANTERNS);
        const glm::vec3 at = round(a) * rng.nextFloat(8.5f, 12.0f) + glm::vec3(0.0f, rng.nextFloat(1.5f, 5.5f), 0.0f);
        const EntityId  id = place("Lantern", ball, ChessLook::lantern(res), at, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(0.32f), false);
        Light light{};
        light.type        = LightType::Point;
        light.color       = {1.0f, 0.72f, 0.4f};
        light.intensity   = 9.0f;
        light.radius      = 9.0f;
        light.castShadows = false;
        scene().add(id, light);

        Drifter d;
        d.id    = id;
        d.at    = at;
        d.bob   = rng.nextFloat(0.3f, 0.8f);
        d.phase = rng.nextFloat(0.0f, glm::two_pi<float>());
        d.swirl = 0.03f;
        m_drifters.push_back(d);
    }
}

// The sea, to the horizon, under ripples that slide slowly on.
void Scenery::spawnSea() {
    ResourceManager& res = resources();
    m_sea = place("Sea", res.findByName<MeshAsset>("scenery:sea"), ChessLook::sea(res), {0.0f, SEA_LEVEL, 0.0f},
                  {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f), false);
}

// The wreck of a thousand games: pieces of every size and set afloat, lying, leaning or bobbing
// upright at their waterline, chunks of board among them, all swirling slowly round the table;
// and some flung up into the air, turning over as they hang there.
void Scenery::spawnFlotsam() {
    ResourceManager& res   = resources();
    const MeshHandle block = res.findByName<MeshAsset>("scenery:block");
    Math::Rng        rng(SEED, 1);
    const PieceType  TYPES[] = {PieceType::Pawn, PieceType::Pawn, PieceType::Pawn, PieceType::Pawn, PieceType::Knight,
                                PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Rook, PieceType::Queen, PieceType::King};
    const glm::vec3 up(0.0f, 1.0f, 0.0f);

    const auto piece = [&](const glm::vec3& at, const glm::quat& facing, float scale, Drifter d) {
        const PieceType type = TYPES[rng.nextInt(0, static_cast<int>(std::size(TYPES)) - 1)];
        const float     roll = rng.nextFloat();
        const PieceSet  set  = roll < 0.4f ? PieceSet::Stone : roll < 0.7f ? PieceSet::Classic : roll < 0.88f ? PieceSet::Metal : PieceSet::Glass;
        const MaterialHandle material = ChessLook::piece(res, set, rng.nextBool() ? Color::White : Color::Black);
        const EntityId id = place("Flotsam", res.findByName<MeshAsset>(ChessLook::pieceMesh(type)), material, at, facing, glm::vec3(scale), false);
        if (type == PieceType::Bishop) {
            const EntityId top = spawn("Flotsam Top", id);
            scene().add(top, Transform{});
            Mesh ball{res.findByName<MeshAsset>("chess:bishop_top"), material};
            ball.castShadows = false;
            scene().add(top, std::move(ball));
        }
        d.id     = id;
        d.at     = at;
        d.facing = facing;
        m_drifters.push_back(d);
        return type;
    };

    // Afloat: the nearer, the smaller, so nothing looms over the table.
    for (int i = 0; i < 150; ++i) {
        const float far   = CLEAR + std::pow(rng.nextFloat(), 1.6f) * 420.0f;
        const float scale = std::max(far * rng.nextFloat(0.8f, 3.5f), 12.0f);
        const float yaw   = rng.nextFloat(0.0f, glm::two_pi<float>());
        const float lying = rng.nextFloat();
        Drifter d;
        d.rockAxis = round(rng.nextFloat(0.0f, glm::two_pi<float>()));
        d.rock     = glm::radians(rng.nextFloat(3.0f, 10.0f));
        d.rate     = rng.nextFloat(0.35f, 0.8f);
        d.phase    = rng.nextFloat(0.0f, glm::two_pi<float>());
        d.swirl    = rng.nextFloat(0.004f, 0.012f) * (far < 120.0f ? 1.0f : 0.5f);
        glm::vec3 at = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far + glm::vec3(0.0f, SEA_LEVEL, 0.0f);
        glm::quat facing = glm::angleAxis(yaw, up);
        // A rough guess at the piece's size: a king's, so the tall ones float a little high.
        const Build size = buildOf(PieceType::King);
        if (lying < 0.55f) {
            facing = glm::angleAxis(glm::radians(rng.nextFloat(80.0f, 100.0f)), round(yaw)) * facing;  // on its side
            at.y  -= size.radius * scale * 0.2f;
        } else if (lying < 0.85f) {
            facing = glm::angleAxis(glm::radians(rng.nextFloat(15.0f, 50.0f)), round(yaw)) * facing;   // leaning
            at.y  -= size.height * scale * rng.nextFloat(0.2f, 0.5f);
        } else {
            at.y -= size.height * scale * rng.nextFloat(0.1f, 0.4f);                                     // upright
        }
        d.bob = 0.004f * scale;
        piece(at, facing, scale, d);
    }

    // Chunks of board afloat among them, two squares by two on each face.
    for (int i = 0; i < 70; ++i) {
        const float     far  = CLEAR + 2.0f + std::pow(rng.nextFloat(), 1.4f) * 300.0f;
        const float     size = std::max(far * rng.nextFloat(0.06f, 0.18f), 1.5f);
        const glm::vec3 at   = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far + glm::vec3(0.0f, SEA_LEVEL + size * 0.02f, 0.0f);
        const glm::quat tilt = glm::angleAxis(glm::radians(rng.nextFloat(0.0f, 25.0f)), round(rng.nextFloat(0.0f, glm::two_pi<float>())))
            * glm::angleAxis(rng.nextFloat(0.0f, glm::two_pi<float>()), up);
        const EntityId id = place("Wreck", block, ChessLook::wreck(res), at, tilt, {size, size * 0.12f, size}, false);
        Drifter d;
        d.id       = id;
        d.at       = at;
        d.facing   = tilt;
        d.rockAxis = round(rng.nextFloat(0.0f, glm::two_pi<float>()));
        d.rock     = glm::radians(rng.nextFloat(4.0f, 12.0f));
        d.bob      = size * 0.04f;
        d.rate     = rng.nextFloat(0.4f, 0.9f);
        d.phase    = rng.nextFloat(0.0f, glm::two_pi<float>());
        d.swirl    = rng.nextFloat(0.004f, 0.012f);
        m_drifters.push_back(d);
    }

    // Flung up into the air: pieces and board chunks hanging there, turning over.
    for (int i = 0; i < 40; ++i) {
        const float     far = 25.0f + std::pow(rng.nextFloat(), 0.8f) * 220.0f;
        const glm::vec3 at  = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far
            + glm::vec3(0.0f, rng.nextFloat(6.0f, 18.0f) + far * rng.nextFloat(0.05f, 0.35f), 0.0f);
        const glm::quat facing = glm::angleAxis(rng.nextFloat(0.0f, glm::two_pi<float>()), anyAxis(rng));
        Drifter d;
        d.spinAxis = anyAxis(rng);
        d.spin     = rng.nextFloat(0.04f, 0.2f) * (rng.nextBool() ? 1.0f : -1.0f);
        d.bob      = rng.nextFloat(0.8f, 3.0f);
        d.rate     = rng.nextFloat(0.2f, 0.45f);
        d.phase    = rng.nextFloat(0.0f, glm::two_pi<float>());
        d.swirl    = 0.006f;
        if (i % 3 == 0) {
            const float    size = far * rng.nextFloat(0.04f, 0.1f);
            const EntityId id   = place("Flung Wreck", block, ChessLook::wreck(res), at, facing, {size, size * 0.12f, size}, false);
            d.id     = id;
            d.at     = at;
            d.facing = facing;
            m_drifters.push_back(d);
        } else {
            piece(at, facing, far * rng.nextFloat(0.8f, 2.5f), d);
        }
    }
}

} // namespace Game
