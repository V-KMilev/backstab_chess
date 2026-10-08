#define VKM_LOG_CATEGORY "CHESS"

#include "chess_game.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "core/math/bounds.h"
#include "core/math/random.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "platform/window/window_manager.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

namespace Game {

namespace {

using Chess::Color;
using Chess::PieceType;
using Chess::Square;

// The set is modelled life size, its board 55 cm across; the world is ten times that.
constexpr float WORLD_SCALE = 10.0f;
constexpr float SQUARE      = 0.058f * WORLD_SCALE;
constexpr float BOARD_TOP   = 0.017f * WORLD_SCALE;
constexpr float BOARD_HALF  = 0.277f * WORLD_SCALE;

// The table, 1.4 by 1 metres and 75 cm high, and the plain round it.
const glm::vec2 TABLE_HALF      = {7.0f, 5.0f};
constexpr float TABLE_THICKNESS = 0.5f;
constexpr float FLOOR_DEPTH     = 7.5f;
constexpr float FLOOR_SIZE      = 1200.0f;

constexpr const char* ACTION_SELECT = "chess/select";
constexpr const char* ACTION_LOOK   = "chess/look";
constexpr const char* ACTION_SEAT   = "chess/seat";

// The moves, as key and direction: forward, back, left, right, up, down.
struct Fly {
    const char* action;
    int         key;
    glm::vec3   along;  ///< In the view's own frame: -Z ahead, +X right, +Y up.
};
const Fly FLY[] = {
    {"chess/forward", GLFW_KEY_W, {0.0f, 0.0f, -1.0f}},
    {"chess/back", GLFW_KEY_S, {0.0f, 0.0f, 1.0f}},
    {"chess/left", GLFW_KEY_A, {-1.0f, 0.0f, 0.0f}},
    {"chess/right", GLFW_KEY_D, {1.0f, 0.0f, 0.0f}},
    {"chess/up", GLFW_KEY_SPACE, {0.0f, 1.0f, 0.0f}},
    {"chess/down", GLFW_KEY_LEFT_SHIFT, {0.0f, -1.0f, 0.0f}},
};

constexpr float LOOK_SPEED     = 0.005f;  ///< Radians per pixel dragged.
constexpr float FLY_SPEED      = 4.5f;    ///< Metres a second.
constexpr float TRAVEL_SECONDS = 1.3f;    ///< From one seat to the next.

// The sun's day: high in the south-east at noon, setting in the west; it lingers high and
// falls fast at the end, so the light warns when time is short.
constexpr float SUN_HIGH        = 32.0f;  ///< Degrees, at noon.
constexpr float SUN_LOW         = -4.0f;  ///< Degrees, at sunset: just under the horizon.
constexpr float SUN_EAST        = 30.0f;  ///< Azimuth at noon, degrees.
constexpr float SUN_SWEEP       = 70.0f;  ///< Degrees it travels round by sunset.
constexpr float SUNRISE_SECONDS = 1.3f;   ///< The sun's return to noon for the next turn.

// The seats: each side's players in a row along its edge of the table, looking at the board.
constexpr float     SEAT_HEIGHT   = 3.8f;
constexpr float     SEAT_DISTANCE = 7.0f;
constexpr float     SEAT_SPACING  = 3.2f;
const glm::vec3     SEAT_TARGET   = {0.0f, 1.5f, 0.0f};
constexpr int       MAX_PER_SIDE  = 4;

const glm::vec3 PALETTE[] = {
    {0.25f, 0.6f, 1.0f},  {0.95f, 0.35f, 0.75f}, {0.35f, 0.85f, 0.4f}, {1.0f, 0.55f, 0.15f},
    {0.65f, 0.4f, 1.0f},  {1.0f, 0.85f, 0.2f},   {0.2f, 0.85f, 0.85f}, {0.95f, 0.25f, 0.25f},
};
// What each player's pieces become, by their place in their side's order.
const PieceSet SKINS[] = {PieceSet::Metal, PieceSet::Glass, PieceSet::Stone};

const char* meshName(PieceType type) {
    switch (type) {
        case PieceType::Pawn:   return "chess:pawn";
        case PieceType::Knight: return "chess:knight";
        case PieceType::Bishop: return "chess:bishop";
        case PieceType::Rook:   return "chess:rook";
        case PieceType::Queen:  return "chess:queen";
        case PieceType::King:   return "chess:king";
        default:                return "";
    }
}

const char* pieceName(PieceType type) {
    switch (type) {
        case PieceType::Pawn:   return "pawn";
        case PieceType::Knight: return "knight";
        case PieceType::Bishop: return "bishop";
        case PieceType::Rook:   return "rook";
        case PieceType::Queen:  return "queen";
        case PieceType::King:   return "king";
        default:                return "piece";
    }
}

const char* sideName(Color side) { return side == Color::White ? "White" : "Black"; }

// A flat ring facing up, 1 across its outer edge's radius, for the mark under an owned piece.
MeshAsset ringMesh(float inner, uint32_t segments) {
    MeshAsset       mesh;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);
    for (uint32_t i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float c = std::cos(u * glm::two_pi<float>());
        const float s = std::sin(u * glm::two_pi<float>());
        mesh.vertices.push_back({{c * inner, 0.0f, s * inner}, up, {u, 0.0f}, tangent});
        mesh.vertices.push_back({{c, 0.0f, s}, up, {u, 1.0f}, tangent});
    }
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t a = i * 2;  // inner, then outer, then the next pair
        mesh.indices.insert(mesh.indices.end(), {a, a + 2, a + 3, a, a + 3, a + 1});
    }
    mesh.boundsMin = {-1.0f, -0.001f, -1.0f};
    mesh.boundsMax = {1.0f, 0.001f, 1.0f};
    return mesh;
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Sets @p text only when it differs, so an unchanged line is not laid out again.
void setText(UIText* label, const std::string& text) {
    if (label && label->text != text) label->text = text;
}

} // namespace

