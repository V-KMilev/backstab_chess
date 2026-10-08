#define VKM_LOG_CATEGORY "CHESS"

#include "chess_game.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/math/bounds.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_text.h"
#include "platform/window/window_manager.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

namespace Game {

namespace {

using Chess::Color;
using Chess::PieceType;
using Chess::Square;

// The set is modelled life size, its board 55 cm across; the world is ten times that, so
// the physics of a falling piece works at the scale it is tuned for. Gravity follows.
constexpr float WORLD_SCALE = 10.0f;
constexpr float SQUARE      = 0.058f * WORLD_SCALE;
constexpr float BOARD_TOP   = 0.017f * WORLD_SCALE;
constexpr float BOARD_HALF  = 0.277f * WORLD_SCALE;
constexpr float GRAVITY     = 9.81f * WORLD_SCALE;

constexpr const char* ACTION_SELECT = "chess/select";
constexpr const char* ACTION_ORBIT  = "chess/orbit";
constexpr const char* ACTION_SETS[] = {"chess/set1", "chess/set2", "chess/set3", "chess/set4"};

constexpr float ORBIT_SPEED = 0.006f;  ///< Radians per pixel dragged.

// Each piece's height and radius in world units, for the body it becomes when knocked off.
struct Shape {
    float height;
    float radius;
};

Shape shapeOf(PieceType type) {
    switch (type) {
        case PieceType::Pawn:   return {0.54f, 0.14f};
        case PieceType::Knight: return {0.75f, 0.19f};
        case PieceType::Bishop: return {0.86f, 0.16f};
        case PieceType::Rook:   return {0.61f, 0.18f};
        case PieceType::Queen:  return {0.90f, 0.20f};
        case PieceType::King:   return {0.95f, 0.21f};
        default:                return {0.5f, 0.15f};
    }
}

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

const char* sideName(Color side) { return side == Color::White ? "White" : "Black"; }

// A pseudo-random number in [-1, 1] from a counter, so a capture's tumble varies without a
// generator to carry around.
float wobble(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n ^= n >> 4u;
    n *= 0x27d4eb2du;
    n ^= n >> 15u;
    return static_cast<float>(n & 0xffffu) / 32767.5f - 1.0f;
}

} // namespace

void ChessGame::onStart() {
    InputMap& map = input();
    map.define(ACTION_SELECT, {InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_LEFT, 1.0f}});
    map.define(ACTION_ORBIT, {InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, 1.0f}});
    for (int i = 0; i < 4; ++i) map.define(ACTION_SETS[i], {InputBinding{InputSource::Key, GLFW_KEY_1 + i, 1.0f}});

    scene().physics().gravity = {0.0f, -GRAVITY, 0.0f};

    spawnTable();
    spawnPieces();

    // The status line, top centre.
    const EntityId canvas = spawn("Status Canvas");
    UICanvas layer;
    layer.sortOrder = 10;
    scene().add(canvas, std::move(layer));
    m_status = spawn("Status", canvas);
    scene().add(m_status, UIElement::at({0.5f, 0.0f}, {0.0f, 36.0f}, {900.0f, 60.0f}));
    UIText text;
    text.pixelSize = 34.0f;
    text.align     = UIText::Align::Center;
    text.valign    = UIText::VAlign::Middle;
    text.color     = {0.95f, 0.93f, 0.88f, 0.92f};
    scene().add(m_status, std::move(text));

    updateStatus();
    updateCamera(0.0f);
}

void ChessGame::onUpdate(float dt) {
    for (int i = 0; i < 4; ++i) {
        if (input().pressed(ACTION_SETS[i])) pieceSet = i;
    }
    if (pieceSet != m_shownSet) applySet();

    advanceGlides(dt);
    updateCamera(dt);

    // A click lands only once the last move has: the board is the position while nothing moves.
    if (m_glides.empty() && input().pressed(ACTION_SELECT) && !input().pointerOverUI()) {
        click(squareUnderPointer());
    }
}

