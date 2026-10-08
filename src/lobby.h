#pragma once

#include <string>
#include <vector>

#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_text.h"
#include "system/script/behavior_api.h"

#include "chess_game.h"

namespace Game {

using namespace Vkm::Engine;

/**
 * @brief The screen before the game: the players, a team each, with their colour, their
 *        pieces' skin and how they take; the turn's length; and the start.
 *
 * Two columns, white's and black's. A click on a player's colour, skin or take style moves
 * it on to the next; a player can swap sides or leave, and each side takes up to four. The
 * heads round the table follow every change. START begins the match once both sides have
 * someone.
 */
class Lobby : public ReflectedBehavior<Lobby> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

        /// The game the lobby sets up and starts.
        void setGame(ChessGame* game) { m_game = game; }

    private:
        void rebuild();
        void spawnTeam(Chess::Color side, float x);
        void handle(const std::string& id);
        void addPlayer(Chess::Color side);
        glm::vec3 freeColor(const glm::vec3& after, size_t self) const;
        int count(Chess::Color side) const;
        EntityId panel(EntityId parent, UIElement place, const glm::vec4& color, float corner);
        EntityId label(EntityId parent, UIElement place, const std::string& text, float size, const glm::vec4& color,
                       UIText::Align align = UIText::Align::Left);
        EntityId button(EntityId parent, UIElement place, const std::string& text, const std::string& id, float size,
                        const glm::vec4& fill, const glm::vec4& ink, float corner);

    private:
        ChessGame*               m_game = nullptr;
        std::vector<PlayerSetup> m_players;
        size_t                   m_turnChoice = 2;  ///< Into TURN_CHOICES.
        int                      m_nextNumber = 1;  ///< For the next player's name.
        EntityId                 m_canvas;
        EntityId                 m_root;            ///< Everything shown, made again on a change.
        std::vector<std::string> m_clicks;          ///< Clicks since the last update.
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Lobby)
VKM_REFLECT_END()