void ChessGame::onStart() {
    InputMap& map = input();
    map.define(ACTION_SELECT, {InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_LEFT, 1.0f}});
    map.define(ACTION_LOOK, {InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, 1.0f}});
    map.define(ACTION_SEAT, {InputBinding{InputSource::Key, GLFW_KEY_F, 1.0f}});
    for (const Fly& fly : FLY) map.define(fly.action, {InputBinding{InputSource::Key, fly.key, 1.0f}});

    resources().add(generateCylinder(0.5f, 1.0f, 40), "chess:disc");
    resources().add(ringMesh(0.72f, 48), "chess:ring");

    // White's players first, then black's; each colour's turns go round them in that order.
    whitePlayers = std::clamp(whitePlayers, 1, MAX_PER_SIDE);
    blackPlayers = std::clamp(blackPlayers, 1, MAX_PER_SIDE);
    std::vector<Chess::Player> players;
    for (int i = 0; i < whitePlayers; ++i) players.push_back({"White " + std::to_string(i + 1), Color::White});
    for (int i = 0; i < blackPlayers; ++i) players.push_back({"Black " + std::to_string(i + 1), Color::Black});
    m_match.emplace(std::move(players));
    m_trophies.assign(m_match->players().size(), 0);

    spawnTable();
    spawnScenery();
    spawnPlayers();
    spawnPieces();
    spawnHud();

    // Sat at the first player's seat from the start, rather than travelling there.
    m_viewer = m_match->current();
    const Seat& first = m_seats[static_cast<size_t>(m_viewer)];
    m_eye   = first.eye;
    m_yaw   = first.yaw;
    m_pitch = first.pitch;
    first.avatar->showHead = false;
    first.avatar->setHeadVisible(false);

    updateCamera(0.0f);
    updateHud(0.0f);
}

void ChessGame::onUpdate(float dt) {
    advanceGlides(dt);
    settleDuel(dt);
    updateCamera(dt);
    updateSun(dt);
    updateAvatars();
    updateHud(dt);

    // A click lands only once the last move has, and the view has reached the player's seat.
    const bool ready = m_glides.empty() && m_travel >= 1.0f && m_duelTime < 0.0f;
    if (ready && input().pressed(ACTION_SELECT) && !input().pointerOverUI()) click(squareUnderPointer());
}

void ChessGame::spawnTable() {
    ResourceManager& res = resources();

    const EntityId board = spawn("Board");
    scene().add(board, Transform{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(WORLD_SCALE)});
    scene().add(board, Mesh{res.findByName<MeshAsset>("chess:board"), ChessLook::board(res)});

    // The table: a top the board and the trophies stand on, on four legs over the plain.
    const MeshHandle cube = res.add(generateCube(), "chess:cube");
    const MeshHandle leg  = res.add(generateCylinder(0.5f, 1.0f, 24), "chess:leg");
    const auto slab = [&](const char* name, MeshHandle mesh, MaterialHandle material, const glm::vec3& at,
                          const glm::vec3& size) {
        const EntityId id = spawn(name);
        scene().add(id, Transform{at, {1.0f, 0.0f, 0.0f, 0.0f}, size});
        scene().add(id, Mesh{mesh, material});
    };
    slab("Table", cube, ChessLook::table(res), {0.0f, -TABLE_THICKNESS * 0.5f, 0.0f},
         {TABLE_HALF.x * 2.0f, TABLE_THICKNESS, TABLE_HALF.y * 2.0f});
    for (const glm::vec2 corner : {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(-1.0f, 1.0f), glm::vec2(1.0f, 1.0f)}) {
        const float height = FLOOR_DEPTH - TABLE_THICKNESS;
        const EntityId id  = spawn("Table Leg");
        scene().add(id, Transform{{corner.x * (TABLE_HALF.x - 0.9f), -TABLE_THICKNESS - height * 0.5f, corner.y * (TABLE_HALF.y - 0.9f)},
                                  {1.0f, 0.0f, 0.0f, 0.0f}, {0.7f, height, 0.7f}});
        scene().add(id, Mesh{leg, ChessLook::table(res)});
    }
    const MeshHandle plain = res.add(generatePlane(1.0f, 1.0f), "chess:plain");
    slab("Floor", plain, ChessLook::floor(res), {0.0f, -FLOOR_DEPTH, 0.0f}, {FLOOR_SIZE, 1.0f, FLOOR_SIZE});
}

