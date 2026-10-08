#pragma once

#include <vector>

#include "resource/asset/material_asset.h"
#include "system/script/behavior_api.h"

namespace Game {

using namespace Vkm::Engine;

/**
 * @brief The world round the game: the table, and a broken chessboard of a plain to the
 *        mountains, strewn with colossal pieces and torn-up squares, some of them adrift in
 *        the air.
 *
 * Laid out from a fixed seed, so every game has the same world. What floats drifts, bobs and
 * turns slowly, and nothing comes near enough the table to pull the eye off the board.
 */
class Scenery : public ReflectedBehavior<Scenery> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    private:
        /// Something adrift: where it hangs, and how it bobs and turns.
        struct Drifter {
            EntityId  id;
            glm::vec3 at;
            glm::quat facing = {1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 axis   = {0.0f, 1.0f, 0.0f};
            float     spin   = 0.0f;  ///< Radians a second about axis.
            float     bob    = 0.0f;  ///< Metres up and down.
            float     phase  = 0.0f;
        };

    private:
        void spawnTable();
        void spawnPlain();
        void spawnColossi();
        void spawnShards();
        EntityId place(const char* name, MeshHandle mesh, MaterialHandle material, const glm::vec3& at,
                       const glm::quat& facing, const glm::vec3& scale, bool shadows = true);

    private:
        std::vector<Drifter> m_drifters;
        float                m_time = 0.0f;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Scenery)
VKM_REFLECT_END()
