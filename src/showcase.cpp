#include "showcase.h"

#include <algorithm>
#include <cmath>

#include "ecs/component/render/mesh.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

#include "chess_look.h"
#include "world.h"

namespace Game {

namespace {

using Chess::PieceType;

// The stand, on the table's near edge in front of white's seats.
const glm::vec3 STAND      = {0.0f, 0.0f, -4.35f};
const glm::vec3 VICTIM     = STAND + glm::vec3(-1.25f, 0.0f, 0.15f);
const glm::vec3 TROPHY     = STAND + glm::vec3(1.6f, 0.0f, 0.35f);
constexpr float STAND_TOP  = 0.14f;
constexpr float TURN_SPEED = 0.35f;  ///< Radians a second.

const glm::vec3 KING_AT   = {0.0f, 0.0f, 0.0f};
const glm::vec3 KNIGHT_AT = {-0.42f, 0.0f, 0.2f};
const glm::vec3 PAWN_AT   = {0.42f, 0.0f, 0.2f};

} // namespace

glm::vec3 Showcase::eye() { return STAND + glm::vec3(0.0f, 1.55f, -3.3f); }
glm::vec3 Showcase::target() { return STAND + glm::vec3(0.0f, 0.55f, 0.0f); }

void Showcase::show(const Profile& profile, Chess::Color side) {
    hide();
    m_profile = profile;
    m_side    = side;
    ResourceManager& res = resources();

    // A brass-rimmed marble turntable.
    const MeshHandle drum = res.add(generateCylinder(0.5f, 1.0f, 64), "showcase:drum");
    const EntityId   base = spawn("Showcase Base");
    scene().add(base, Transform{STAND + glm::vec3(0.0f, 0.04f, 0.0f), {1.0f, 0.0f, 0.0f, 0.0f}, {2.1f, 0.08f, 2.1f}});
    scene().add(base, Mesh{drum, ChessLook::brass(res)});
    m_parts.push_back(base);
    const EntityId top = spawn("Showcase Top");
    scene().add(top, Transform{STAND + glm::vec3(0.0f, 0.11f, 0.0f), {1.0f, 0.0f, 0.0f, 0.0f}, {1.95f, 0.06f, 1.95f}});
    scene().add(top, Mesh{drum, ChessLook::tile(res, side == Chess::Color::White)});
    m_parts.push_back(top);

    paint();
    m_pieces = {piece(PieceType::King, KING_AT, m_skin), piece(PieceType::Knight, KNIGHT_AT, m_skin), piece(PieceType::Pawn, PAWN_AT, m_skin)};
    m_king   = m_pieces[0];

    // The pawn a take carries off: the other side's, plain wood.
    const Chess::Color other = Chess::opposite(side);
    m_victim = spawn("Showcase Victim");
    scene().add(m_victim, Transform{VICTIM, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(WORLD_SCALE)});
    scene().add(m_victim, Mesh{res.findByName<MeshAsset>(ChessLook::pieceMesh(PieceType::Pawn)), ChessLook::piece(res, PieceSet::Classic, other)});
    m_parts.push_back(m_victim);
}

void Showcase::hide() {
    for (const EntityId id : m_parts) destroy(id);
    m_parts.clear();
    m_pieces.clear();
    m_king   = {};
    m_victim = {};
    m_effect = {};
    m_take   = -1.0f;
    m_claim  = -1.0f;
}

void Showcase::refresh(const Profile& profile) {
    m_profile = profile;
    if (shown()) paint();
}

void Showcase::setSide(Chess::Color side) {
    if (side == m_side) return;
    if (shown()) show(m_profile, side);
    else m_side = side;
}

// The materials from the profile, made again in place so everything wearing them follows.
void Showcase::paint() {
    ResourceManager& res  = resources();
    const glm::vec3  took = hsv(m_profile.take.hue, 0.75f, 1.0f);
    m_skin   = ChessLook::designed(res, "showcase:skin", m_profile.pieces, m_side);
    m_flash  = ChessLook::glowing(res, "showcase:flash", hsv(m_profile.claim.hue, 0.6f, 1.0f), 0.95f, 8.0f);
    m_ripple = ChessLook::glowing(res, "showcase:ripple", took, 0.8f, 2.5f);
    m_beam   = ChessLook::glowing(res, "showcase:beam", took, 0.22f, 3.0f);
    if (m_claim < 0.0f) {
        for (const EntityId id : m_pieces) {
            if (Mesh* mesh = scene().tryGet<Mesh>(id)) mesh->material = m_skin;
        }
    }
}

EntityId Showcase::piece(PieceType type, const glm::vec3& at, MaterialHandle material) {
    ResourceManager& res = resources();
    const EntityId   id  = spawn("Showcase Piece");
    scene().add(id, Transform{STAND + glm::vec3(0.0f, STAND_TOP, 0.0f) + at, {1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3(WORLD_SCALE)});
    scene().add(id, Mesh{res.findByName<MeshAsset>(ChessLook::pieceMesh(type)), material});
    m_parts.push_back(id);
    return id;
}

void Showcase::playTake() {
    if (!shown() || m_claim >= 0.0f) return;
    m_take = 0.0f;
}

void Showcase::playClaim() {
    if (!shown() || m_take >= 0.0f) return;
    m_claim = 0.0f;
}

void Showcase::onUpdate(float dt) {
    if (!shown()) return;
    ResourceManager& res = resources();

    // The turntable turns, and the pieces with it.
    m_turn += dt * TURN_SPEED;
    const glm::quat round = glm::angleAxis(m_turn, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 offsets[] = {KING_AT, KNIGHT_AT, PAWN_AT};
    for (size_t i = 0; i < m_pieces.size(); ++i) {
        if (Transform* t = scene().tryGet<Transform>(m_pieces[i])) {
            t->position = STAND + glm::vec3(0.0f, STAND_TOP, 0.0f) + round * offsets[i];
            t->rotation = round;
            t->scale    = glm::vec3(WORLD_SCALE);
        }
    }

    // The claim, played on the king from plain wood to the skin.
    if (m_claim >= 0.0f) {
        const ClaimStyle style = m_profile.claim.style;
        m_claim = std::min(m_claim + dt * std::max(m_profile.claim.speed, 0.1f) / claimSeconds(style), 1.0f);
        const ClaimPose pose = claimPose(style, m_claim);
        if (Mesh* mesh = scene().tryGet<Mesh>(m_king)) {
            mesh->material = pose.flash ? m_flash : pose.swapped ? m_skin : ChessLook::piece(res, PieceSet::Classic, m_side);
        }
        if (Transform* t = scene().tryGet<Transform>(m_king)) {
            t->position += glm::vec3(0.0f, pose.lift, 0.0f);
            t->rotation  = round * glm::angleAxis(pose.spin, glm::vec3(0.0f, 1.0f, 0.0f));
            t->scale     = glm::vec3(WORLD_SCALE * pose.scale);
        }
        if (pose.ring > 0.0f && !m_effect) {
            m_effect = spawn("Showcase Wave");
            scene().add(m_effect, Transform{});
            Mesh wave{res.findByName<MeshAsset>("chess:ring"), m_ripple};
            wave.castShadows = false;
            scene().add(m_effect, std::move(wave));
            m_parts.push_back(m_effect);
        }
        if (Transform* t = scene().tryGet<Transform>(m_effect)) {
            const float r = pose.ring * 0.29f;
            t->position = STAND + glm::vec3(0.0f, STAND_TOP + 0.01f, 0.0f);
            t->scale    = {r, 1.0f, r};
        }
        if (m_claim >= 1.0f) {
            m_claim = -1.0f;
            if (m_effect) destroy(m_effect);
            m_parts.erase(std::remove(m_parts.begin(), m_parts.end(), m_effect), m_parts.end());
            m_effect = {};
        }
    }

    // The take: the victim pawn off to the trophy spot as the profile's take carries it, and back.
    if (m_take >= 0.0f) {
        const TakeStyle style   = m_profile.take.style;
        const TakeShape shape   = {m_profile.take.height, static_cast<int>(std::lround(m_profile.take.spins))};
        const float     seconds = takeSeconds(style) / std::max(m_profile.take.speed, 0.1f);
        m_take = std::min(m_take + dt / seconds, 1.0f);
        const TakePose pose = takePose(style, m_take, VICTIM, TROPHY, shape);
        if (Transform* t = scene().tryGet<Transform>(m_victim)) {
            t->position = pose.position;
            t->rotation = glm::angleAxis(pose.spin, glm::vec3(0.0f, 1.0f, 0.0f));
            t->scale    = pose.scale * WORLD_SCALE;
        }
        const TakeEffect effect = takeEffect(style);
        if (effect != TakeEffect::None && !m_effect) {
            m_effect = spawn("Showcase Effect");
            scene().add(m_effect, Transform{});
            Mesh drawn{res.findByName<MeshAsset>(effect == TakeEffect::Ripple ? "chess:ring" : "chess:disc"),
                       effect == TakeEffect::Ripple ? m_ripple : m_beam};
            drawn.castShadows = false;
            scene().add(m_effect, std::move(drawn));
            m_parts.push_back(m_effect);
        }
        if (Mesh* mesh = scene().tryGet<Mesh>(m_effect)) mesh->visible = pose.showEffect;
        if (Transform* t = scene().tryGet<Transform>(m_effect)) {
            t->position = pose.effectAt;
            t->scale    = pose.effectScale;
        }
        if (m_take >= 1.0f) {
            m_take = -1.0f;
            if (m_effect) destroy(m_effect);
            m_parts.erase(std::remove(m_parts.begin(), m_parts.end(), m_effect), m_parts.end());
            m_effect = {};
            if (Transform* t = scene().tryGet<Transform>(m_victim)) {
                t->position = VICTIM;
                t->rotation = {1.0f, 0.0f, 0.0f, 0.0f};
                t->scale    = glm::vec3(WORLD_SCALE);
            }
        }
    }
}

} // namespace Game