// Colossal pieces standing in the plain, some sunk and some fallen, against the sky all round.
void ChessGame::spawnScenery() {
    struct Colossus {
        float     azimuth;   ///< Degrees round from +Z toward +X.
        float     distance;  ///< From the table, in metres.
        PieceType type;
        float     scale;     ///< Times life size.
        float     tilt;      ///< Degrees fallen over.
        float     sink;      ///< Metres below the plain its base stands.
        PieceSet  set;
        Color     side;
    };
    const Colossus COLOSSI[] = {
        {18.0f, 260.0f, PieceType::King, 1500.0f, 0.0f, 10.0f, PieceSet::Stone, Color::White},
        {62.0f, 380.0f, PieceType::Queen, 2300.0f, 4.0f, 30.0f, PieceSet::Stone, Color::Black},
        {104.0f, 170.0f, PieceType::Rook, 750.0f, 0.0f, 6.0f, PieceSet::Stone, Color::Black},
        {148.0f, 290.0f, PieceType::Knight, 1500.0f, 0.0f, 15.0f, PieceSet::Stone, Color::White},
        {196.0f, 210.0f, PieceType::Pawn, 900.0f, 84.0f, 0.0f, PieceSet::Classic, Color::Black},
        {232.0f, 440.0f, PieceType::Bishop, 2500.0f, 0.0f, 50.0f, PieceSet::Stone, Color::White},
        {287.0f, 200.0f, PieceType::Rook, 1000.0f, 11.0f, 14.0f, PieceSet::Stone, Color::White},
        {326.0f, 350.0f, PieceType::King, 2000.0f, 0.0f, 40.0f, PieceSet::Stone, Color::Black},
        {352.0f, 170.0f, PieceType::Pawn, 650.0f, 0.0f, 5.0f, PieceSet::Classic, Color::White},
    };
    ResourceManager& res = resources();
    for (const Colossus& c : COLOSSI) {
        const float     a  = glm::radians(c.azimuth);
        const glm::vec3 at = {std::sin(a) * c.distance, -FLOOR_DEPTH - c.sink, std::cos(a) * c.distance};
        // A fallen one lies on its side, its radius up off the plain.
        const float     lying = c.tilt > 45.0f ? 0.015f * c.scale : 0.0f;
        const glm::quat turn  = glm::angleAxis(a, glm::vec3(0.0f, 1.0f, 0.0f))
            * glm::angleAxis(glm::radians(c.tilt), glm::vec3(0.0f, 0.0f, 1.0f));
        const EntityId id = spawn("Colossus");
        scene().add(id, Transform{at + glm::vec3(0.0f, lying, 0.0f), turn, glm::vec3(c.scale)});
        // No shadows: a low sun would lay a colossus's across the whole table.
        const MaterialHandle stone = ChessLook::piece(res, c.set, c.side);
        Mesh body{res.findByName<MeshAsset>(meshName(c.type)), stone};
        body.castShadows = false;
        scene().add(id, std::move(body));
        if (c.type == PieceType::Bishop) {
            const EntityId top = spawn("Colossus Top", id);
            scene().add(top, Transform{});
            Mesh ball{res.findByName<MeshAsset>("chess:bishop_top"), stone};
            ball.castShadows = false;
            scene().add(top, std::move(ball));
        }
    }
}

void ChessGame::spawnPlayers() {
    ResourceManager& res = resources();
    int ranks[2] = {0, 0};
    const auto& players = m_match->players();
    for (size_t p = 0; p < players.size(); ++p) {
        const Color side  = players[p].side;
        const int   count = side == Color::White ? whitePlayers : blackPlayers;
        const int   rank  = ranks[side == Color::White ? 0 : 1]++;

        // White's first player sits on white's left, which is +X; black's is at -X.
        const float along = (static_cast<float>(rank) - 0.5f * static_cast<float>(count - 1)) * SEAT_SPACING;
        Seat seat;
        seat.eye   = side == Color::White ? glm::vec3(-along, SEAT_HEIGHT, -SEAT_DISTANCE)
                                          : glm::vec3(along, SEAT_HEIGHT, SEAT_DISTANCE);
        Math::toYawPitch(SEAT_TARGET - seat.eye, seat.yaw, seat.pitch);
        seat.color = PALETTE[p % std::size(PALETTE)];
        seat.skin  = SKINS[static_cast<size_t>(rank) % std::size(SKINS)];

        MaterialAsset ring;
        ring.type             = MaterialType::Transparent;
        ring.albedo           = glm::vec4(seat.color, 0.85f);
        ring.emission         = seat.color;
        ring.emissiveStrength = 2.0f;
        ring.roughness        = 0.4f;
        ring.doubleSided      = true;
        seat.ring = res.add(std::move(ring), "chess:ring:" + std::to_string(p));

        const EntityId head = spawn(players[p].name.c_str());
        scene().add(head, Transform{seat.eye, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f)});
        Avatar& avatar   = addBehavior<Avatar>(scene(), head);
        avatar.color     = seat.color;
        avatar.teamWhite = side == Color::White;
        seat.avatar      = &avatar;
        m_seats.push_back(seat);
    }
}

void ChessGame::spawnPieces() {
    for (Square s = 0; s < 64; ++s) {
        const Chess::Piece piece = m_match->position().at(s);
        if (!piece.empty()) m_pieces[static_cast<size_t>(s)] = spawnPiece(piece, s);
    }
    refreshLooks();
}

EntityId ChessGame::spawnPiece(Chess::Piece piece, Square square) {
    // Black's pieces face the other way; it shows on a knight.
    const glm::quat facing = piece.color == Color::White
        ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
        : glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    DrawnPiece drawn;
    drawn.side = piece.color;
    drawn.body = spawn(meshName(piece.type));
    scene().add(drawn.body, Transform{squareCentre(square), facing, glm::vec3(WORLD_SCALE)});
    scene().add(drawn.body, Mesh{});

    // The owner's ring, glowing round the base, just clear of the board's lacquer; the piece's
    // scale is the world's, so its sizes are a tenth.
    drawn.ring = spawn("Ring", drawn.body);
    scene().add(drawn.ring, Transform{{0.0f, 0.0006f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.026f, 1.0f, 0.026f}});
    Mesh ring{resources().findByName<MeshAsset>("chess:ring"), {}};
    ring.castShadows = false;
    ring.visible     = false;
    scene().add(drawn.ring, std::move(ring));

    m_drawn.push_back(drawn);
    setMesh(m_drawn.back(), piece.type);
    return drawn.body;
}

void ChessGame::setMesh(DrawnPiece& drawn, PieceType type) {
    ResourceManager& res = resources();
    Mesh* mesh = scene().tryGet<Mesh>(drawn.body);
    if (!mesh) return;
    mesh->mesh = res.findByName<MeshAsset>(meshName(type));

    // A bishop's ball is a mesh of its own, carried as a child.
    if (type == PieceType::Bishop && !drawn.top) {
        drawn.top = spawn("chess:bishop_top", drawn.body);
        scene().add(drawn.top, Transform{});
        scene().add(drawn.top, Mesh{res.findByName<MeshAsset>("chess:bishop_top"), mesh->material});
    }
}