void ChessGame::spawnTable() {
    ResourceManager& res = resources();

    const EntityId board = spawn("Board");
    scene().add(board, Transform{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(WORLD_SCALE)});
    scene().add(board, Mesh{res.findByName<MeshAsset>("chess:board"), ChessLook::board(res)});
    // Physics ignores a Transform's scale, so every shape is in world units.
    Collider boardShape;
    boardShape.parts[0].center      = {0.0f, BOARD_TOP * 0.5f, 0.0f};
    boardShape.parts[0].halfExtents = {BOARD_HALF, BOARD_TOP * 0.5f, BOARD_HALF};
    scene().add(board, std::move(boardShape));
    Rigidbody boardBody;
    boardBody.motion = RigidbodyMotion::Static;
    scene().add(board, std::move(boardBody));

    // The table: a slab the board rests on, wide enough to catch what falls off it.
    const MeshHandle cube  = res.add(generateCube(), "chess:cube");
    const EntityId   table = spawn("Table");
    scene().add(table, Transform{{0.0f, -0.25f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {40.0f, 0.5f, 28.0f}});
    scene().add(table, Mesh{cube, ChessLook::table(res)});
    Collider tableShape;
    tableShape.parts[0].halfExtents = {20.0f, 0.25f, 14.0f};
    scene().add(table, std::move(tableShape));
    Rigidbody tableBody;
    tableBody.motion = RigidbodyMotion::Static;
    scene().add(table, std::move(tableBody));
}

void ChessGame::spawnPieces() {
    for (Square s = 0; s < 64; ++s) {
        const Chess::Piece piece = m_position.at(s);
        if (!piece.empty()) m_pieces[static_cast<size_t>(s)] = spawnPiece(piece, s);
    }
    applySet();
}

EntityId ChessGame::spawnPiece(Chess::Piece piece, Square square) {
    const EntityId entity = spawn(meshName(piece.type));
    // Black's pieces face the other way; it shows on a knight.
    const glm::quat facing = piece.color == Color::White
        ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
        : glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    scene().add(entity, Transform{squareCentre(square), facing, glm::vec3(WORLD_SCALE)});
    scene().add(entity, Mesh{});
    m_drawn.push_back({entity, piece.color});
    setMesh(entity, piece.type);
    return entity;
}

void ChessGame::setMesh(EntityId entity, PieceType type) {
    ResourceManager& res = resources();
    if (Mesh* mesh = scene().tryGet<Mesh>(entity)) mesh->mesh = res.findByName<MeshAsset>(meshName(type));

    // A bishop's ball is a mesh of its own, carried as a child.
    if (type == PieceType::Bishop) {
        const EntityId top = spawn("chess:bishop_top", entity);
        scene().add(top, Transform{});
        Mesh ball;
        ball.mesh     = res.findByName<MeshAsset>("chess:bishop_top");
        ball.material = scene().get<Mesh>(entity).material;
        scene().add(top, std::move(ball));
        const auto owner = std::find_if(m_drawn.begin(), m_drawn.end(), [&](const Drawn& d) { return d.entity == entity; });
        m_drawn.push_back({top, owner != m_drawn.end() ? owner->side : Color::White});
    }
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

void ChessGame::click(Square square) {
    if (m_position.outcome() != Chess::Outcome::Ongoing) return;

    if (m_selected != Chess::NO_SQUARE && square != Chess::NO_SQUARE) {
        // A pawn reaching the last rank becomes a queen until there is a way to ask.
        const auto choice = std::find_if(m_choices.begin(), m_choices.end(), [&](const Chess::Move& m) {
            return m.to == square && (m.promotion == PieceType::None || m.promotion == PieceType::Queen);
        });
        if (choice != m_choices.end()) {
            const Chess::Move move = *choice;
            clearChoices();
            play(move);
            return;
        }
    }

    const bool own = square != Chess::NO_SQUARE && !m_position.at(square).empty()
        && m_position.at(square).color == m_position.sideToMove();
    clearChoices();
    if (own && square != m_selected) showChoices(square);
}

void ChessGame::showChoices(Square square) {
    m_selected = square;
    for (const Chess::Move& move : m_position.legalMoves()) {
        if (move.from == square) m_choices.push_back(move);
    }

    ResourceManager& res  = resources();
    const MeshHandle disc = res.add(generateCylinder(0.5f, 1.0f, 40), "chess:disc");
    const auto mark = [&](Square at, float size, MaterialHandle material) {
        const EntityId hint = spawn("Hint");
        scene().add(hint, Transform{squareCentre(at) + glm::vec3(0.0f, 0.004f, 0.0f), {1.0f, 0.0f, 0.0f, 0.0f}, {size, 0.006f, size}});
        Mesh drawn{disc, material};
        drawn.castShadows = false;
        scene().add(hint, std::move(drawn));
        m_hints.push_back(hint);
    };
    mark(square, SQUARE * 0.9f, ChessLook::chosen(res));
    for (const Chess::Move& move : m_choices) {
        if (move.promotion != PieceType::None && move.promotion != PieceType::Queen) continue;
        const bool takes = !m_position.at(move.to).empty() || move.kind == Chess::MoveKind::EnPassant;
        mark(move.to, takes ? SQUARE * 0.85f : SQUARE * 0.32f, ChessLook::hint(res));
    }
}

void ChessGame::clearChoices() {
    for (const EntityId hint : m_hints) destroy(hint);
    m_hints.clear();
    m_choices.clear();
    m_selected = Chess::NO_SQUARE;
}

void ChessGame::play(const Chess::Move& move) {
    const auto idx = [](Square s) { return static_cast<size_t>(s); };

    // What is taken, and where it stands: beside the arrival square for en passant.
    Square taken = Chess::NO_SQUARE;
    if (move.kind == Chess::MoveKind::EnPassant) taken = Chess::squareAt(Chess::fileOf(move.to), Chess::rankOf(move.from));
    else if (!m_position.at(move.to).empty()) taken = move.to;

    const PieceType mover = m_position.at(move.from).type;
    Glide glide;
    glide.piece   = m_pieces[idx(move.from)];
    glide.from    = squareCentre(move.from);
    glide.to      = squareCentre(move.to);
    glide.lift    = mover == PieceType::Knight ? 0.9f : 0.12f;
    glide.becomes = move.promotion;
    if (taken != Chess::NO_SQUARE) {
        glide.victim = m_pieces[idx(taken)];
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

    LOG_INFO("%s plays %s", sideName(m_position.sideToMove()), Chess::Position::toUci(move).c_str());
    m_position.play(move);
    updateStatus();
}

void ChessGame::advanceGlides(float dt) {
    const float step = moveSeconds > 0.0f ? dt / moveSeconds : 1.0f;
    for (Glide& glide : m_glides) {
        glide.t = std::min(glide.t + step, 1.0f);
        // Eased along the ground, and a half sine of lift over it.
        const float     eased = glide.t * glide.t * (3.0f - 2.0f * glide.t);
        const glm::vec3 at    = glm::mix(glide.from, glide.to, eased)
            + glm::vec3(0.0f, glide.lift * std::sin(glide.t * glm::pi<float>()), 0.0f);
        if (Transform* t = scene().tryGet<Transform>(glide.piece)) t->position = at;

        if (glide.t >= 1.0f) {
            if (glide.victim) knockOff(glide.victim, glide.from);
            if (glide.becomes != PieceType::None) setMesh(glide.piece, glide.becomes);
        }
    }
    m_glides.erase(
        std::remove_if(m_glides.begin(), m_glides.end(), [](const Glide& g) { return g.t >= 1.0f; }),
        m_glides.end()
    );
}

void ChessGame::knockOff(EntityId piece, const glm::vec3& from) {
    Transform* t = scene().tryGet<Transform>(piece);
    if (!t) return;
    const Mesh* mesh = scene().tryGet<Mesh>(piece);
    PieceType   type = PieceType::Pawn;
    for (const PieceType candidate : {PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
                                      PieceType::Rook, PieceType::Queen, PieceType::King}) {
        if (mesh && mesh->mesh == resources().findByName<MeshAsset>(meshName(candidate))) type = candidate;
    }
    const Shape shape = shapeOf(type);

    // A capsule in world units round the piece's height.
    Collider body;
    body.parts[0].shape      = ColliderShape::Capsule;
    body.parts[0].radius     = shape.radius;
    body.parts[0].halfHeight = std::max(shape.height * 0.5f - shape.radius, 0.0f);
    body.parts[0].center     = {0.0f, shape.height * 0.5f, 0.0f};
    scene().add(piece, std::move(body));

    // Struck away from the attacker, up and spinning, toward the table beyond the board.
    const uint32_t  seed = piece.slot() * 7919u;
    glm::vec3       away = t->position - from;
    away.y = 0.0f;
    away   = glm::dot(away, away) > 1e-6f ? glm::normalize(away) : glm::vec3(0.0f, 0.0f, 1.0f);
    Rigidbody fall;
    fall.mass            = 0.4f;
    fall.friction        = 0.6f;
    fall.restitution     = 0.25f;
    fall.linearVelocity  = away * (9.0f + 2.0f * wobble(seed)) + glm::vec3(0.0f, 13.0f + 2.0f * wobble(seed + 1), 0.0f);
    fall.angularVelocity = {6.0f * wobble(seed + 2), 3.0f * wobble(seed + 3), 6.0f * wobble(seed + 4)};
    scene().add(piece, std::move(fall));
}

void ChessGame::applySet() {
    pieceSet = std::clamp(pieceSet, 0, static_cast<int>(PieceSet::Count) - 1);
    m_shownSet = pieceSet;
    for (const Drawn& drawn : m_drawn) {
        if (Mesh* mesh = scene().tryGet<Mesh>(drawn.entity)) {
            mesh->material = ChessLook::piece(resources(), static_cast<PieceSet>(pieceSet), drawn.side);
        }
    }
    LOG_INFO("Pieces: %s", pieceSetName(static_cast<PieceSet>(pieceSet)));
}

void ChessGame::updateCamera(float dt) {
    (void)dt;
    if (input().held(ACTION_ORBIT)) {
        const glm::vec2 drag = input().pointerDelta();
        m_yaw   -= drag.x * ORBIT_SPEED;
        m_pitch  = std::clamp(m_pitch - drag.y * ORBIT_SPEED, glm::radians(-85.0f), glm::radians(-8.0f));
    }
    m_distance = std::clamp(m_distance * std::pow(0.9f, input().wheel()), 3.5f, 16.0f);

    Transform* view = scene().tryGet<Transform>(findActiveCamera(scene()));
    if (!view) return;
    const glm::quat orbit = Math::fromYawPitch(m_yaw, m_pitch);
    view->rotation = orbit;
    view->position = glm::vec3(0.0f, BOARD_TOP, 0.0f) - Math::computeForward(orbit) * m_distance;
}

void ChessGame::updateStatus() {
    UIText* text = scene().tryGet<UIText>(m_status);
    if (!text) return;
    const Color side = m_position.sideToMove();
    switch (m_position.outcome()) {
        case Chess::Outcome::Checkmate:
            text->text = std::string("Checkmate. ") + sideName(Chess::opposite(side)) + " wins";
            break;
        case Chess::Outcome::Stalemate:            text->text = "Stalemate. A draw";                   break;
        case Chess::Outcome::FiftyMoves:           text->text = "Fifty moves without a capture. A draw"; break;
        case Chess::Outcome::Repetition:           text->text = "The same position three times. A draw"; break;
        case Chess::Outcome::InsufficientMaterial: text->text = "Neither side can mate. A draw";        break;
        case Chess::Outcome::Ongoing:
            text->text = std::string(sideName(side)) + " to move" + (m_position.inCheck(side) ? ". Check" : "");
            break;
    }
}

} // namespace Game
