#pragma once

#include <array>
#include <vector>

#include "resource/asset/material_asset.h"
#include "system/script/behavior_api.h"

namespace Game {

using namespace Vkm::Engine;

/**
 * @brief A player at the table: a floating head with googly eyes and a crown, and a ring on
 *        the square they point at.
 *
 * Whoever drives it says where the head should be and where it points; the head eases there,
 * bobs, and its pupils swing behind every move.
 */
class Avatar : public ReflectedBehavior<Avatar> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

        /// Where the head should float, and which way it looks.
        void setPose(const glm::vec3& position, const glm::quat& facing);

        /// Point at @p at on the board, or at nothing.
        void setPointer(bool on, const glm::vec3& at);

        /// Shows or hides the head; its owner looks out of it, and sees only the ring.
        void setHeadVisible(bool visible);

    public:
        glm::vec3 color     = {1.0f, 0.45f, 0.3f};  ///< The head's, and its ring's.
        bool      teamWhite = true;                  ///< A gold crown, or a dark one.
        bool      showHead  = true;                  ///< Off for the player looking out of it.

    private:
        EntityId part(const char* name, MeshHandle mesh, MaterialHandle material, const glm::vec3& at,
                      const glm::vec3& scale);

    private:
        EntityId                m_head;
        std::array<EntityId, 2> m_pupils{};
        std::vector<EntityId>   m_parts;
        EntityId                m_pointer;

        glm::vec3 m_target       = {0.0f, 3.0f, 0.0f};
        glm::quat m_facing       = {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_velocity     = {0.0f, 0.0f, 0.0f};
        glm::vec2 m_pupil        = {0.0f, 0.0f};  ///< Swing in the eye's plane, eye radii.
        glm::vec2 m_pupilSpeed   = {0.0f, 0.0f};
        glm::vec3 m_pointAt      = {0.0f, 0.0f, 0.0f};
        bool      m_pointing     = false;
        float     m_time         = 0.0f;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Avatar)
    VKM_F(color)
    VKM_F(teamWhite)
    VKM_F(showHead)
VKM_REFLECT_END()
