// The entry points a host looks for; each is documented in system/script/module_entry.h.
#include <nlohmann/json.hpp>

#include "io/asset/asset_serializer.h"
#include "net/net_session.h"

#include "system/script/module_entry.h"

#include "chess_game.h"
#include "chess_look.h"
#include "menu.h"
#include "net_link.h"
#include "net_state.h"
#include "scenery.h"
#include "showcase.h"

namespace {

// The shared entities, which a server and its clients make alike and in the same order, so
// their slots agree: the match, then the seats.
Vkm::Engine::EntityId                          g_match;
std::array<Vkm::Engine::EntityId, Game::SEATS> g_seats{};

// The player who may start a match: the seated one who has been there longest.
void electHost(Vkm::Engine::Scene& scene) {
    Game::MatchState* state = scene.tryGet<Game::MatchState>(g_match);
    if (!state) return;
    uint16_t host = 0;
    for (const Vkm::Engine::EntityId seat : g_seats) {
        const Game::SeatState* s = scene.tryGet<Game::SeatState>(seat);
        if (s && s->player && (!host || s->player < host)) host = s->player;
    }
    state->host = host;
}

} // namespace

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::ChessGame, Game::Avatar, Game::Scenery, Game::Menu,
                                                            Game::Showcase, Game::NetLink>();
}

VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    using namespace Vkm::Engine;

    // First, before anything else: what the server replicates, alike on every end.
    g_match = scene.createEntity();
    scene.add(g_match, makeName("Match"));
    scene.add(g_match, Game::MatchState{});
    for (size_t i = 0; i < g_seats.size(); ++i) {
        g_seats[i] = scene.createEntity();
        scene.add(g_seats[i], makeName(("Seat " + std::to_string(i + 1)).c_str()));
        scene.add(g_seats[i], Game::SeatState{});
    }

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
    env.fog.density       = 0.005f;
    env.fog.height        = -7.5f;
    env.fog.heightFalloff = 0.02f;
    env.fog.anisotropy    = 0.75f;
    env.fog.albedo        = {1.0f, 0.92f, 0.82f};
    env.fog.maxDistance   = 400.0f;

    // The behaviors, on entities with nothing that replicates. What they show - the sun, the
    // camera, the table, the pieces, the menus - they make themselves, and only where there is
    // a screen: a server makes nothing past these.
    const EntityId world = scene.createEntity();
    scene.add(world, makeName("World"));
    Game::Scenery& scenery = addBehavior<Game::Scenery>(scene, world);

    const EntityId game = scene.createEntity();
    scene.add(game, makeName("Chess"));
    Game::ChessGame& chess = addBehavior<Game::ChessGame>(scene, game);

    const EntityId stand = scene.createEntity();
    scene.add(stand, makeName("Showcase"));
    Game::Showcase& showcase = addBehavior<Game::Showcase>(scene, stand);

    const EntityId link = scene.createEntity();
    scene.add(link, makeName("Net"));
    Game::NetLink& net = addBehavior<Game::NetLink>(scene, link);
    net.matchEntity  = g_match;
    net.seatEntities = g_seats;

    const EntityId menu = scene.createEntity();
    scene.add(menu, makeName("Menu"));
    Game::Menu& screens = addBehavior<Game::Menu>(scene, menu);
    screens.link(&chess, &showcase, &scenery, &net);
    net.link(&chess, &screens);
}

VKM_MODULE_ENTRY
void vkmSetupNetwork(Vkm::Engine::NetSession& session) {
    using namespace Vkm::Engine;
    Game::registerNetTypes();

    // A joining player takes the first empty seat, on the side with fewer; none once a match
    // has begun.
    session.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId id) -> EntityId {
            const Game::MatchState* state = scene.tryGet<Game::MatchState>(g_match);
            if (!state || state->phase != 0) return EntityId{};
            int sides[2] = {0, 0};
            for (const EntityId seat : g_seats) {
                const Game::SeatState* s = scene.tryGet<Game::SeatState>(seat);
                if (s && s->player) ++sides[s->side & 1];
            }
            for (const EntityId seat : g_seats) {
                Game::SeatState* s = scene.tryGet<Game::SeatState>(seat);
                if (!s || s->player) continue;
                *s        = Game::SeatState{};
                s->player = id;
                s->name   = "Player " + std::to_string(id);
                s->side   = sides[1] < sides[0] ? 1 : 0;
                s->hue    = static_cast<uint8_t>((id * 97u) % 256u);
                electHost(scene);
                return seat;
            }
            return EntityId{};
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId seat) {
            if (Game::SeatState* s = scene.tryGet<Game::SeatState>(seat)) *s = Game::SeatState{};
            electHost(scene);
        }
    );
}