ChessGame::DrawnPiece* ChessGame::drawnOf(EntityId body) {
    const auto it = std::find_if(m_drawn.begin(), m_drawn.end(), [&](const DrawnPiece& d) { return d.body == body; });
    return it != m_drawn.end() ? &*it : nullptr;
}

// White sits at -Z looking along +Z, so its right hand, the h-file, is at -X.
glm::vec3 ChessGame::squareCentre(Square square) const {
    return {
        (3.5f - static_cast<float>(Chess::fileOf(square))) * SQUARE,
        BOARD_TOP,
        (static_cast<float>(Chess::rankOf(square)) - 3.5f) * SQUARE,
    };
}

Square ChessGame::squareUnderPointer() {
    const WindowManager* screen = window();
    const EntityId       eye    = findActiveCamera(scene());
    const Transform*     view   = scene().tryGet<Transform>(eye);
    const Camera*        lens   = scene().tryGet<Camera>(eye);
    if (!screen || !view || !lens || screen->getWidth() == 0 || screen->getHeight() == 0) return Chess::NO_SQUARE;

    // The cursor through the camera onto the board's top.
    const float     width  = static_cast<float>(screen->getWidth());
    const float     height = static_cast<float>(screen->getHeight());
    const glm::vec2 pixel  = input().pointer() * screen->framebufferScale();
    const glm::vec2 ndc    = {pixel.x / width * 2.0f - 1.0f, 1.0f - pixel.y / height * 2.0f};
    const glm::mat4 viewProj = Camera::computeProjection(*lens, width / height) * Transform::computeView(*view);
    const Math::Ray ray      = Math::rayThroughNdc(glm::inverse(viewProj), ndc);
    if (std::abs(ray.direction.y) < 1e-4f) return Chess::NO_SQUARE;

    const float t = (BOARD_TOP - ray.origin.y) / ray.direction.y;
    if (t <= 0.0f) return Chess::NO_SQUARE;
    const glm::vec3 hit  = ray.origin + ray.direction * t;
    const int       file = static_cast<int>(std::floor(4.0f - hit.x / SQUARE));
    const int       rank = static_cast<int>(std::floor(hit.z / SQUARE + 4.0f));
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return Chess::NO_SQUARE;
    return Chess::squareAt(file, rank);
}

void ChessGame::mark(Square square, float size, MaterialHandle material) {
    const EntityId hint = spawn("Hint");
    scene().add(hint, Transform{squareCentre(square) + glm::vec3(0.0f, 0.004f, 0.0f), {1.0f, 0.0f, 0.0f, 0.0f}, {size, 0.006f, size}});
    Mesh drawn{resources().findByName<MeshAsset>("chess:disc"), material};
    drawn.castShadows = false;
    scene().add(hint, std::move(drawn));
    m_hints.push_back(hint);
}

void ChessGame::click(Square square) {
    if (m_match->over() || m_match->duel()) return;
    const Chess::Position& position = m_match->position();

    if (m_selected != Chess::NO_SQUARE && square != Chess::NO_SQUARE) {
        // A pawn reaching the last rank becomes a queen until there is a way to ask.
        const auto choice = std::find_if(m_choices.begin(), m_choices.end(), [&](const Chess::Move& m) {
            return m.to == square && (m.promotion == PieceType::None || m.promotion == PieceType::Queen);
        });
        if (choice != m_choices.end()) {
            const Chess::Move move = *choice;
            clearChoices();
            tryMove(move);
            return;
        }
    }

    const bool own = square != Chess::NO_SQUARE && !position.at(square).empty()
        && position.at(square).color == position.sideToMove();
    const bool again = square == m_selected;
    clearChoices();
    if (own && !again) showChoices(square);
}

// Blue squares are plain moves; red ones are duels, for a teammate's piece or an enemy's.
void ChessGame::showChoices(Square square) {
    const Chess::Position& position = m_match->position();
    m_selected = square;
    for (const Chess::Move& move : position.legalMoves()) {
        if (move.from == square) m_choices.push_back(move);
    }

    ResourceManager& res = resources();
    const bool teammates = m_match->ownerOf(square) >= 0 && m_match->ownerOf(square) != m_match->current();
    mark(square, SQUARE * 0.9f, teammates ? ChessLook::duel(res) : ChessLook::chosen(res));
    for (const Chess::Move& move : m_choices) {
        if (move.promotion != PieceType::None && move.promotion != PieceType::Queen) continue;
        const bool takes = position.takenBy(move) != Chess::NO_SQUARE;
        const bool duel  = m_match->duelFor(move).has_value();
        mark(move.to, takes ? SQUARE * 0.85f : SQUARE * 0.32f, duel ? ChessLook::duel(res) : ChessLook::hint(res));
    }
}

void ChessGame::clearChoices() {
    for (const EntityId hint : m_hints) destroy(hint);
    m_hints.clear();
    m_choices.clear();
    m_selected = Chess::NO_SQUARE;
}

void ChessGame::tryMove(const Chess::Move& move) {
    const Chess::Position before = m_match->position();
    const int             player = m_match->current();
    switch (m_match->attempt(move)) {
        case Chess::Attempt::Played:
            LOG_INFO("%s plays %s", nameOf(player), Chess::Position::toUci(move).c_str());
            showMove(move, before);
            break;
        case Chess::Attempt::Duel:
            startDuel("DUEL!");
            break;
        case Chess::Attempt::Illegal:
            break;
    }
}

