#define VKM_LOG_CATEGORY "CHESS"

#include "chess_game.h"

#include "dial.h"
#include "shapes.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "core/math/bounds.h"
#include "core/math/random.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "platform/window/window_manager.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/generate/mesh_generators.h"

namespace Game {

namespace {

using Chess::Color;
using Chess::PieceType;
using Chess::Square;

// The set is modelled life size, its board 55 cm across; the world is ten times that.
constexpr float SQUARE      = 0.058f * WORLD_SCALE;
constexpr float BOARD_TOP   = 0.017f * WORLD_SCALE;
constexpr float BOARD_HALF  = 0.277f * WORLD_SCALE;


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

// The sun's round: up in the east as white's turn begins, down in the west as it ends, and
// round under the plain through black's, the moon over the table instead. Each half starts
// just after the horizon and ends just past it.
constexpr float SUN_PEAK      = 45.0f;                           ///< Degrees, at noon.
constexpr float SUN_RISE_AZ   = 90.0f;                           ///< Azimuth at sunrise, degrees.
const float     HALF_START    = 0.06f * glm::pi<float>();        ///< Into a half its turn begins.
const float     HALF_LENGTH   = 0.98f * glm::pi<float>();        ///< How far a turn's sun goes.
// Between turns the sun races on to the next sunrise or sunset, a time-lapse.
constexpr float LAPSE_PER_HALF = 2.0f;   ///< Seconds to race through half a round.

// The HUD: the turn card's clock, the scoreboard's tiles, and their colours.
constexpr float CARD_WIDTH    = 470.0f;
constexpr float CARD_HEIGHT   = 100.0f;
constexpr float DIAL_SIZE     = 84.0f;
constexpr int   HURRY_SECONDS = 10;
constexpr float TILE_WIDTH    = 270.0f;
constexpr float TILE_HEIGHT   = 46.0f;
const glm::vec4 HUD_SHADE     = {0.05f, 0.05f, 0.08f, 0.74f};
const glm::vec4 HUD_TEXT      = {0.96f, 0.94f, 0.9f, 1.0f};
const glm::vec4 GOLD          = {1.0f, 0.78f, 0.3f, 1.0f};
const glm::vec4 HURRY_COLOR   = {1.0f, 0.35f, 0.25f, 1.0f};
const glm::vec4 SUNSET_COLOR  = {1.0f, 0.6f, 0.2f, 1.0f};

// A scoreboard tile's background: ivory for white's players, near black for black's.
glm::vec4 teamTile(Color side) {
    return side == Color::White ? glm::vec4(0.93f, 0.91f, 0.86f, 0.94f) : glm::vec4(0.06f, 0.06f, 0.08f, 0.9f);
}

// The lamp over the table, faint by day and the room's light by night.
constexpr float LAMP_DAY   = 15.0f;
constexpr float LAMP_NIGHT = 140.0f;

// The seats: each side's players in a row along its edge of the table, looking at the board.
constexpr float     SEAT_HEIGHT   = 3.8f;
constexpr float     SEAT_DISTANCE = 7.0f;
constexpr float     SEAT_SPACING  = 3.2f;
const glm::vec3     SEAT_TARGET   = {0.0f, 1.5f, 0.0f};

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


// Overshoots past 1 and settles back, for a pop.
float backOut(float t) {
    constexpr float S = 1.70158f;
    const float     u = t - 1.0f;
    return 1.0f + (S + 1.0f) * u * u * u + S * u * u;
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
    resources().add(Shapes::ring(0.72f, 48), "chess:ring");

    spawnTable();
    spawnPieces();
}

void ChessGame::setPlayers(const std::vector<PlayerSetup>& players) {
    if (m_match) return;
    for (const Seat& seat : m_seats) destroy(seat.head);
    m_seats.clear();
    m_setups = players;
    spawnPlayers();
}

// The players are set; the match begins with the first of white's, the view sweeping from the
// table's orbit to the local player's seat as the sun comes up.
void ChessGame::begin() {
    if (m_match || m_setups.empty()) return;
    std::vector<Chess::Player> players;
    for (const PlayerSetup& setup : m_setups) players.push_back({setup.name, setup.side});
    m_match.emplace(std::move(players));
    m_trophies.assign(m_match->players().size(), 0);
    spawnHud();

    m_viewer = 0;
    for (size_t i = 0; i < m_setups.size(); ++i) {
        if (m_setups[i].local) m_viewer = static_cast<int>(i);
    }
    Avatar* first = m_seats[static_cast<size_t>(m_viewer)].avatar;
    first->showHead = false;
    first->setHeadVisible(false);
    m_fromEye   = m_eye;
    m_fromYaw   = m_yaw;
    m_fromPitch = m_pitch;
    m_travel    = 0.0f;
    newTurn();
    refreshLooks();
    updateHud(0.0f);
}

// Before the match: the view circles the table, the day goes by, and the heads wait at their
// seats.
void ChessGame::attract(float dt) {
    m_orbit += dt * 0.07f;
    m_eye = {std::sin(m_orbit) * 15.0f, 5.5f + std::sin(m_orbit * 0.6f), std::cos(m_orbit) * 15.0f};
    Math::toYawPitch(glm::vec3(0.0f, 0.4f, 0.0f) - m_eye, m_yaw, m_pitch);
    if (Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()))) {
        view->rotation = Math::fromYawPitch(m_yaw, m_pitch);
        view->position = m_eye;
    }
    m_sun += dt * 0.06f;
    applySun();
    updateAvatars();
}

