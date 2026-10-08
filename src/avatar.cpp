#include "avatar.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ecs/component/render/mesh.h"
#include "resource/asset/mesh_asset.h"
#include "resource/generate/mesh_generators.h"

namespace Game {

namespace {

constexpr float HEAD = 0.7f;        ///< The head's diameter, in metres.
constexpr float FOLLOW = 7.0f;      ///< How fast the head closes on where it should be, per second.
constexpr float BOB = 0.06f;        ///< How far it bobs, up and down.

// The shared shapes and the materials every head has; a head's own colour is made per avatar.
MeshHandle shape(ResourceManager& res, const char* name, MeshAsset (*make)()) {
    if (const MeshHandle found = res.findByName<MeshAsset>(name)) return found;
    return res.add(make(), name);
}

MeshAsset sphere() { return generateSphere(32, 16); }
MeshAsset spike() { return generateCone(0.5f, 1.0f, 12); }
MeshAsset band() { return generateCylinder(0.5f, 1.0f, 32); }

MaterialHandle plastic(ResourceManager& res, const std::string& name, const glm::vec3& color, float roughness) {
    if (const MaterialHandle found = res.findByName<MaterialAsset>(name)) return found;
    MaterialAsset m;
    m.albedo             = {color, 1.0f};
    m.roughness          = roughness;
    m.clearcoat          = 1.0f;
    m.clearcoatRoughness = 0.05f;
    return res.add(std::move(m), name);
}

} // namespace

EntityId Avatar::part(const char* name, MeshHandle mesh, MaterialHandle material, const glm::vec3& at,
                      const glm::vec3& scale) {
    const EntityId id = spawn(name, m_head);
    scene().add(id, Transform{at, {1.0f, 0.0f, 0.0f, 0.0f}, scale});
    scene().add(id, Mesh{mesh, material});
    m_parts.push_back(id);
    return id;
}

void Avatar::onStart() {
    ResourceManager& res = resources();
    const MeshHandle ball = shape(res, "avatar:sphere", sphere);
    const MeshHandle cone = shape(res, "avatar:spike", spike);
    const MeshHandle ring = shape(res, "avatar:band", band);

    const std::string me   = "avatar:" + std::to_string(entity().slot());
    const MaterialHandle skin  = plastic(res, me + ":skin", color, 0.35f);
    const MaterialHandle white = plastic(res, "avatar:eye", {0.95f, 0.95f, 0.93f}, 0.2f);
    const MaterialHandle black = plastic(res, "avatar:pupil", {0.02f, 0.02f, 0.02f}, 0.1f);

    MaterialAsset metal;
    metal.albedo    = teamWhite ? glm::vec4(1.0f, 0.77f, 0.34f, 1.0f) : glm::vec4(0.18f, 0.18f, 0.22f, 1.0f);
    metal.metallic  = 1.0f;
    metal.roughness = 0.2f;
    const MaterialHandle crown = res.add(std::move(metal), me + ":crown");

    // The head's root moves; every part hangs off it. Forward is -Z, so the face is there.
    m_head = spawn("Head", entity());
    scene().add(m_head, Transform{});
    part("Skull", ball, skin, {0.0f, 0.0f, 0.0f}, glm::vec3(HEAD));
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -0.15f : 0.15f;
        part("Eye", ball, white, {x, 0.08f, -0.24f}, glm::vec3(0.26f));
        m_pupils[static_cast<size_t>(side)] = part("Pupil", ball, black, {x, 0.08f, -0.36f}, glm::vec3(0.11f));
    }
    // A crown: a band and five spikes round it, tipped a little to one side.
    part("Band", ring, crown, {0.05f, 0.33f, 0.0f}, {0.42f, 0.1f, 0.42f});
    for (int i = 0; i < 5; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / 5.0f;
        part("Spike", cone, crown, {0.05f + 0.17f * std::cos(a), 0.45f, 0.17f * std::sin(a)}, {0.1f, 0.18f, 0.1f});
    }

    // The ring on the board, flat and glowing in the head's colour.
    MaterialAsset glow;
    glow.type             = MaterialType::Transparent;
    glow.albedo           = {color, 0.3f};
    glow.emission         = color;
    glow.emissiveStrength = 0.8f;
    const MaterialHandle glowing = res.add(std::move(glow), me + ":pointer");
    m_pointer = spawn("Pointer");
    scene().add(m_pointer, Transform{{0.0f, -10.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.42f, 0.008f, 0.42f}});
    Mesh drawn{ring, glowing};
    drawn.castShadows = false;
    drawn.visible     = false;
    scene().add(m_pointer, std::move(drawn));

    if (Transform* root = tryGet<Transform>()) m_target = root->position;
    setHeadVisible(showHead);
}

// The pointer ring is not the head's child, so it goes with it by hand.
void Avatar::onDestroy() { destroy(m_pointer); }

void Avatar::setPose(const glm::vec3& position, const glm::quat& facing) {
    m_target = position;
    m_facing = facing;
}

void Avatar::setPointer(bool on, const glm::vec3& at) {
    m_pointing = on;
    m_pointAt  = at;
}

void Avatar::setHeadVisible(bool visible) {
    for (const EntityId id : m_parts) {
        if (Mesh* mesh = scene().tryGet<Mesh>(id)) mesh->visible = visible;
    }
}

void Avatar::onUpdate(float dt) {
    m_time += dt;
    Transform* root = tryGet<Transform>();
    if (!root || dt <= 0.0f) return;

    // The body eases toward its target; what it was asked to do it does a moment late.
    const glm::vec3 before = root->position;
    const float     k      = 1.0f - std::exp(-FOLLOW * dt);
    root->position = glm::mix(root->position, m_target, k);
    root->rotation = glm::slerp(root->rotation, m_facing, k);
    const glm::vec3 velocity     = (root->position - before) / dt;
    const glm::vec3 acceleration = (velocity - m_velocity) / dt;
    m_velocity = velocity;

    // The head bobs on the body, and the pupils swing against how it accelerates: a spring
    // with too little damping, so they wobble after it stops.
    if (Transform* head = scene().tryGet<Transform>(m_head)) {
        head->position = {0.0f, BOB * std::sin(m_time * 2.1f), 0.0f};
        head->rotation = glm::angleAxis(0.08f * std::sin(m_time * 1.3f), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    const glm::vec3 local = glm::inverse(root->rotation) * acceleration;
    const glm::vec2 push  = glm::clamp(glm::vec2(-local.x, -local.y) * 0.02f, glm::vec2(-1.0f), glm::vec2(1.0f));
    m_pupilSpeed += (push * 40.0f - m_pupil * 90.0f - m_pupilSpeed * 4.0f) * dt;
    m_pupil       = glm::clamp(m_pupil + m_pupilSpeed * dt, glm::vec2(-1.0f), glm::vec2(1.0f));
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -0.15f : 0.15f;
        if (Transform* pupil = scene().tryGet<Transform>(m_pupils[static_cast<size_t>(side)])) {
            pupil->position = {x + 0.06f * m_pupil.x, 0.08f + 0.06f * m_pupil.y, -0.36f};
        }
    }

    // The ring sits on the board where the head points, and turns slowly.
    if (Mesh* ring = scene().tryGet<Mesh>(m_pointer)) ring->visible = m_pointing;
    if (Transform* ring = scene().tryGet<Transform>(m_pointer)) {
        ring->position = m_pointAt + glm::vec3(0.0f, 0.01f, 0.0f);
        const float pulse = 0.42f * (1.0f + 0.08f * std::sin(m_time * 6.0f));
        ring->scale = {pulse, 0.008f, pulse};
    }
}

} // namespace Game