void ChessGame::startDuel(const std::string& title) {
    const Chess::Duel&     duel     = *m_match->duel();
    const Chess::Position& position = m_match->position();
    m_duelTime = 0.0f;

    clearChoices();
    mark(duel.move.from, SQUARE * 0.9f, ChessLook::chosen(resources()));
    mark(duel.move.to, SQUARE * 0.85f, ChessLook::duel(resources()));

    std::string detail = nameOf(duel.challenger);
    if (duel.kind == Chess::DuelKind::Teammate) {
        detail += std::string(" wants ") + nameOf(duel.defender) + "'s " + pieceName(position.at(duel.move.from).type);
    } else {
        detail += std::string(" goes for ") + nameOf(duel.defender) + "'s "
            + pieceName(position.at(position.takenBy(duel.move)).type);
    }
    showBanner(title, detail, duelSeconds);
    LOG_INFO("%s", detail.c_str());
}

// The coin spins for duelSeconds; the winner's move is played, or the loser's turn handed on.
void ChessGame::settleDuel(float dt) {
    if (m_duelTime < 0.0f || !m_match->duel()) return;
    m_duelTime += dt;
    if (m_duelTime < duelSeconds) return;

    const Chess::Duel     duel   = *m_match->duel();
    const Chess::Position before = m_match->position();
    const bool            won    = Math::Random::boolean();
    m_duelTime = -1.0f;
    clearChoices();
    m_match->resolveDuel(won);

    const std::string winner = nameOf(won ? duel.challenger : duel.defender);
    LOG_INFO("%s wins the duel", winner.c_str());
    if (won && m_match->duel()) {
        startDuel(winner + " wins! Another duel");
    } else if (won) {
        LOG_INFO("%s plays %s", nameOf(duel.challenger), Chess::Position::toUci(duel.move).c_str());
        showBanner(winner + " wins!", "and plays the move", 1.8f);
        showMove(duel.move, before);
    } else if (duel.kind == Chess::DuelKind::Teammate) {
        showBanner(winner + " holds on!", winner + " plays this turn instead", 2.2f);
        newDay();
    } else {
        showBanner(winner + " holds on!", "Nothing is taken, and " + winner + " moves next", 2.2f);
        newDay();
    }
}

// The turn's clock is the sun. A duel has a short day of its own, and a turn's day stands
// still while pieces move or the view travels.
void ChessGame::updateSun(float dt) {
    const bool waiting = m_match->over() || !m_glides.empty() || m_travel < 1.0f;
    if (m_duelTime >= 0.0f) {
        m_day = duelSeconds > 0.0f ? std::min(m_duelTime / duelSeconds, 1.0f) : 1.0f;
    } else if (m_sunrise < 1.0f) {
        m_sunrise = std::min(m_sunrise + dt / SUNRISE_SECONDS, 1.0f);
        m_day     = glm::mix(m_dayFrom, 0.0f, smooth(m_sunrise));
    } else if (!waiting && turnSeconds > 0.0f) {
        m_day += dt / turnSeconds;
        if (m_day >= 1.0f) {
            m_day = 1.0f;
            timeOut();
        }
    }
    SkySettings& sky = scene().environment().sky;
    sky.sunElevation = SUN_LOW + (SUN_HIGH - SUN_LOW) * std::cos(m_day * glm::half_pi<float>());
    sky.sunAzimuth   = SUN_EAST + SUN_SWEEP * m_day;
}

void ChessGame::newDay() {
    m_dayFrom = m_day;
    m_sunrise = 0.0f;
}

// The sun has set on a player who has not moved: a move is made for them, one that needs no
// duel where there is one.
void ChessGame::timeOut() {
    const std::vector<Chess::Move> moves = m_match->position().legalMoves();
    std::vector<Chess::Move>       free;
    for (const Chess::Move& move : moves) {
        if (!m_match->duelFor(move)) free.push_back(move);
    }
    const std::vector<Chess::Move>& pool = free.empty() ? moves : free;
    if (pool.empty()) return;

    clearChoices();
    const std::string who = nameOf(m_match->current());
    LOG_INFO("The sun sets on %s", who.c_str());
    showBanner("Sunset!", who + " ran out of daylight", 2.2f);
    tryMove(pool[static_cast<size_t>(Math::Random::range(0, static_cast<int>(pool.size()) - 1))]);
}

void ChessGame::showMove(const Chess::Move& move, const Chess::Position& before) {
    const auto   idx   = [](Square s) { return static_cast<size_t>(s); };
    const Square taken = before.takenBy(move);

    const PieceType mover = before.at(move.from).type;
    Glide glide;
    glide.piece   = m_pieces[idx(move.from)];
    glide.from    = squareCentre(move.from);
    glide.to      = squareCentre(move.to);
    glide.lift    = mover == PieceType::Knight ? 0.9f : 0.12f;
    glide.becomes = move.promotion;
    if (taken != Chess::NO_SQUARE) {
        glide.victim = m_pieces[idx(taken)];
        glide.taker  = m_match->ownerOf(move.to);  // the match has played it: the mover owns it
        m_pieces[idx(taken)] = {};
    }
    m_glides.push_back(glide);
    m_pieces[idx(move.to)]   = m_pieces[idx(move.from)];
    m_pieces[idx(move.from)] = {};

    // Castling carries the rook round the king.
    if (move.kind == Chess::MoveKind::CastleKing || move.kind == Chess::MoveKind::CastleQueen) {
        const bool   kingSide = move.kind == Chess::MoveKind::CastleKing;
        const Square rookFrom = kingSide ? move.to + 1 : move.to - 2;
        const Square rookTo   = kingSide ? move.to - 1 : move.to + 1;
        Glide rook;
        rook.piece = m_pieces[idx(rookFrom)];
        rook.from  = squareCentre(rookFrom);
        rook.to    = squareCentre(rookTo);
        rook.lift  = 0.5f;
        m_glides.push_back(rook);
        m_pieces[idx(rookTo)]   = m_pieces[idx(rookFrom)];
        m_pieces[idx(rookFrom)] = {};
    }
    refreshLooks();
    newDay();
}