void ChessGame::onUpdate(float dt) {
    if (!m_match) {
        attract(dt);
        return;
    }
    advanceGlides(dt);
    advanceTakes(dt);
    animateHints(dt);
    settleDuel(dt);
    updateCamera(dt);
    updateSun(dt);
    updateAvatars();
    updateHud(dt);

    // A click lands only on the local player's turn, once the last move has and the view has
    // reached the seat. The others' turns are theirs, or a debugging bot's.
    const bool ready = settled() && m_travel >= 1.0f && m_duelTime < 0.0f && m_lapse >= 1.0f && !m_match->over();
    if (!ready) return;
    if (m_match->current() == m_viewer) {
        if (input().pressed(ACTION_SELECT) && !input().pointerOverUI()) click(squareUnderPointer());
    } else if (debugBots) {
        playBot(dt);
    }
}

void ChessGame::spawnTable() {
    ResourceManager& res = resources();

    const EntityId board = spawn("Board");
    scene().add(board, Transform{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(WORLD_SCALE)});
    scene().add(board, Mesh{res.findByName<MeshAsset>("chess:board"), ChessLook::board(res)});

    // The lamp over the table, pointing down; updateSun turns it up at night.
    m_lamp = spawn("Lamp");
    scene().add(m_lamp, Transform{{0.0f, 7.5f, 0.0f}, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(1.0f)});
    Light lamp{};
    lamp.type           = LightType::Spot;
    lamp.color          = {1.0f, 0.86f, 0.68f};
    lamp.intensity      = LAMP_DAY;
    lamp.radius         = 20.0f;
    lamp.innerConeAngle = glm::radians(28.0f);
    lamp.outerConeAngle = glm::radians(48.0f);
    lamp.sourceRadius   = 0.35f;
    scene().add(m_lamp, lamp);
}

void ChessGame::spawnPlayers() {
    ResourceManager& res = resources();
    int counts[2] = {0, 0};
    for (const PlayerSetup& setup : m_setups) ++counts[setup.side == Color::White ? 0 : 1];
    int ranks[2] = {0, 0};
    for (size_t p = 0; p < m_setups.size(); ++p) {
        const PlayerSetup& setup = m_setups[p];
        const Color        side  = setup.side;
        const int          count = counts[side == Color::White ? 0 : 1];
        const int          rank  = ranks[side == Color::White ? 0 : 1]++;

        // White's first player sits on white's left, which is +X; black's is at -X.
        const float along = (static_cast<float>(rank) - 0.5f * static_cast<float>(count - 1)) * SEAT_SPACING;
        Seat seat;
        seat.eye   = side == Color::White ? glm::vec3(-along, SEAT_HEIGHT, -SEAT_DISTANCE)
                                          : glm::vec3(along, SEAT_HEIGHT, SEAT_DISTANCE);
        Math::toYawPitch(SEAT_TARGET - seat.eye, seat.yaw, seat.pitch);
        seat.viewEye   = seat.eye;
        seat.viewYaw   = seat.yaw;
        seat.viewPitch = seat.pitch;
        seat.color = setup.color;
        seat.skin  = setup.skin;
        seat.takes = setup.takes;

        MaterialAsset ring;
        ring.type             = MaterialType::Transparent;
        ring.albedo           = glm::vec4(seat.color, 0.85f);
        ring.emission         = seat.color;
        ring.emissiveStrength = 2.0f;
        ring.roughness        = 0.4f;
        ring.doubleSided      = true;
        seat.ring = res.add(std::move(ring), "chess:ring:" + std::to_string(p));

        MaterialAsset beam;
        beam.type             = MaterialType::Transparent;
        beam.albedo           = glm::vec4(seat.color, 0.22f);
        beam.emission         = seat.color;
        beam.emissiveStrength = 3.0f;
        beam.doubleSided      = true;
        seat.beam = res.add(std::move(beam), "chess:beam:" + std::to_string(p));

        const EntityId head = spawn(setup.name.c_str());
        scene().add(head, Transform{seat.eye, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(1.0f)});
        Avatar& avatar   = addBehavior<Avatar>(scene(), head);
        avatar.color     = seat.color;
        avatar.teamWhite = side == Color::White;
        seat.avatar      = &avatar;
        seat.head        = head;
        m_seats.push_back(seat);
    }
}

