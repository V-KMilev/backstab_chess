// The entry points a host looks for; each is documented in system/script/module_entry.h.
#include <nlohmann/json.hpp>

#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "io/asset/asset_serializer.h"

#include "system/script/module_entry.h"

#include "chess_game.h"
#include "chess_look.h"
#include "scenery.h"

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::ChessGame, Game::Avatar, Game::Scenery>();
}

VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    using namespace Vkm::Engine;

    // The chess set, cooked from library/ (tools/register_assets.py), loaded by name.
    nlohmann::json assets;
    for (const char* mesh : {"chess:board", "chess:pawn", "chess:knight", "chess:bishop", "chess:bishop_top",
                             "chess:rook", "chess:queen", "chess:king"}) {
        assets["meshes"].push_back({{"name", mesh}});
    }
    for (const char* part : {"board", "pieces_white", "pieces_black"}) {
        for (const char* map : {"diff", "nor_gl", "arm"}) {
            const std::string ref = std::string("assets/chess_set/textures/chess_set_") + part + "_" + map + "_2k.jpg";
            assets["textures"].push_back({{"name", ref}});
        }
    }
    AssetSerializer::loadAssets(assets, resources);
    Game::ChessLook::build(resources);

    // A flat, wet plain under a haze that shows the light. The game moves the sun: day for
    // white's turns, night for black's, so night is lit enough to play by.
    Environment& env = scene.environment();
    env.sky.lightIntensity       = 4.0f;
    env.night.radiance           = {0.012f, 0.018f, 0.04f};
    env.night.moonlightIntensity = 0.45f;
    env.sky.mie                  = 6.0f;
    env.fog.enabled       = true;
    env.fog.density       = 0.0025f;
    env.fog.height        = -7.5f;
    env.fog.heightFalloff = 0.02f;
    env.fog.anisotropy    = 0.75f;
    env.fog.albedo        = {1.0f, 0.92f, 0.82f};
    env.fog.maxDistance   = 400.0f;

    const EntityId sun = scene.createEntity();
    scene.add(sun, makeName("Sun"));
    scene.add(sun, Transform{});
    Light sunLight{};
    sunLight.type           = LightType::Directional;
    sunLight.shadowDistance = 40.0f;
    scene.add(sun, sunLight);


    const EntityId camera = scene.createEntity();
    scene.add(camera, makeName("Camera"));
    scene.add(camera, Transform{});
    Camera lens{};
    lens.fovY = glm::radians(50.0f);
    scene.add(camera, lens);

    const EntityId world = scene.createEntity();
    scene.add(world, makeName("World"));
    addBehavior<Game::Scenery>(scene, world);

    const EntityId game = scene.createEntity();
    scene.add(game, makeName("Chess"));
    addBehavior<Game::ChessGame>(scene, game);
}