void ChessGame::advanceGlides(float dt) {
    std::vector<Glide> started;
    bool               promoted = false;
    for (Glide& glide : m_glides) {
        const float seconds = glide.seconds > 0.0f ? glide.seconds : moveSeconds;
        glide.t = seconds > 0.0f ? std::min(glide.t + dt / seconds, 1.0f) : 1.0f;
        // Eased along the way, and a half sine of lift over it.
        const float e = smooth(glide.t);
        if (Transform* t = scene().tryGet<Transform>(glide.piece)) {
            t->position = glm::mix(glide.from, glide.to, e)
                + glm::vec3(0.0f, glide.lift * std::sin(glide.t * glm::pi<float>()), 0.0f);
            if (glide.spin != 0.0f) t->rotation = glm::angleAxis(glide.spin * e, glm::vec3(0.0f, 1.0f, 0.0f)) * glide.facing;
        }

        if (glide.t >= 1.0f) {
            if (glide.victim) started.push_back(takeAway(glide.victim, glide.taker));
            DrawnPiece* drawn = drawnOf(glide.piece);
            if (drawn && glide.becomes != PieceType::None) {
                setMesh(*drawn, glide.becomes);
                promoted = true;
            }
        }
    }
    m_glides.erase(
        std::remove_if(m_glides.begin(), m_glides.end(), [](const Glide& g) { return g.t >= 1.0f; }),
        m_glides.end()
    );
    m_glides.insert(m_glides.end(), started.begin(), started.end());
    if (promoted) refreshLooks();
}

// A taken piece rises off the board, turning once, and floats to the table's edge in front of
// whoever took it, to stand in a row of their trophies.
ChessGame::Glide ChessGame::takeAway(EntityId piece, int taker) {
    constexpr int   PER_ROW = 6;
    constexpr float GAP     = 0.42f;
    Glide glide;
    glide.piece   = piece;
    glide.seconds = 1.1f;
    glide.lift    = 1.4f;
    glide.spin    = glm::two_pi<float>();
    if (const Transform* t = scene().tryGet<Transform>(piece)) {
        glide.from   = t->position;
        glide.facing = t->rotation;
    }
    glide.to = glide.from;
    if (taker < 0 || static_cast<size_t>(taker) >= m_seats.size()) return glide;

    const int   count = m_trophies[static_cast<size_t>(taker)]++;
    const int   col   = count % PER_ROW;
    const int   row   = count / PER_ROW;
    const float near  = m_seats[static_cast<size_t>(taker)].eye.z < 0.0f ? -1.0f : 1.0f;
    const float x     = m_seats[static_cast<size_t>(taker)].eye.x
        + (static_cast<float>(col) - 0.5f * static_cast<float>(PER_ROW - 1)) * GAP;
    glide.to = {
        std::clamp(x, -TABLE_HALF.x + 0.4f, TABLE_HALF.x - 0.4f),
        0.0f,
        near * (BOARD_HALF + 0.6f + GAP * static_cast<float>(row)),
    };
    return glide;
}

// A piece nobody owns is the set's own wood; an owned one wears its owner's skin and ring.
void ChessGame::refreshLooks() {
    ResourceManager& res = resources();
    for (const DrawnPiece& drawn : m_drawn) {
        if (Mesh* ring = scene().tryGet<Mesh>(drawn.ring)) ring->visible = false;
    }
    for (Square s = 0; s < 64; ++s) {
        const DrawnPiece* drawn = drawnOf(m_pieces[static_cast<size_t>(s)]);
        if (!m_pieces[static_cast<size_t>(s)] || !drawn) continue;
        const int            owner = m_match->ownerOf(s);
        const Seat*          seat  = owner >= 0 ? &m_seats[static_cast<size_t>(owner)] : nullptr;
        const MaterialHandle skin  = ChessLook::piece(res, seat ? seat->skin : PieceSet::Classic, drawn->side);
        if (Mesh* body = scene().tryGet<Mesh>(drawn->body)) body->material = skin;
        if (Mesh* top = scene().tryGet<Mesh>(drawn->top)) top->material = skin;
        if (Mesh* ring = scene().tryGet<Mesh>(drawn->ring); ring && seat) {
            ring->material = seat->ring;
            ring->visible  = true;
        }
    }
}

