#include "scenery.h"

#include <cmath>

#include "core/math/random.h"
#include "ecs/component/render/mesh.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

#include "chess_look.h"
#include "world.h"

namespace Game {

namespace {

using Chess::Color;
using Chess::PieceType;

constexpr uint64_t SEED = 0xBAC5AB;  ///< The world's layout; every game has the same one.

// A piece's height and radius, life size, for standing, sinking and laying a colossus down.
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

// A flat square facing up, @p size across, its UVs counting squares of @p square two to a
// repeat of the checker, a light square centred on the origin.
MeshAsset floorMesh(float size, float square) {
    MeshAsset       mesh;
    const float     h  = size * 0.5f;
    const float     uv = h / (square * 2.0f);
    const float     o  = 0.25f;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);
    mesh.vertices = {
        {{-h, 0.0f, -h}, up, {o - uv, o - uv}, tangent},
        {{h, 0.0f, -h}, up, {o + uv, o - uv}, tangent},
        {{h, 0.0f, h}, up, {o + uv, o + uv}, tangent},
        {{-h, 0.0f, h}, up, {o - uv, o + uv}, tangent},
    };
    mesh.indices   = {0, 2, 1, 0, 3, 2};
    mesh.boundsMin = {-h, -0.01f, -h};
    mesh.boundsMax = {h, 0.01f, h};
    return mesh;
}

// The turn that takes +Y onto @p direction.
glm::quat upTo(const glm::vec3& direction) {
    const glm::vec3 d    = glm::normalize(direction);
    const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), d);
    const float     s    = glm::length(axis);
    if (s < 1e-5f) return d.y > 0.0f ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::angleAxis(std::atan2(s, d.y), axis / s);
}

// A direction in the horizontal plane, @p angle round from +Z toward +X.
glm::vec3 round(float angle) { return {std::sin(angle), 0.0f, std::cos(angle)}; }

} // namespace

void Scenery::onStart() {
    ResourceManager& res = resources();
    res.add(generateCylinder(0.5f, 1.0f, 96), "scenery:drum");
    res.add(generateCone(0.5f, 1.0f, 64), "scenery:cone");
    res.add(generateSphere(32, 16), "scenery:ball");
    res.add(generateCube(), "scenery:block");
    res.add(floorMesh(FLOOR_SIZE, FLOOR_SQUARE), "scenery:plain");

    spawnTable();
    spawnPlain();
    spawnColossi();
    spawnShards();
}