void ChessGame::spawnPieces() {
    const Chess::Position start = Chess::Position::start();
    for (Square s = 0; s < 64; ++s) {
        const Chess::Piece piece = start.at(s);
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
    drawn.body = spawn(ChessLook::pieceMesh(piece.type));
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
    mesh->mesh = res.findByName<MeshAsset>(ChessLook::pieceMesh(type));

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

void ChessGame::mark(Square square, float size, MaterialHandle material, bool ring, float delay, bool target) {
    const EntityId id = spawn("Hint");
    scene().add(id, Transform{squareCentre(square), {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(0.0f)});
    Mesh drawn{resources().findByName<MeshAsset>(ring ? "chess:ring" : "chess:disc"), material};
    drawn.castShadows = false;
    scene().add(id, std::move(drawn));
    m_hints.push_back({id, square, size, delay, target, ring});
}

// The hints pop up one after another, rippling out from the piece, then breathe; the one under
// the pointer swells, and the chosen piece lifts off the board and hovers.
void ChessGame::animateHints(float dt) {
    m_hintTime += dt;
    const bool   mine    = m_match && m_match->current() == m_viewer && settled();
    const Square hovered = mine && !m_hints.empty() ? squareUnderPointer() : Chess::NO_SQUARE;
    for (const Hint& hint : m_hints) {
        Transform* t = scene().tryGet<Transform>(hint.id);
        if (!t) continue;
        const float appear  = std::clamp((m_hintTime - hint.delay) / 0.28f, 0.0f, 1.0f);
        const float pop     = appear < 1.0f ? backOut(appear) : 1.0f;
        const float breathe = 1.0f + 0.07f * std::sin(m_hintTime * 3.5f + static_cast<float>(hint.square) * 0.7f);
        const bool  over    = hint.target && hint.square == hovered;
        const float across  = hint.size * pop * breathe * (over ? 1.35f : 1.0f);
        t->scale    = hint.ring ? glm::vec3(across * 0.5f, 1.0f, across * 0.5f) : glm::vec3(across, 0.006f, across);
        t->position = squareCentre(hint.square) + glm::vec3(0.0f, (hint.ring ? 0.007f : 0.004f) + (over ? 0.01f : 0.0f), 0.0f);
    }
    if (m_selected != Chess::NO_SQUARE && m_glides.empty()) {
        if (Transform* t = scene().tryGet<Transform>(m_pieces[static_cast<size_t>(m_selected)])) {
            const float rise = smooth(std::min(m_hintTime / 0.2f, 1.0f));
            t->position.y = BOARD_TOP + rise * (0.14f + 0.025f * std::sin(m_hintTime * 3.0f));
        }
    }
}

// For debugging alone: a bot looks the board over, picks a move - the best capture going, give or take a whim - and
// plays it after a moment, its head pointing at the piece and then where it goes meanwhile.
void ChessGame::playBot(float dt) {
    constexpr float THINK = 2.2f;
    if (!m_botMove) {
        const Chess::Position& position = m_match->position();
        float best = -1.0f;
        for (const Chess::Move& move : position.legalMoves()) {
            const Square taken = position.takenBy(move);
            const float  worth = (taken != Chess::NO_SQUARE ? static_cast<float>(Chess::captureValue(position.at(taken).type)) : 0.0f)
                + Math::Random::range(0.0f, 2.5f);
            if (worth > best) {
                best      = worth;
                m_botMove = move;
            }
        }
        m_botTime = 0.0f;
        if (!m_botMove) return;
    }
    m_botTime += dt;
    if (m_botTime < THINK) return;
    const Chess::Move move = *m_botMove;
    m_botMove.reset();
    tryMove(move);
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

    // A ring under the piece; a dot on each square it can reach, a ring round each piece it can
    // take; red where it is a duel.
    ResourceManager& res = resources();
    m_hintTime = 0.0f;
    const bool teammates = m_match->ownerOf(square) >= 0 && m_match->ownerOf(square) != m_match->current();
    mark(square, SQUARE * 0.95f, teammates ? ChessLook::duel(res) : ChessLook::chosen(res), true);
    for (const Chess::Move& move : m_choices) {
        if (move.promotion != PieceType::None && move.promotion != PieceType::Queen) continue;
        const bool  takes = position.takenBy(move) != Chess::NO_SQUARE;
        const bool  duel  = m_match->duelFor(move).has_value();
        const int   far   = std::max(std::abs(Chess::fileOf(move.to) - Chess::fileOf(square)), std::abs(Chess::rankOf(move.to) - Chess::rankOf(square)));
        mark(move.to, takes ? SQUARE * 0.92f : SQUARE * 0.38f, duel ? ChessLook::duel(res) : ChessLook::hint(res), takes,
             0.04f * static_cast<float>(far), true);
    }
}

void ChessGame::clearChoices() {
    // The lifted piece back down on its square, unless a move is about to carry it.
    if (m_selected != Chess::NO_SQUARE) {
        if (Transform* t = scene().tryGet<Transform>(m_pieces[static_cast<size_t>(m_selected)])) t->position = squareCentre(m_selected);
    }
    for (const Hint& hint : m_hints) destroy(hint.id);
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
    m_hintTime = 0.0f;
    mark(duel.move.from, SQUARE * 0.95f, ChessLook::chosen(resources()), true);
    mark(duel.move.to, SQUARE * 0.92f, ChessLook::duel(resources()), true, 0.1f);

    std::string detail = nameOf(duel.challenger);
    if (duel.kind == Chess::DuelKind::Teammate) {
        detail += std::string(" wants ") + nameOf(duel.defender) + "'s " + pieceName(position.at(duel.move.from).type);
    } else {
        detail += std::string(" goes for ") + nameOf(duel.defender) + "'s "
            + pieceName(position.at(position.takenBy(duel.move)).type);
    }
    showNews(title, detail, duelSeconds, HURRY_COLOR);
    LOG_INFO("%s", detail.c_str());
}

// The coin spins for duelSeconds; the winner's move is played, or the loser's turn handed on.
void ChessGame::settleDuel(float dt) {
    if (m_duelTime < 0.0f || !m_match->duel()) return;
    m_duelTime += dt;
    if (m_duelTime >= duelSeconds) finishDuel(Math::Random::boolean());
}

// A duel is part of its turn, on the turn's clock: a defender who wins plays in what is left
// of that day or night, and the clock starts again only once a move is made.
void ChessGame::finishDuel(bool challengerWon) {
    const Chess::Duel     duel   = *m_match->duel();
    const Chess::Position before = m_match->position();
    m_duelTime = -1.0f;
    clearChoices();
    m_match->resolveDuel(challengerWon);

    const std::string winner = nameOf(challengerWon ? duel.challenger : duel.defender);
    LOG_INFO("%s wins the duel", winner.c_str());
    if (challengerWon && m_match->duel()) {
        startDuel(winner + " wins! Duel two");
    } else if (challengerWon) {
        LOG_INFO("%s plays %s", nameOf(duel.challenger), Chess::Position::toUci(duel.move).c_str());
        showNews(winner + " wins!", "and plays the move", 1.8f, seatColor(duel.challenger));
        showMove(duel.move, before);
    } else if (duel.kind == Chess::DuelKind::Teammate) {
        showNews(winner + " holds on!", "and plays this turn instead", 2.2f, seatColor(duel.defender));
    } else {
        showNews(winner + " holds on!", "keeps the piece, and moves next", 2.2f, seatColor(duel.defender));
    }
}

// The turn's clock is the sun, and runs through a duel too; it stands still while pieces move
// or the view travels. A duel the sun sets on is the defender's: they held out.
void ChessGame::updateSun(float dt) {
    const bool waiting = m_match->over() || !settled() || m_travel < 1.0f;
    if (m_lapse < 1.0f) {
        // Once the last move has landed.
        if (settled()) m_lapse = std::min(m_lapse + dt / m_lapseTime, 1.0f);
        m_sun = glm::mix(m_lapseFrom, m_turnStart, smooth(m_lapse));
    } else if (!waiting && turnSeconds > 0.0f) {
        m_sun += HALF_LENGTH / turnSeconds * dt;
        if (m_sun >= m_turnEnd) {
            m_sun = m_turnEnd;
            if (m_match->duel()) finishDuel(false);
            else timeOut();
        }
    }
    applySun();
}

// The sky from the sun's angle round it, and the lamp by how dark it is.
void ChessGame::applySun() {
    const float elevation = SUN_PEAK * std::sin(m_sun);
    SkySettings& sky = scene().environment().sky;
    sky.sunElevation = elevation;
    sky.sunAzimuth   = SUN_RISE_AZ + glm::degrees(m_sun);

    // The lamp comes up through dusk, and down again with the dawn.
    const float night = glm::smoothstep(8.0f, -6.0f, elevation);
    if (Light* lamp = scene().tryGet<Light>(m_lamp)) lamp->intensity = glm::mix(LAMP_DAY, LAMP_NIGHT, night);
}

// The side to move's turn begins: the sun hurries on to its sunrise, or its sunset, the next
// one round; a colour that moves twice running waits out the other's half.
void ChessGame::newTurn() {
    const float half  = m_match->position().sideToMove() == Color::White ? 0.0f : glm::pi<float>();
    const float start = half + HALF_START;
    const float laps  = std::ceil((m_sun - start) / glm::two_pi<float>());
    m_turnStart = start + std::max(laps, 0.0f) * glm::two_pi<float>();
    m_turnEnd   = m_turnStart + HALF_LENGTH;
    m_lapseFrom = m_sun;
    m_lapse     = 0.0f;
    m_lapseTime = std::clamp((m_turnStart - m_sun) / glm::pi<float>() * LAPSE_PER_HALF, 0.6f, 2.6f);
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
    const bool day = m_match->position().sideToMove() == Color::White;
    showNews(day ? "Sunset!" : "Sunrise!", who + " ran out of time", 2.2f, SUNSET_COLOR);
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
        // A squash wants the attacker to come down on it from a hop.
        if (glide.taker >= 0 && m_seats[static_cast<size_t>(glide.taker)].takes == TakeStyle::Squash) {
            glide.lift = std::max(glide.lift, 0.9f);
        }
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
    newTurn();
}

void ChessGame::advanceGlides(float dt) {
    const float step     = moveSeconds > 0.0f ? dt / moveSeconds : 1.0f;
    bool        promoted = false;
    for (Glide& glide : m_glides) {
        glide.t = std::min(glide.t + step, 1.0f);
        // Eased along the ground, and a half sine of lift over it.
        const glm::vec3 at = glm::mix(glide.from, glide.to, smooth(glide.t))
            + glm::vec3(0.0f, glide.lift * std::sin(glide.t * glm::pi<float>()), 0.0f);
        if (Transform* t = scene().tryGet<Transform>(glide.piece)) t->position = at;

        // The victim goes as the taker's style says, some way into the move or on landing.
        const TakeStyle style = glide.taker >= 0 ? m_seats[static_cast<size_t>(glide.taker)].takes : TakeStyle::Float;
        if (glide.victim && glide.t >= takeStart(style)) {
            startTake(glide.victim, glide.taker);
            glide.victim = {};
        }
        if (glide.t >= 1.0f) {
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
    if (promoted) refreshLooks();
}

void ChessGame::startTake(EntityId piece, int taker) {
    const Seat* seat = taker >= 0 ? &m_seats[static_cast<size_t>(taker)] : nullptr;
    Take take;
    take.piece = piece;
    take.style = seat ? seat->takes : TakeStyle::Float;
    if (const Transform* t = scene().tryGet<Transform>(piece)) {
        take.from   = t->position;
        take.facing = t->rotation;
    }
    take.to = seat ? trophySpot(taker) : take.from;

    const TakeEffect effect = takeEffect(take.style);
    if (effect != TakeEffect::None && seat) {
        ResourceManager& res = resources();
        take.effect = spawn("Take Effect");
        scene().add(take.effect, Transform{});
        Mesh drawn{res.findByName<MeshAsset>(effect == TakeEffect::Ripple ? "chess:ring" : "chess:disc"),
                   effect == TakeEffect::Ripple ? seat->ring : seat->beam};
        drawn.visible     = false;
        drawn.castShadows = false;
        scene().add(take.effect, std::move(drawn));
    }
    m_takes.push_back(take);
}

void ChessGame::advanceTakes(float dt) {
    for (Take& take : m_takes) {
        take.t = std::min(take.t + dt / takeSeconds(take.style), 1.0f);
        const TakePose pose = takePose(take.style, take.t, take.from, take.to);
        if (Transform* t = scene().tryGet<Transform>(take.piece)) {
            t->position = pose.position;
            t->rotation = glm::angleAxis(pose.spin, glm::vec3(0.0f, 1.0f, 0.0f)) * take.facing;
            t->scale    = pose.scale * WORLD_SCALE;
        }
        if (Mesh* effect = scene().tryGet<Mesh>(take.effect)) effect->visible = pose.showEffect && take.t < 1.0f;
        if (Transform* t = scene().tryGet<Transform>(take.effect)) {
            t->position = pose.effectAt;
            t->scale    = pose.effectScale;
        }
        if (take.t >= 1.0f && take.effect) destroy(take.effect);
    }
    m_takes.erase(
        std::remove_if(m_takes.begin(), m_takes.end(), [](const Take& t) { return t.t >= 1.0f; }),
        m_takes.end()
    );
}

// The next place in @p taker's trophy row, along the table's edge in front of their seat.
glm::vec3 ChessGame::trophySpot(int taker) {
    constexpr int   PER_ROW = 6;
    constexpr float GAP     = 0.42f;
    const Seat& seat  = m_seats[static_cast<size_t>(taker)];
    const int   count = m_trophies[static_cast<size_t>(taker)]++;
    const int   col   = count % PER_ROW;
    const int   row   = count / PER_ROW;
    const float near  = seat.eye.z < 0.0f ? -1.0f : 1.0f;
    const float x     = seat.eye.x + (static_cast<float>(col) - 0.5f * static_cast<float>(PER_ROW - 1)) * GAP;
    return {
        std::clamp(x, -TABLE_RADIUS + 1.6f, TABLE_RADIUS - 1.6f),
        0.0f,
        near * (BOARD_HALF + 0.6f + GAP * static_cast<float>(row)),
    };
}

// Nothing is moving on the table.
bool ChessGame::settled() const { return m_glides.empty() && m_takes.empty(); }

// A piece nobody owns is the set's own wood; an owned one wears its owner's skin and ring.
void ChessGame::refreshLooks() {
    ResourceManager& res = resources();
    for (const DrawnPiece& drawn : m_drawn) {
        if (Mesh* ring = scene().tryGet<Mesh>(drawn.ring)) ring->visible = false;
    }
    for (Square s = 0; s < 64; ++s) {
        const DrawnPiece* drawn = drawnOf(m_pieces[static_cast<size_t>(s)]);
        if (!m_pieces[static_cast<size_t>(s)] || !drawn) continue;
        const int            owner = m_match ? m_match->ownerOf(s) : -1;
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
    const Seat&     seat  = m_seats[static_cast<size_t>(m_viewer)];
    const glm::vec3 goal  = seat.viewEye;
    const float     goalY = seat.viewYaw;
    const float     goalP = seat.viewPitch;

    if (m_travel < 1.0f) {
        // Round the table to the seat, wide of the other players' heads and rising a little,
        // with the board held in view between the two ends' own angles.
        m_travel = std::min(m_travel + dt / TRAVEL_SECONDS, 1.0f);
        const float e      = smooth(m_travel);
        const float arc    = std::sin(m_travel * glm::pi<float>());
        const float from   = std::atan2(m_fromEye.x, m_fromEye.z);
        const float turn   = std::remainder(std::atan2(goal.x, goal.z) - from, glm::two_pi<float>());
        const float radius = glm::mix(glm::length(glm::vec2(m_fromEye.x, m_fromEye.z)), glm::length(glm::vec2(goal.x, goal.z)), e)
            + 2.5f * arc;
        const float around = from + turn * e;
        m_eye = {std::sin(around) * radius, glm::mix(m_fromEye.y, goal.y, e) + 1.2f * arc, std::cos(around) * radius};

        const glm::quat ends = glm::slerp(Math::fromYawPitch(m_fromYaw, m_fromPitch), Math::fromYawPitch(goalY, goalP), e);
        const glm::quat view = glm::slerp(ends, Math::lookRotation(SEAT_TARGET - m_eye), arc);
        Math::toYawPitch(Math::computeForward(view), m_yaw, m_pitch);
        if (m_travel >= 1.0f) {
            m_eye   = goal;
            m_yaw   = goalY;
            m_pitch = goalP;
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
    const Chess::Duel* duel = m_match && m_match->duel() ? &*m_match->duel() : nullptr;
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
        if (m_botMove && m_match && player == m_match->current()) {
            // A bot thinking: at the piece, then at where it goes.
            const Square at = m_botTime < 1.1f ? m_botMove->from : m_botMove->to;
            look = squareCentre(at);
            seat.avatar->setPose(seat.eye, Math::lookRotation(look - seat.eye));
            seat.avatar->setPointer(true, squareCentre(at));
            continue;
        }
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
        text.color     = HUD_TEXT;
        scene().add(id, std::move(text));
        return id;
    };
    // An element by its centre, inside its parent's top-left frame.
    const auto centred = [](const glm::vec2& centre, const glm::vec2& size) {
        return UIElement::at({0.0f, 0.0f}, centre - size * 0.5f, size);
    };

    // The turn card, top centre: a dial with the sun and the moon on opposite sides, turning
    // round as the turn's day or night goes by, the seconds left in its middle; beside it,
    // whose move it is.
    const EntityId  card   = m_card = panel("Turn", canvas, UIElement::at({0.5f, 0.0f}, {0.0f, 14.0f}, {CARD_WIDTH, CARD_HEIGHT}), HUD_SHADE, CARD_HEIGHT * 0.5f);
    const glm::vec2 hub    = {CARD_HEIGHT * 0.5f + 4.0f, CARD_HEIGHT * 0.5f};
    const EntityId  dial   = spawn("Dial", card);
    scene().add(dial, centred(hub, glm::vec2(DIAL_SIZE)));
    const float     radius = DIAL_SIZE * 0.5f;
    // The face, a texture painted every frame as it turns.
    TextureAsset face;
    face.params.width           = Dial::SIZE;
    face.params.height          = Dial::SIZE;
    face.params.internalFormat  = TextureInternalFormat::SRGBA8;
    face.params.format          = TexturePixelFormat::RGBA;
    face.params.generateMipmaps = false;
    Dial::paint(face.pixelData, 0.0f, 0.0f);
    m_dialFace = resources().add(std::move(face), "hud:dial");
    const EntityId disc = panel("Dial Face", dial, UIElement::at({0.0f, 0.0f}, {0.0f, 0.0f}, glm::vec2(DIAL_SIZE)), HUD_TEXT, 0.0f);
    if (UIImage* image = scene().tryGet<UIImage>(disc)) image->texture = m_dialFace;
    panel("Dial Hub", dial, centred({radius, radius}, {38.0f, 38.0f}), {0.06f, 0.06f, 0.1f, 0.92f}, 19.0f);
    m_clockSeconds = label("Clock Seconds", dial, centred({radius, radius}, {DIAL_SIZE, 30.0f}), 24.0f, UIText::Align::Center);

    const float textLeft = hub.x + radius + 18.0f;
    const float textWide = CARD_WIDTH - textLeft - 24.0f;
    m_turnName   = label("Turn Name", card, UIElement::at({0.0f, 0.0f}, {textLeft, 14.0f}, {textWide, 44.0f}), 36.0f, UIText::Align::Left);
    m_turnDetail = label("Turn Detail", card, UIElement::at({0.0f, 0.0f}, {textLeft, 56.0f}, {textWide, 28.0f}), 21.0f, UIText::Align::Left);

    // The scoreboard, top left: a tile a player in their team's colours, white's then black's.
    const EntityId board = spawn("Scores", canvas);
    scene().add(board, UIElement::at({0.0f, 0.0f}, {20.0f, 18.0f}, {TILE_WIDTH, 600.0f}));
    float y = 0.0f;
    m_scoreRows.resize(m_seats.size());
    for (const Color side : {Color::White, Color::Black}) {
        for (size_t i = 0; i < m_seats.size(); ++i) {
            if (m_match->players()[i].side != side) continue;
            ScoreRow& row = m_scoreRows[i];
            row.y    = y;
            row.tile = panel("Score Tile", board, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {TILE_WIDTH, TILE_HEIGHT}), teamTile(side), 14.0f);
            panel("Swatch", row.tile, UIElement::at({0.0f, 0.5f}, {14.0f, 0.0f}, {14.0f, 14.0f}), glm::vec4(m_seats[i].color, 1.0f), 7.0f);
            row.name   = label("Name", row.tile, UIElement::at({0.0f, 0.5f}, {38.0f, 0.0f}, {170.0f, TILE_HEIGHT}), 26.0f, UIText::Align::Left);
            row.points = label("Points", row.tile, UIElement::at({1.0f, 0.5f}, {-14.0f, 0.0f}, {70.0f, TILE_HEIGHT}), 30.0f, UIText::Align::Right);
            y += TILE_HEIGHT + 6.0f;
        }
    }

}

void ChessGame::showNews(const std::string& title, const std::string& detail, float seconds, const glm::vec4& accent) {
    m_newsTitle  = title;
    m_newsDetail = detail;
    m_newsTime   = seconds;
    m_newsLength = seconds;
    m_newsAccent = accent;
}

glm::vec4 ChessGame::seatColor(int player) const {
    return player >= 0 && static_cast<size_t>(player) < m_seats.size() ? glm::vec4(m_seats[static_cast<size_t>(player)].color, 1.0f) : HUD_TEXT;
}

void ChessGame::updateHud(float dt) {
    m_hudTime   += dt;
    m_newsTime = std::max(m_newsTime - dt, 0.0f);

    const Chess::Position&            position = m_match->position();
    const std::vector<Chess::Player>& players  = m_match->players();
    const int  current = m_match->current();
    int best = 0;
    for (const Chess::Player& player : players) best = std::max(best, player.score);

    // The dial turns a half round each turn, clockwise: the sun's face is on top as white's
    // turn begins and the moon's as it ends, and round again through black's.
    const float through = std::clamp((m_sun - m_turnStart) / HALF_LENGTH, 0.0f, 1.0f);
    const int   left    = static_cast<int>(std::ceil((1.0f - through) * turnSeconds));
    const bool  hurry   = left <= HURRY_SECONDS && !m_match->over();
    const float pulse   = hurry ? 0.5f + 0.5f * std::sin(m_hudTime * 9.0f) : 0.0f;
    // Painted again only once it has turned a hair's width, or every frame while it pulses.
    const float turned = m_sun - HALF_START;
    if (hurry || std::abs(turned - m_dialPainted) > 0.004f) {
        if (TextureAsset* face = resources().tryEdit(m_dialFace)) {
            Dial::paint(face->pixelData, turned, hurry ? 0.4f + 0.6f * pulse : 0.0f);
            resources().commit(m_dialFace);
            m_dialPainted = hurry ? -1000.0f : turned;
        }
    }
    if (UIText* seconds = scene().tryGet<UIText>(m_clockSeconds)) {
        setText(seconds, m_match->over() ? "" : std::to_string(left));
        seconds->color = hurry ? HURRY_COLOR : HUD_TEXT;
    }

    // Whose move, and what is going on.
    std::string name   = current < 0 ? "" : current == m_viewer ? "Your move" : nameOf(current);
    std::string detail;
    glm::vec4   color  = current >= 0 ? glm::vec4(m_seats[static_cast<size_t>(current)].color, 1.0f) : HUD_TEXT;
    if (m_match->over()) {
        // The game is won on points, whoever mated.
        std::vector<std::string> leaders;
        for (const Chess::Player& player : players) {
            if (player.score == best) leaders.push_back(player.name);
        }
        name  = leaders.size() == 1 ? leaders[0] + " wins!" : "A tie!";
        color = GOLD;
        switch (position.outcome()) {
            case Chess::Outcome::Checkmate:            detail = "Checkmate";                     break;
            case Chess::Outcome::Stalemate:            detail = "Stalemate";                     break;
            case Chess::Outcome::FiftyMoves:           detail = "Fifty moves without a capture"; break;
            case Chess::Outcome::Repetition:           detail = "The same position three times"; break;
            case Chess::Outcome::InsufficientMaterial: detail = "Neither side can mate";         break;
            case Chess::Outcome::Ongoing:                                                        break;
        }
        detail += ", " + std::to_string(best) + " points";
    } else if (const auto& duel = m_match->duel()) {
        detail = std::string("DUEL with ") + nameOf(duel->defender);
    } else if (position.inCheck(position.sideToMove())) {
        detail = "CHECK!";
    }
    // News takes the card over: its two lines, and the card washed in its colour, fading in
    // and out.
    const float shown = m_newsLength - m_newsTime;
    const float news  = m_newsTime > 0.0f ? std::min(smooth(std::min(shown / 0.2f, 1.0f)), std::min(m_newsTime / 0.3f, 1.0f)) : 0.0f;
    if (m_newsTime > 0.0f) {
        name   = m_newsTitle;
        detail = m_newsDetail;
        color  = glm::vec4(glm::mix(glm::vec3(m_newsAccent), glm::vec3(1.0f), 0.3f), 1.0f);
    }
    if (UIImage* fill = scene().tryGet<UIImage>(m_card)) {
        fill->color             = glm::mix(HUD_SHADE, glm::vec4(glm::vec3(m_newsAccent) * 0.32f + 0.02f, 0.94f), news);
        fill->shape.borderWidth = news > 0.0f ? 2.5f : 0.0f;
        fill->shape.borderColor = glm::vec4(glm::vec3(m_newsAccent), news);
    }
    if (UIText* text = scene().tryGet<UIText>(m_turnName)) {
        setText(text, name);
        text->color = color;
    }
    // The name sits in the middle of the card, and moves up to make room for news.
    if (UIElement* place = scene().tryGet<UIElement>(m_turnName)) place->position.y = detail.empty() ? (CARD_HEIGHT - 44.0f) * 0.5f : 14.0f;
    if (UIText* text = scene().tryGet<UIText>(m_turnDetail)) {
        setText(text, detail);
        const bool alarm = m_newsTime <= 0.0f && (detail == "CHECK!" || m_match->duel().has_value());
        text->color = alarm ? HURRY_COLOR : glm::vec4(glm::vec3(HUD_TEXT), 0.7f);
    }

    // The scoreboard, most points first: each tile in its team's colours, sliding to its place,
    // the player moving ringed in their own colour, and the leaders at the end.
    std::vector<size_t> order(m_scoreRows.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return players[a].score > players[b].score; });
    const float slide = std::min(dt * 10.0f, 1.0f);
    for (size_t rank = 0; rank < order.size(); ++rank) {
        ScoreRow& row = m_scoreRows[order[rank]];
        row.y += (static_cast<float>(rank) * (TILE_HEIGHT + 6.0f) - row.y) * slide;
        if (UIElement* place = scene().tryGet<UIElement>(row.tile)) place->position.y = row.y;
    }
    for (size_t i = 0; i < m_scoreRows.size(); ++i) {
        const ScoreRow& row   = m_scoreRows[i];
        const bool      now   = static_cast<int>(i) == current && !m_match->over();
        const bool      lead  = m_match->over() && players[i].score == best;
        const bool      white = players[i].side == Color::White;
        if (UIImage* tile = scene().tryGet<UIImage>(row.tile)) {
            tile->shape.borderWidth = now || lead ? 3.0f : 0.0f;
            tile->shape.borderColor = glm::vec4(m_seats[i].color, 1.0f);
        }
        const glm::vec4 ink = white ? glm::vec4(0.1f, 0.1f, 0.12f, now || lead ? 1.0f : 0.75f)
                                    : glm::vec4(0.96f, 0.94f, 0.9f, now || lead ? 1.0f : 0.75f);
        if (UIText* text = scene().tryGet<UIText>(row.name)) {
            setText(text, players[i].name);
            text->color = ink;
        }
        if (UIText* text = scene().tryGet<UIText>(row.points)) {
            setText(text, std::to_string(players[i].score));
            text->color = ink;
        }
    }
}

const char* ChessGame::nameOf(int player) const {
    const auto& players = m_match->players();
    return player >= 0 && static_cast<size_t>(player) < players.size() ? players[static_cast<size_t>(player)].name.c_str() : "Nobody";
}

} // namespace Game
