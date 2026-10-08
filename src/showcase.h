#pragma once

#include <vector>

#include "resource/asset/material_asset.h"
#include "system/script/behavior_api.h"

#include "chess/position.h"
#include "claims.h"
#include "profile.h"
#include "takes.h"

namespace Game {

using namespace Vkm::Engine;

/**
 * @brief A turntable on the table's near edge, where the customize screen shows a player's
 *        design: their king, knight and pawn in their skin, a pawn for their take to carry
 *        off, and their claim played on the king.
 *
 * Built when shown and gone when hidden; what it shows follows the profile it is given.
 */
class Showcase : public ReflectedBehavior<Showcase> {
    public:
        void onUpdate(float dt) override;

        /// Builds the stand, showing @p profile's design for @p side.
        void show(const Profile& profile, Chess::Color side);

        /// Takes the stand away.
        void hide();

        /// Shows @p profile's design afresh, on the side it was shown for.
        void refresh(const Profile& profile);

        /// The side the pieces are shown as: a design is lighter for white, darker for black.
        void setSide(Chess::Color side);

        void playTake();
        void playClaim();

        bool shown() const { return !m_parts.empty(); }

        /// Where the view looks at the stand from, and at.
        static glm::vec3 eye();
        static glm::vec3 target();

    private:
        EntityId piece(Chess::PieceType type, const glm::vec3& at, MaterialHandle material);
        void paint();

    private:
        Profile               m_profile;
        Chess::Color          m_side = Chess::Color::White;
        std::vector<EntityId> m_parts;
        std::vector<EntityId> m_pieces;  ///< On the turntable, turning with it.
        EntityId              m_king;
        EntityId              m_victim;
        EntityId              m_effect;
        MaterialHandle        m_skin;
        MaterialHandle        m_flash;
        MaterialHandle        m_ripple;
        MaterialHandle        m_beam;
        float                 m_turn  = 0.0f;
        float                 m_take  = -1.0f;  ///< 0..1 through a take shown; below zero, none.
        float                 m_claim = -1.0f;  ///< 0..1 through a claim shown.
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Showcase)
VKM_REFLECT_END()