void Scenery::onUpdate(float dt) {
    m_time += dt;
    for (const Drifter& d : m_drifters) {
        Transform* t = scene().tryGet<Transform>(d.id);
        if (!t) continue;
        t->rotation = glm::angleAxis(d.spin * m_time, d.axis) * d.facing;
        t->position = d.at + glm::vec3(0.0f, d.bob * std::sin(m_time * 0.35f + d.phase), 0.0f);
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

// A round walnut table: a green leather inlay under the board, brass studs round a brass
// rim, a turned column with brass rings, and four splayed legs on brass balls.
void Scenery::spawnTable() {
    ResourceManager&     res     = resources();
    const MeshHandle     drum    = res.findByName<MeshAsset>("scenery:drum");
    const MeshHandle     ball    = res.findByName<MeshAsset>("scenery:ball");
    const MaterialHandle walnut  = ChessLook::table(res);
    const MaterialHandle brass   = ChessLook::brass(res);
    const glm::quat      upright = {1.0f, 0.0f, 0.0f, 0.0f};
    const float          wide    = TABLE_RADIUS * 2.0f;

    place("Table Top", drum, walnut, {0.0f, -TABLE_THICKNESS * 0.5f, 0.0f}, upright, {wide, TABLE_THICKNESS, wide});
    place("Table Rim", drum, brass, {0.0f, -TABLE_THICKNESS * 0.5f, 0.0f}, upright, {wide + 0.16f, TABLE_THICKNESS * 0.42f, wide + 0.16f});
    place("Table Inlay", drum, ChessLook::leather(res), {0.0f, 0.004f, 0.0f}, upright, {8.6f, 0.02f, 8.6f}, false);
    for (int i = 0; i < 40; ++i) {
        const glm::vec3 at = round(glm::two_pi<float>() * static_cast<float>(i) / 40.0f) * (TABLE_RADIUS - 0.28f);
        place("Table Stud", ball, brass, at + glm::vec3(0.0f, 0.02f, 0.0f), upright, glm::vec3(0.16f), false);
    }
    place("Table Apron", drum, walnut, {0.0f, -TABLE_THICKNESS - 0.25f, 0.0f}, upright, {6.6f, 0.5f, 6.6f});

    // The column, turned: brass rings top and bottom, a bulb between.
    const float top    = -TABLE_THICKNESS - 0.5f;
    const float bottom = -5.3f;
    place("Table Column", drum, walnut, {0.0f, (top + bottom) * 0.5f, 0.0f}, upright, {1.4f, top - bottom, 1.4f});
    place("Table Ring", drum, brass, {0.0f, top - 0.1f, 0.0f}, upright, {1.9f, 0.18f, 1.9f});
    place("Table Ring", drum, brass, {0.0f, bottom + 0.1f, 0.0f}, upright, {1.9f, 0.18f, 1.9f});
    place("Table Bulb", ball, walnut, {0.0f, (top + bottom) * 0.5f - 0.3f, 0.0f}, upright, {2.3f, 1.7f, 2.3f});
    place("Table Hub", drum, walnut, {0.0f, bottom - 0.3f, 0.0f}, upright, {2.2f, 0.6f, 2.2f});

    // Four legs splayed from the hub to brass balls on the plain, between the seats.
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 out  = round(glm::quarter_pi<float>() + glm::half_pi<float>() * static_cast<float>(i));
        const glm::vec3 from = out * 0.8f + glm::vec3(0.0f, bottom - 0.3f, 0.0f);
        const glm::vec3 to   = out * 3.6f + glm::vec3(0.0f, -FLOOR_DEPTH + 0.4f, 0.0f);
        place("Table Leg", drum, walnut, (from + to) * 0.5f, upTo(to - from), {0.56f, glm::length(to - from), 0.56f});
        place("Table Foot", ball, brass, to, upright, glm::vec3(0.8f));
    }
}

// The chessboard plain to the horizon, and a ring of mountains greyed by the haze, inside the
// renderer's cull distance of 500 metres.
void Scenery::spawnPlain() {
    ResourceManager& res = resources();
    place("Plain", res.findByName<MeshAsset>("scenery:plain"), ChessLook::floor(res), {0.0f, -FLOOR_DEPTH, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f), false);

    const MeshHandle peak = res.findByName<MeshAsset>("scenery:cone");
    for (int i = 0; i < 14; ++i) {
        const float a      = glm::two_pi<float>() * (static_cast<float>(i) + 0.37f * std::sin(static_cast<float>(i) * 2.3f)) / 14.0f;
        const float far    = 430.0f + 40.0f * std::sin(static_cast<float>(i) * 1.7f);
        const float height = 60.0f + 50.0f * (0.5f + 0.5f * std::sin(static_cast<float>(i) * 3.1f));
        const float wide   = 230.0f + 110.0f * (0.5f + 0.5f * std::cos(static_cast<float>(i) * 2.7f));
        place("Mountain", peak, ChessLook::rock(res), round(a) * far + glm::vec3(0.0f, -FLOOR_DEPTH + height * 0.5f - 4.0f, 0.0f),
              glm::angleAxis(a, glm::vec3(0.0f, 1.0f, 0.0f)), {wide, height, wide}, false);
    }
}

// Colossal pieces strewn over the plain: standing, sunk, toppled, leaning, and some adrift in
// the air, turning slowly. The nearer, the smaller, so none looms over the table.
void Scenery::spawnColossi() {
    ResourceManager& res = resources();
    Math::Rng        rng(SEED, 1);
    const PieceType  TYPES[] = {PieceType::Pawn, PieceType::Pawn, PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
                                PieceType::Rook, PieceType::Rook, PieceType::Queen, PieceType::King};
    for (int i = 0; i < 44; ++i) {
        const PieceType type  = TYPES[rng.nextInt(0, static_cast<int>(std::size(TYPES)) - 1)];
        const Color     side  = rng.nextBool() ? Color::White : Color::Black;
        const float     roll  = rng.nextFloat();
        const PieceSet  set   = roll < 0.7f ? PieceSet::Stone : roll < 0.85f ? PieceSet::Classic : roll < 0.95f ? PieceSet::Metal : PieceSet::Glass;
        const float     far   = 45.0f + std::pow(rng.nextFloat(), 0.8f) * 380.0f;
        const float     scale = far * rng.nextFloat(3.0f, 6.5f);
        const Build     build = buildOf(type);
        const float     tall  = build.height * scale;
        const glm::vec3 base  = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far + glm::vec3(0.0f, -FLOOR_DEPTH, 0.0f);
        const glm::quat turn  = glm::angleAxis(rng.nextFloat(0.0f, glm::two_pi<float>()), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 side3 = round(rng.nextFloat(0.0f, glm::two_pi<float>()));
        const glm::vec3 tip   = glm::cross(side3, glm::vec3(0.0f, 1.0f, 0.0f));  // horizontal, to fall about

        const float posture = rng.nextFloat();
        glm::vec3   at      = base;
        glm::quat   facing  = turn;
        bool        adrift  = false;
        if (posture < 0.4f) {
            at.y -= tall * rng.nextFloat(0.0f, 0.35f);  // standing, sunk a way
        } else if (posture < 0.62f) {
            facing = glm::angleAxis(glm::radians(rng.nextFloat(80.0f, 95.0f)), tip) * turn;  // fallen
            at.y  += build.radius * scale * 0.9f;
        } else if (posture < 0.82f) {
            facing = glm::angleAxis(glm::radians(rng.nextFloat(10.0f, 38.0f)), tip) * turn;  // leaning
            at.y  -= tall * rng.nextFloat(0.1f, 0.3f);
        } else {
            adrift = true;  // in the air, any way up
            at.y  += tall * rng.nextFloat(0.4f, 1.3f) + 12.0f;
            facing = glm::angleAxis(rng.nextFloat(0.0f, glm::pi<float>()), glm::normalize(side3 + glm::vec3(0.0f, 0.6f, 0.0f))) * turn;
        }

        const MaterialHandle material = ChessLook::piece(res, set, side);
        const EntityId id = place("Colossus", res.findByName<MeshAsset>(ChessLook::pieceMesh(type)), material, at, facing,
                                  glm::vec3(scale), false);
        if (type == PieceType::Bishop) {
            const EntityId top = spawn("Colossus Top", id);
            scene().add(top, Transform{});
            Mesh ball{res.findByName<MeshAsset>("chess:bishop_top"), material};
            ball.castShadows = false;
            scene().add(top, std::move(ball));
        }
        if (adrift) {
            m_drifters.push_back({id, at, facing, glm::normalize(glm::vec3(rng.nextFloat(-1.0f, 1.0f), 1.0f, rng.nextFloat(-1.0f, 1.0f))),
                                  rng.nextFloat(0.02f, 0.07f) * (rng.nextBool() ? 1.0f : -1.0f), rng.nextFloat(1.5f, 5.0f),
                                  rng.nextFloat(0.0f, glm::two_pi<float>())});
        }
    }
}

// Squares torn out of the plain: some heaved up and tilted where they lay, more adrift in the
// air, tumbling. Each keeps the colour of the square it came from.
void Scenery::spawnShards() {
    ResourceManager& res   = resources();
    const MeshHandle block = res.findByName<MeshAsset>("scenery:block");
    Math::Rng        rng(SEED, 2);
    const auto tile = [&](int i, int j) {
        return (i + j) % 2 == 0 ? ChessLook::tileLight(res) : ChessLook::tileDark(res);
    };

    // Heaved up where they lay, on the plain's own squares.
    for (int n = 0; n < 46; ++n) {
        const float     far   = rng.nextFloat(32.0f, 260.0f);
        const glm::vec3 spot  = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far;
        const int       i     = static_cast<int>(std::round(spot.x / FLOOR_SQUARE));
        const int       j     = static_cast<int>(std::round(spot.z / FLOOR_SQUARE));
        const glm::vec3 tip   = round(rng.nextFloat(0.0f, glm::two_pi<float>()));
        const glm::quat lean  = glm::angleAxis(glm::radians(rng.nextFloat(6.0f, 42.0f)), tip);
        const glm::vec3 at    = {static_cast<float>(i) * FLOOR_SQUARE, -FLOOR_DEPTH + rng.nextFloat(-0.5f, 2.5f), static_cast<float>(j) * FLOOR_SQUARE};
        place("Heaved Square", block, tile(i, j), at, lean, {FLOOR_SQUARE, 1.6f, FLOOR_SQUARE}, false);
    }

    // Adrift: smaller, at every height, turning over slowly.
    for (int n = 0; n < 90; ++n) {
        const float     far    = 26.0f + std::pow(rng.nextFloat(), 0.7f) * 230.0f;
        const float     size   = FLOOR_SQUARE * rng.nextFloat(0.25f, 0.9f);
        const glm::vec3 at     = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far
            + glm::vec3(0.0f, rng.nextFloat(4.0f, 22.0f) + far * rng.nextFloat(0.02f, 0.3f), 0.0f);
        const glm::vec3 axis   = glm::normalize(glm::vec3(rng.nextFloat(-1.0f, 1.0f), rng.nextFloat(-1.0f, 1.0f), rng.nextFloat(-1.0f, 1.0f)) + glm::vec3(0.0f, 0.01f, 0.0f));
        const glm::quat facing = glm::angleAxis(rng.nextFloat(0.0f, glm::two_pi<float>()), axis);
        const EntityId  id     = place("Drifting Square", block, tile(n, 0), at, facing, {size, size * 0.14f, size}, false);
        m_drifters.push_back({id, at, facing, axis, rng.nextFloat(0.05f, 0.22f) * (rng.nextBool() ? 1.0f : -1.0f),
                              rng.nextFloat(0.5f, 3.0f), rng.nextFloat(0.0f, glm::two_pi<float>())});
    }
}

} // namespace Game