void ChessGame::updateCamera(float dt) {
    // The view goes to whoever moves next once the last move has landed and no duel is waiting.
    const int next = m_match->over() ? m_viewer : m_match->current();
    if (next >= 0 && next != m_viewer && m_glides.empty() && m_duelTime < 0.0f) {
        Avatar* leaving = m_seats[static_cast<size_t>(m_viewer)].avatar;
        Avatar* coming  = m_seats[static_cast<size_t>(next)].avatar;
        leaving->showHead = true;
        leaving->setHeadVisible(true);
        coming->showHead = false;
        coming->setHeadVisible(false);
        m_viewer    = next;
        m_fromEye   = m_eye;
        m_fromYaw   = m_yaw;
        m_fromPitch = m_pitch;
        m_travel    = 0.0f;
        clearChoices();
    }
    const Seat& seat = m_seats[static_cast<size_t>(m_viewer)];

    if (m_travel < 1.0f) {
        // Round the table to the next seat, rising a little, with the board held in view
        // between the two seats' own angles.
        m_travel = std::min(m_travel + dt / TRAVEL_SECONDS, 1.0f);
        const float e      = smooth(m_travel);
        const float arc    = std::sin(m_travel * glm::pi<float>());
        const float from   = std::atan2(m_fromEye.x, m_fromEye.z);
        const float turn   = std::remainder(std::atan2(seat.eye.x, seat.eye.z) - from, glm::two_pi<float>());
        const float radius = glm::mix(glm::length(glm::vec2(m_fromEye.x, m_fromEye.z)), glm::length(glm::vec2(seat.eye.x, seat.eye.z)), e);
        const float around = from + turn * e;
        m_eye = {std::sin(around) * radius, glm::mix(m_fromEye.y, seat.eye.y, e) + 1.2f * arc, std::cos(around) * radius};

        const glm::quat ends = glm::slerp(Math::fromYawPitch(m_fromYaw, m_fromPitch), Math::fromYawPitch(seat.yaw, seat.pitch), e);
        const glm::quat view = glm::slerp(ends, Math::lookRotation(SEAT_TARGET - m_eye), arc);
        Math::toYawPitch(Math::computeForward(view), m_yaw, m_pitch);
        if (m_travel >= 1.0f) {
            m_eye   = seat.eye;
            m_yaw   = seat.yaw;
            m_pitch = seat.pitch;
        }
    } else {
        if (input().held(ACTION_LOOK)) {
            const glm::vec2 drag = input().pointerDelta();
            m_yaw   -= drag.x * LOOK_SPEED;
            m_pitch  = std::clamp(m_pitch - drag.y * LOOK_SPEED, glm::radians(-85.0f), glm::radians(60.0f));
        }
        if (input().pressed(ACTION_SEAT)) {
            m_eye   = seat.eye;
            m_yaw   = seat.yaw;
            m_pitch = seat.pitch;
        }

        // Flight is level whatever the view's pitch, so W never dives into the table.
        const glm::quat heading = glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 move(0.0f);
        for (const Fly& fly : FLY) {
            if (!input().held(fly.action)) continue;
            move += fly.along.y != 0.0f ? fly.along : heading * fly.along;
        }
        if (glm::dot(move, move) > 0.0f) m_eye += glm::normalize(move) * FLY_SPEED * dt;
        m_eye += Math::computeForward(Math::fromYawPitch(m_yaw, m_pitch)) * (input().wheel() * 0.6f);
    }

    // Kept over the room: above the table, and within reach of the board.
    m_eye.y = std::clamp(m_eye.y, 0.8f, 10.0f);
    const glm::vec2 flat(m_eye.x, m_eye.z);
    const float     reach = glm::length(flat);
    if (reach > 16.0f) {
        m_eye.x *= 16.0f / reach;
        m_eye.z *= 16.0f / reach;
    }

    Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()));
    if (!view) return;
    view->rotation = Math::fromYawPitch(m_yaw, m_pitch);
    view->position = m_eye;
}

// The viewer's head rides the camera and points where the cursor does; everyone else sits
// at their seat watching the board, and two duelling players stare each other down.
void ChessGame::updateAvatars() {
    const std::optional<Chess::Duel>& duel = m_match->duel();
    for (size_t i = 0; i < m_seats.size(); ++i) {
        const Seat& seat   = m_seats[i];
        const int   player = static_cast<int>(i);
        if (player == m_viewer) {
            seat.avatar->setPose(m_eye, Math::fromYawPitch(m_yaw, m_pitch));
            const Square at = m_travel >= 1.0f ? squareUnderPointer() : Chess::NO_SQUARE;
            seat.avatar->setPointer(at != Chess::NO_SQUARE, at != Chess::NO_SQUARE ? squareCentre(at) : glm::vec3(0.0f));
            continue;
        }
        glm::vec3 look = SEAT_TARGET;
        if (duel && (duel->challenger == player || duel->defender == player)) {
            const int rival = duel->challenger == player ? duel->defender : duel->challenger;
            look = rival == m_viewer ? m_eye : m_seats[static_cast<size_t>(rival)].eye;
        }
        seat.avatar->setPose(seat.eye, Math::lookRotation(look - seat.eye));
        seat.avatar->setPointer(false, glm::vec3(0.0f));
    }
}

