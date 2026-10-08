#pragma once

#include <vector>

#include "resource/asset/material_asset.h"
#include "system/script/behavior_api.h"

namespace Game {

using namespace Vkm::Engine;

/**
 * @brief The world round the game: the table, standing in a sea strewn with chess pieces and
 *        wrecked boards, which bob and rock and swirl slowly round it, some adrift in the air.
 *
 * Laid out from a fixed seed, so every game has the same world. Nothing comes near enough the
 * table to pull the eye off the board, and the lanterns round it light the night.
 */
class Scenery : public ReflectedBehavior<Scenery> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    private:
        /// Something afloat or adrift, and how it moves: everything here is still at zero.
        struct Drifter {
            EntityId  id;
            glm::vec3 at;
            glm::quat facing   = {1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 spinAxis = {0.0f, 1.0f, 0.0f};
            float     spin     = 0.0f;  ///< Radians a second about spinAxis, without end.
            glm::vec3 rockAxis = {1.0f, 0.0f, 0.0f};
            float     rock     = 0.0f;  ///< Radians it rocks either way about rockAxis.
            float     bob      = 0.0f;  ///< Metres up and down.
            float     rate     = 0.5f;  ///< Of the bob and the rock, radians a second.
            float     phase    = 0.0f;
            float     swirl    = 0.0f;  ///< Radians a second it circles the table.
        };

    private:
        void spawnTable();
        void spawnLanterns();
        void spawnSea();
        void spawnFlotsam();
        EntityId place(const char* name, MeshHandle mesh, MaterialHandle material, const glm::vec3& at,
                       const glm::quat& facing, const glm::vec3& scale, bool shadows = true);

    private:
        std::vector<Drifter> m_drifters;
        EntityId             m_sea;
        float                m_time = 0.0f;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Scenery)
VKM_REFLECT_END()
