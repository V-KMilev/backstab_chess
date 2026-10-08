#pragma once

#include <string>
#include <vector>

#include "resource/asset/texture_asset.h"
#include "system/script/behavior_api.h"

#include "chess_game.h"
#include "profile.h"
#include "ui_kit.h"

namespace Game {

using namespace Vkm::Engine;

class Scenery;
class Showcase;

/**
 * @brief Every screen round the game: the main menu; the teams before a match; customizing
 *        your pieces, your take and your claim against a live preview; the settings, graphics,
 *        game and controls; and in the match, pause and game over.
 *
 * Holds this machine's profile and settings, applies the settings to the engine and the game as
 * they change, and keeps both in the config folder.
 */
class Menu : public ReflectedBehavior<Menu> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

        /// The game the menu sets up and starts, the stand customizing shows on, the world.
        void link(ChessGame* game, Showcase* showcase, Scenery* scenery);

    private:
        enum class Screen { Main, Play, Customize, Settings, Paused, Over, Playing };

        void go(Screen screen);
        void build();
        void buildMain();
        void buildPlay();
        void buildCustomize();
        void buildSettings();
        void buildPaused();
        void buildOver();
        EntityId backdrop(float alpha);
        EntityId card(UIElement place, const std::string& title);
        void tabs(EntityId parent, float x, float y, const std::vector<std::string>& names, int& active);

        void apply();
        void startMatch();
        void addBot(Chess::Color side);
        void syncYou();
        int count(Chess::Color side) const;

    private:
        ChessGame* m_game     = nullptr;
        Showcase*  m_showcase = nullptr;
        Scenery*   m_scenery  = nullptr;

        Ui::Kit       m_kit;
        Screen        m_screen = Screen::Main;
        Screen        m_return = Screen::Main;  ///< Where settings go back to.
        bool          m_rebuild = false;        ///< Build the screen again next frame.
        EntityId      m_canvas;
        EntityId      m_root;
        TextureHandle m_hue;                    ///< The rainbow a colour slider runs along.

        std::vector<PlayerSetup> m_players;     ///< You first, then the bots.
        int   m_nextBot     = 1;
        int   m_customTab   = 0;
        int   m_settingsTab = 0;
        bool  m_previewBlack = false;
        bool  m_fullscreen  = false;            ///< As last applied.
        int   m_seaDetail   = -1;               ///< As last applied.
        float m_overDelay   = 0.0f;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Menu)
VKM_REFLECT_END()