void ChessGame::spawnHud() {
    const EntityId canvas = spawn("HUD");
    UICanvas layer;
    layer.sortOrder = 10;
    scene().add(canvas, std::move(layer));

    const auto panel = [&](const char* name, EntityId parent, UIElement place, const glm::vec4& color, float corner) {
        const EntityId id = spawn(name, parent);
        scene().add(id, std::move(place));
        UIImage fill;
        fill.color              = color;
        fill.shape.cornerRadius = corner;
        scene().add(id, std::move(fill));
        return id;
    };
    const auto label = [&](const char* name, EntityId parent, UIElement place, float size, UIText::Align align) {
        const EntityId id = spawn(name, parent);
        scene().add(id, std::move(place));
        UIText text;
        text.pixelSize = size;
        text.align     = align;
        text.valign    = UIText::VAlign::Middle;
        text.color     = {0.95f, 0.93f, 0.88f, 1.0f};
        scene().add(id, std::move(text));
        return id;
    };
    const glm::vec4 SHADE = {0.04f, 0.04f, 0.06f, 0.62f};

    // The status line, top centre.
    const EntityId status = panel("Status", canvas, UIElement::at({0.5f, 0.0f}, {0.0f, 24.0f}, {760.0f, 60.0f}), SHADE, 30.0f);
    m_status = label("Status Text", status, UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {720.0f, 60.0f}), 30.0f, UIText::Align::Center);

    // The scoreboard, top left: a swatch of each player's colour, their name and their points.
    const size_t   count = m_seats.size();
    const EntityId board = panel("Scores", canvas,
        UIElement::at({0.0f, 0.0f}, {24.0f, 24.0f}, {330.0f, 64.0f + 44.0f * static_cast<float>(count)}), SHADE, 18.0f);
    const EntityId title = label("Scores Title", board, UIElement::at({0.0f, 0.0f}, {22.0f, 12.0f}, {290.0f, 36.0f}), 24.0f, UIText::Align::Left);
    if (UIText* text = scene().tryGet<UIText>(title)) {
        text->text  = "SCORES";
        text->color = {0.95f, 0.93f, 0.88f, 0.55f};
    }
    for (size_t i = 0; i < count; ++i) {
        const float    y   = 54.0f + 44.0f * static_cast<float>(i);
        const EntityId row = spawn("Score Row", board);
        scene().add(row, UIElement::at({0.0f, 0.0f}, {14.0f, y}, {302.0f, 40.0f}));
        panel("Swatch", row, UIElement::at({0.0f, 0.5f}, {8.0f, 0.0f}, {16.0f, 16.0f}), glm::vec4(m_seats[i].color, 1.0f), 8.0f);
        ScoreRow line;
        line.name   = label("Name", row, UIElement::at({0.0f, 0.5f}, {36.0f, 0.0f}, {200.0f, 40.0f}), 28.0f, UIText::Align::Left);
        line.points = label("Points", row, UIElement::at({1.0f, 0.5f}, {-8.0f, 0.0f}, {80.0f, 40.0f}), 28.0f, UIText::Align::Right);
        m_scoreRows.push_back(line);
    }

    // The banner a duel and its outcome take over the bottom of the screen with, clear of the
    // heads across the table.
    m_banner       = panel("Banner", canvas, UIElement::at({0.5f, 1.0f}, {0.0f, -48.0f}, {900.0f, 170.0f}), {0.05f, 0.02f, 0.03f, 0.78f}, 24.0f);
    m_bannerTitle  = label("Banner Title", m_banner, UIElement::at({0.5f, 0.0f}, {0.0f, 18.0f}, {860.0f, 86.0f}), 72.0f, UIText::Align::Center);
    m_bannerDetail = label("Banner Detail", m_banner, UIElement::at({0.5f, 1.0f}, {0.0f, -20.0f}, {860.0f, 44.0f}), 30.0f, UIText::Align::Center);
    if (UIText* text = scene().tryGet<UIText>(m_bannerTitle)) text->color = {1.0f, 0.78f, 0.35f, 1.0f};
    if (UIElement* place = scene().tryGet<UIElement>(m_banner)) place->visible = false;
}

void ChessGame::showBanner(const std::string& title, const std::string& detail, float seconds) {
    setText(scene().tryGet<UIText>(m_bannerTitle), title);
    setText(scene().tryGet<UIText>(m_bannerDetail), detail);
    m_bannerTime = seconds;
}

void ChessGame::updateHud(float dt) {
    m_bannerTime = std::max(m_bannerTime - dt, 0.0f);
    if (UIElement* place = scene().tryGet<UIElement>(m_banner)) place->visible = m_bannerTime > 0.0f;

    const Chess::Position&            position = m_match->position();
    const std::vector<Chess::Player>& players  = m_match->players();
    const int current = m_match->current();
    int best = 0;
    for (const Chess::Player& player : players) best = std::max(best, player.score);

    for (size_t i = 0; i < m_scoreRows.size(); ++i) {
        const bool now  = static_cast<int>(i) == current && !m_match->over();
        const bool lead = m_match->over() && players[i].score == best;
        const glm::vec4 color = now || lead ? glm::vec4(1.0f, 0.97f, 0.9f, 1.0f) : glm::vec4(0.8f, 0.78f, 0.74f, 0.7f);
        UIText* name   = scene().tryGet<UIText>(m_scoreRows[i].name);
        UIText* points = scene().tryGet<UIText>(m_scoreRows[i].points);
        setText(name, players[i].name + (now ? "  <" : ""));
        setText(points, std::to_string(players[i].score));
        if (name) name->color = color;
        if (points) points->color = color;
    }

    std::string line;
    switch (position.outcome()) {
        case Chess::Outcome::Checkmate:            line = "Checkmate";                     break;
        case Chess::Outcome::Stalemate:            line = "Stalemate";                     break;
        case Chess::Outcome::FiftyMoves:           line = "Fifty moves without a capture"; break;
        case Chess::Outcome::Repetition:           line = "The same position three times"; break;
        case Chess::Outcome::InsufficientMaterial: line = "Neither side can mate";         break;
        case Chess::Outcome::Ongoing:                                                      break;
    }
    if (m_match->over()) {
        // The game is won on points, whoever mated.
        std::vector<std::string> leaders;
        for (const Chess::Player& player : players) {
            if (player.score == best) leaders.push_back(player.name);
        }
        line += leaders.size() == 1 ? ". " + leaders[0] + " wins with " + std::to_string(best)
                                    : ". A tie at " + std::to_string(best);
    } else if (const auto& duel = m_match->duel()) {
        line = std::string("Duel: ") + nameOf(duel->challenger) + " against " + nameOf(duel->defender);
    } else if (current >= 0) {
        line = std::string(nameOf(current)) + " to move, for " + sideName(position.sideToMove());
        if (position.inCheck(position.sideToMove())) line += ". Check!";
        const int left = static_cast<int>(std::ceil((1.0f - m_day) * turnSeconds));
        line += "   Sunset in " + std::to_string(left) + "s";
    }
    setText(scene().tryGet<UIText>(m_status), line);
}

const char* ChessGame::nameOf(int player) const {
    const auto& players = m_match->players();
    return player >= 0 && static_cast<size_t>(player) < players.size() ? players[static_cast<size_t>(player)].name.c_str() : "Nobody";
}

} // namespace Game
