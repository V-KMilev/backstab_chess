#include "scenery.h"

#include <cmath>
#include <iterator>
#include <optional>

#include "core/math/random.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

#include "chess_look.h"
#include "sea.h"
#include "world.h"

namespace Game {

namespace {

using Chess::Color;
using Chess::PieceType;

constexpr uint64_t SEED     = 0xBAC5AB;  ///< The world's layout; every game has the same one.
constexpr int      LANTERNS = 12;
constexpr float    CLEAR    = 16.0f;     ///< Nothing afloat comes nearer the table's middle.
constexpr float    SWIRL    = 0.008f;    ///< Radians a second the flotsam circles the table, all as one.

// The room something takes: a circle on the plane and the heights it spans. Two that do not
// overlap never will, as everything turns round the table together.
struct Footprint {
    glm::vec2 at;
    float     radius;
    float     low;
    float     high;
};

bool clear(const std::vector<Footprint>& taken, const Footprint& f) {
    for (const Footprint& o : taken) {
        const bool level = f.low < o.high && o.low < f.high;
        if (level && glm::length(f.at - o.at) < (f.radius + o.radius) * 1.15f) return false;
    }
    return true;
}

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

// A direction in the horizontal plane, @p angle round from +Z toward +X.
glm::vec3 round(float angle) { return {std::sin(angle), 0.0f, std::cos(angle)}; }

// The turn that tips +Y onto @p normal.
glm::quat tilt(const glm::vec3& normal) {
    const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal);
    const float     s    = glm::length(axis);
    if (s < 1e-6f) return {1.0f, 0.0f, 0.0f, 0.0f};
    return glm::angleAxis(std::atan2(s, normal.y), axis / s);
}

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
    m_swell = res.add(Sea::mesh(m_swellRest), "scenery:swell");

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
        glm::vec3       at    = round * d.at + up * (d.bob * std::sin(wave));
        glm::quat       lean  = {1.0f, 0.0f, 0.0f, 0.0f};
        if (d.afloat) {
            // Lifted and tipped by the swell under it.
            at.y += Sea::height(at.x, at.z, m_time);
            lean = tilt(Sea::normal(at.x, at.z, m_time));
        }
        t->position = at;
        t->rotation = lean * round * glm::angleAxis(d.rock * std::sin(wave * 0.8f + 1.3f), d.rockAxis)
            * glm::angleAxis(d.spin * m_time, d.spinAxis) * d.facing;
    }

    // The near sea rolls on.
    if (MeshAsset* swell = resources().tryEdit(m_swell)) {
        Sea::shape(*swell, m_swellRest, m_time, SEA_TILE);
        resources().commit(m_swell);
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

// Lanterns: small chess pieces of glowing glass, scattered round the table at every height and
// turning slowly as they hang there, each lighting what is near it - the table's edge, the
// heads, the board's rim. They show little by day and carry the night.
void Scenery::spawnLanterns() {
    ResourceManager& res = resources();
    Math::Rng        rng(SEED, 3);
    const PieceType  TYPES[] = {PieceType::Pawn, PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen, PieceType::King};
    for (int i = 0; i < LANTERNS; ++i) {
        const PieceType type  = TYPES[static_cast<size_t>(i) % std::size(TYPES)];
        const float     a     = glm::two_pi<float>() * (static_cast<float>(i) + rng.nextFloat(-0.45f, 0.45f)) / static_cast<float>(LANTERNS);
        const glm::vec3 at    = round(a) * rng.nextFloat(7.5f, 13.0f) + glm::vec3(0.0f, rng.nextFloat(0.8f, 6.5f), 0.0f);
        const glm::quat tilt  = glm::angleAxis(glm::radians(rng.nextFloat(-35.0f, 35.0f)), anyAxis(rng));
        const float     scale = rng.nextFloat(9.0f, 14.0f);
        const EntityId  id    = place("Lantern", res.findByName<MeshAsset>(ChessLook::pieceMesh(type)), ChessLook::lantern(res), at, tilt,
                                      glm::vec3(scale), false);
        if (type == PieceType::Bishop) {
            const EntityId top = spawn("Lantern Top", id);
            scene().add(top, Transform{});
            Mesh ball{res.findByName<MeshAsset>("chess:bishop_top"), ChessLook::lantern(res)};
            ball.castShadows = false;
            scene().add(top, std::move(ball));
        }
        // The light at the piece's heart, half its height up.
        const EntityId heart = spawn("Lantern Light", id);
        scene().add(heart, Transform{{0.0f, buildOf(type).height * 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f / scale)});
        Light light{};
        light.type        = LightType::Point;
        light.color       = {1.0f, 0.72f, 0.4f};
        light.intensity   = 8.0f;
        light.radius      = 9.0f;
        light.castShadows = false;
        scene().add(heart, light);

        Drifter d;
        d.id       = id;
        d.at       = at;
        d.facing   = tilt;
        d.spin     = rng.nextFloat(0.15f, 0.4f) * (rng.nextBool() ? 1.0f : -1.0f);
        d.bob      = rng.nextFloat(0.25f, 0.7f);
        d.rate     = rng.nextFloat(0.4f, 0.8f);
        d.phase    = rng.nextFloat(0.0f, glm::two_pi<float>());
        d.swirl    = 0.02f;
        m_drifters.push_back(d);
    }
}

// The sea, to the horizon, under ripples that slide slowly on.
void Scenery::spawnSea() {
    ResourceManager& res = resources();
    place("Swell", m_swell, ChessLook::sea(res), {0.0f, SEA_LEVEL, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f), false);
}

// The wreck of a thousand games: pieces of every size and set afloat, lying, leaning or bobbing
// upright at their waterline, squares of board among them, all circling the table together;
// and some flung up into the air, turning over as they hang there. Each is set only where it
// clears all the others.
void Scenery::spawnFlotsam() {
    ResourceManager& res   = resources();
    const MeshHandle block = res.findByName<MeshAsset>("scenery:block");
    Math::Rng        rng(SEED, 1);
    const PieceType  TYPES[] = {PieceType::Pawn, PieceType::Pawn, PieceType::Pawn, PieceType::Pawn, PieceType::Knight,
                                PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Rook, PieceType::Queen, PieceType::King};
    const glm::vec3        up(0.0f, 1.0f, 0.0f);
    std::vector<Footprint> taken;
    taken.push_back({{0.0f, 0.0f}, 15.0f, -1000.0f, 1000.0f});  // the table and its lanterns

    // A spot this far out that @p room fits at, if one turns up in a few tries.
    const auto findSpot = [&](float far, Footprint room) -> std::optional<glm::vec2> {
        for (int attempt = 0; attempt < 40; ++attempt) {
            const glm::vec3 p = round(rng.nextFloat(0.0f, glm::two_pi<float>())) * far;
            room.at = {p.x, p.z};
            if (clear(taken, room)) {
                taken.push_back(room);
                return room.at;
            }
        }
        return std::nullopt;
    };
    const auto spawnPiece = [&](PieceType type, const glm::vec3& at, const glm::quat& facing, float scale, Drifter d) {
        const float          roll = rng.nextFloat();
        const PieceSet       set  = roll < 0.4f ? PieceSet::Stone : roll < 0.7f ? PieceSet::Classic : roll < 0.88f ? PieceSet::Metal : PieceSet::Glass;
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
        d.swirl  = SWIRL;
        m_drifters.push_back(d);
    };
    const auto rocking = [&](float low, float high) {
        Drifter d;
        d.rockAxis = round(rng.nextFloat(0.0f, glm::two_pi<float>()));
        d.rock     = glm::radians(rng.nextFloat(low, high));
        d.rate     = rng.nextFloat(0.35f, 0.8f);
        d.phase    = rng.nextFloat(0.0f, glm::two_pi<float>());
        d.afloat   = true;
        return d;
    };

    // Afloat: the nearer, the smaller, so nothing looms over the table.
    for (int i = 0; i < 150; ++i) {
        const PieceType type  = TYPES[rng.nextInt(0, static_cast<int>(std::size(TYPES)) - 1)];
        const Build     build = buildOf(type);
        const float     far   = CLEAR + std::pow(rng.nextFloat(), 1.6f) * 420.0f;
        const float     scale = std::max(far * rng.nextFloat(0.8f, 3.5f), 12.0f);
        const float     yaw   = rng.nextFloat(0.0f, glm::two_pi<float>());
        const float     pose  = rng.nextFloat();
        const float     tall  = build.height * scale;
        const float     wide  = build.radius * scale;

        glm::quat facing = glm::angleAxis(yaw, up);
        float     sink   = 0.0f;
        Footprint room{{}, wide * 1.3f, SEA_LEVEL - 2.0f, SEA_LEVEL + tall};
        if (pose < 0.55f) {
            const float lean = glm::radians(rng.nextFloat(80.0f, 100.0f));  // on its side
            facing = glm::angleAxis(lean, round(yaw)) * facing;
            sink   = wide * 0.2f;
            room   = {{}, tall * 0.55f + wide, SEA_LEVEL - wide, SEA_LEVEL + wide * 1.8f};
        } else if (pose < 0.85f) {
            const float lean = glm::radians(rng.nextFloat(15.0f, 50.0f));  // leaning
            facing = glm::angleAxis(lean, round(yaw)) * facing;
            sink   = tall * rng.nextFloat(0.2f, 0.5f);
            room.radius = wide * 1.3f + tall * std::sin(lean) * 0.6f;
        } else {
            sink = tall * rng.nextFloat(0.1f, 0.4f);                       // upright
        }
        const std::optional<glm::vec2> spot = findSpot(far, room);
        if (!spot) continue;
        spawnPiece(type, {spot->x, SEA_LEVEL - sink, spot->y}, facing, scale, rocking(3.0f, 10.0f));
    }

    // Squares of board afloat among them, white marble and black, one at a time.
    for (int i = 0; i < 80; ++i) {
        const float far  = CLEAR + 2.0f + std::pow(rng.nextFloat(), 1.4f) * 300.0f;
        const float size = std::max(far * rng.nextFloat(0.03f, 0.08f), 1.0f);
        const std::optional<glm::vec2> spot = findSpot(far, {{}, size * 0.75f, SEA_LEVEL - size * 0.3f, SEA_LEVEL + size * 0.3f});
        if (!spot) continue;
        const glm::vec3 at   = {spot->x, SEA_LEVEL + size * 0.03f, spot->y};
        const glm::quat tilt = glm::angleAxis(glm::radians(rng.nextFloat(0.0f, 20.0f)), round(rng.nextFloat(0.0f, glm::two_pi<float>())))
            * glm::angleAxis(rng.nextFloat(0.0f, glm::two_pi<float>()), up);
        Drifter d = rocking(4.0f, 12.0f);
        d.id     = place("Tile Adrift", block, ChessLook::tile(res, i % 2 == 0), at, tilt, {size, size * 0.16f, size}, false);
        d.at     = at;
        d.facing = tilt;
        d.swirl  = SWIRL;
        m_drifters.push_back(d);
    }

    // Flung up into the air: pieces and squares hanging there, turning over, each clear of
    // the sea and of everything else in a sphere round it.
    for (int i = 0; i < 40; ++i) {
        const bool      chunk = i % 3 == 0;
        const PieceType type  = TYPES[rng.nextInt(0, static_cast<int>(std::size(TYPES)) - 1)];
        const float     far   = 25.0f + std::pow(rng.nextFloat(), 0.8f) * 220.0f;
        const float     size  = chunk ? far * rng.nextFloat(0.03f, 0.07f) : far * rng.nextFloat(0.8f, 2.5f);
        const float     reach = chunk ? size * 0.75f : buildOf(type).height * size * 0.6f;
        const float     y     = std::max(rng.nextFloat(6.0f, 18.0f) + far * rng.nextFloat(0.05f, 0.35f), SEA_LEVEL + reach + 3.0f);
        const std::optional<glm::vec2> spot = findSpot(far, {{}, reach, y - reach, y + reach});
        if (!spot) continue;
        const glm::vec3 at     = {spot->x, y, spot->y};
        const glm::quat facing = glm::angleAxis(rng.nextFloat(0.0f, glm::two_pi<float>()), anyAxis(rng));
        Drifter d;
        d.spinAxis = anyAxis(rng);
        d.spin     = rng.nextFloat(0.04f, 0.2f) * (rng.nextBool() ? 1.0f : -1.0f);
        d.bob      = std::min(rng.nextFloat(0.8f, 3.0f), reach * 0.2f);
        d.rate     = rng.nextFloat(0.2f, 0.45f);
        d.phase    = rng.nextFloat(0.0f, glm::two_pi<float>());
        if (chunk) {
            d.id     = place("Flung Tile", block, ChessLook::tile(res, i % 2 == 0), at, facing, {size, size * 0.16f, size}, false);
            d.at     = at;
            d.facing = facing;
            d.swirl  = SWIRL;
            m_drifters.push_back(d);
        } else {
            spawnPiece(type, at, facing, size, d);
        }
    }
}

} // namespace Game
