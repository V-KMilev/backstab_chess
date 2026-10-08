#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "system/script/behavior_api.h"

#include "avatar.h"
#include "chess/match.h"
#include "chess_look.h"
#include "claims.h"
#include "profile.h"
#include "ui_kit.h"
#include "takes.h"

namespace Game {

using namespace Vkm::Engine;

/// A player as the lobby sets them up: who, which side, and how their pieces look.
struct PlayerSetup {
    std::string  name;
    Chess::Color side  = Chess::Color::White;
    glm::vec3    color = {1.0f, 1.0f, 1.0f};
    PieceLook    pieces;
    TakeLook     take;
    ClaimLook    claim;
    bool         local = false;  ///< The player at this screen.
};

/**
 * @brief A game of Backstab Chess on the table, played hot-seat: the board, its pieces, every
 *        player's seat and head, and the hand that moves for whoever's turn it is.
 *
 * Click a piece to see where it can go, then a square to move it there. A piece wears its
 * owner's skin and a ring in their colour; reaching for a teammate's piece, or taking an
 * enemy's, is a duel, settled for now by a coin. WASD flies, Space and Shift go up and down,
 * right-drag looks, F returns to the seat.
 */
class ChessGame : public ReflectedBehavior<ChessGame> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

        /// Seats the players and puts their heads round the table; until begin(), again as they change.
        void setPlayers(const std::vector<PlayerSetup>& players);

        /// Starts the match with the players set, white's first to move.
        void begin();

        /// Before the match, holds the view at @p eye looking at @p target, easing there.
        void focus(const glm::vec3& eye, const glm::vec3& target);

        /// Lets the view circle the table again.
        void unfocus() { m_focused = false; }

        bool started() const { return m_match.has_value(); }

        /// Whether the match has ended.
        bool over() const { return m_match && m_match->over(); }

        /// The players and their scores, as the match numbers them.
        std::vector<Chess::Player> results() const { return m_match ? m_match->players() : std::vector<Chess::Player>{}; }

        /// How the match ended, in words.
        std::string overReason() const;

        /// Ends any match and sets the board out again, for the next.
        void reset();

        /// Whether the local player's clicks and keys reach the game: off under a menu.
        void setInputEnabled(bool enabled) { m_inputEnabled = enabled; }

    public:
        float moveSeconds  = 0.45f;  ///< How long a piece takes from one square to the next.
        float duelSeconds  = 2.0f;   ///< How long a duel's coin spins.
        float turnSeconds  = 60.0f;  ///< A day or a night: how long a player has to move.
        bool  debugBots    = true;   ///< Play the other seats with a bot, to test alone until there is a network.
        float lookScale    = 1.0f;   ///< Times the view's turn per pixel dragged.
        float flyScale     = 1.0f;   ///< Times the view's flying speed.
        bool  showHints    = true;   ///< Mark where a chosen piece can go.

    private:
        /// A piece gliding to its square, and what it takes on the way.
        struct Glide {
            EntityId         piece;
            glm::vec3        from;
            glm::vec3        to;
            float            t       = 0.0f;
            float            lift    = 0.0f;  ///< How high it arcs, in metres; a knight jumps.
            EntityId         victim;          ///< Taken, as the taker's style says, near arrival.
            int              taker   = -1;    ///< Whose trophy the victim becomes.
            Chess::PieceType becomes = Chess::PieceType::None;
        };

        /// A taken piece on its way to its taker's trophy row, in the taker's style.
        struct Take {
            EntityId  piece;
            EntityId  effect;  ///< The style's ripple or column, if it has one.
            TakeStyle style   = TakeStyle::Float;
            TakeShape shape;
            float     seconds = 1.0f;
            float     t       = 0.0f;
            glm::vec3 from;
            glm::vec3 to;
            glm::quat facing = {1.0f, 0.0f, 0.0f, 0.0f};
        };

        /// A mark on the board: where a selected piece may go, or what a duel is over.
        struct Hint {
            EntityId      id;
            Chess::Square square = Chess::NO_SQUARE;
            float         size   = 0.0f;   ///< Across, in metres, at rest.
            float         delay  = 0.0f;   ///< Seconds before it pops up.
            bool          target = false;  ///< Swells under the pointer.
            bool          ring   = false;
        };

        /// A piece's entities: its body, a bishop's ball, and the ring in its owner's colour.
        struct DrawnPiece {
            EntityId     body;
            EntityId     top;
            EntityId     ring;
            Chess::Color side   = Chess::Color::White;
            int          owner  = -1;  ///< Whose look it shows.
            glm::quat    facing = {1.0f, 0.0f, 0.0f, 0.0f};
        };

        /// A player's place at the table, and how they look.
        struct Seat {
            Avatar*        avatar = nullptr;
            EntityId       head;    ///< The avatar's entity, to remove it by.
            glm::vec3      eye    = {0.0f, 0.0f, 0.0f};
            float          yaw    = 0.0f;
            float          pitch  = 0.0f;
            // Where the player left their view, which their next turn returns them to.
            glm::vec3      viewEye   = {0.0f, 0.0f, 0.0f};
            float          viewYaw   = 0.0f;
            float          viewPitch = 0.0f;
            glm::vec3      color  = {1.0f, 1.0f, 1.0f};
            TakeLook       take;    ///< How the pieces they take leave the board.
            ClaimLook      claim;   ///< How the pieces they claim change.
            MaterialHandle skin;    ///< Their pieces as they designed them, for their side.
            MaterialHandle ring;    ///< Under what they own, in their colour.
            MaterialHandle effect;  ///< A take's column of light.
            MaterialHandle ripple;  ///< A take's ripple, and a claim's wave.
            MaterialHandle flash;   ///< A claim's flash.
        };

        /// A piece changing into its new owner's look, once its move has landed.
        struct Claim {
            EntityId       piece;
            MaterialHandle from;    ///< What it wore before.
            int            owner = -1;
            float          t     = 0.0f;
            EntityId       effect;  ///< The wave's ring, while it spreads.
        };

        /// A player's line on the scoreboard.
        struct ScoreRow {
            EntityId tile;
            EntityId name;
            EntityId points;
            float    y = 0.0f;  ///< Where the tile is, sliding to its place in the order.
        };

    private:
        void spawnTable();
        void spawnPlayers();
        void spawnPieces();
        void spawnHud();
        EntityId spawnPiece(Chess::Piece piece, Chess::Square square);
        void setMesh(DrawnPiece& drawn, Chess::PieceType type);
        DrawnPiece* drawnOf(EntityId body);
        glm::vec3 squareCentre(Chess::Square square) const;
        Chess::Square squareUnderPointer();
        void mark(Chess::Square square, float size, MaterialHandle material, bool ring, float delay = 0.0f, bool target = false);
        void animateHints(float dt);

        void click(Chess::Square square);
        void openPicker(const std::vector<Chess::Move>& moves);
        void closePicker();
        void markLastMove(const Chess::Move& move);
        void playBot(float dt);
        void showChoices(Chess::Square square);
        void clearChoices();
        void tryMove(const Chess::Move& move);
        void startDuel(const std::string& title);
        void settleDuel(float dt);
        void openNeedle();
        void closeNeedle();
        void showHearts();
        void finishDuel(bool challengerWon);
        void showMove(const Chess::Move& move, const Chess::Position& before);
        void advanceGlides(float dt);
        void startTake(EntityId piece, int taker);
        void advanceTakes(float dt);
        glm::vec3 trophySpot(int taker);
        bool settled() const;

        void attract(float dt);
        void updateSun(float dt);
        void applySun();
        void newTurn();
        void timeOut();

        void refreshLooks();
        void advanceClaims(float dt);
        void setSkin(DrawnPiece& drawn, MaterialHandle material);
        bool gliding(EntityId piece) const;
        Chess::Square squareOf(EntityId piece) const;
        void updateCamera(float dt);
        void updateAvatars();
        void updateHud(float dt);
        void showNews(const std::string& title, const std::string& detail, float seconds, const glm::vec4& accent);
        const char* nameOf(int player) const;
        glm::vec4 seatColor(int player) const;

    private:
        std::optional<Chess::Match> m_match;
        bool                        m_inputEnabled = true;
        EntityId                    m_hud;
        std::vector<PlayerSetup>    m_setups;
        float                       m_orbit = 0.0f;  ///< Round the table, before the match.
        bool                        m_focused = false;
        glm::vec3                   m_focusEye{0.0f};
        glm::vec3                   m_focusAt{0.0f};

        std::array<EntityId, 64> m_pieces{};  ///< The body of the piece on each square.
        std::vector<DrawnPiece>  m_drawn;     ///< Every piece, captured ones too.
        std::vector<Seat>        m_seats;     ///< One per player, as the match numbers them.
        std::vector<Hint>        m_hints;
        float                    m_hintTime = 0.0f;  ///< Since the hints were shown.
        std::vector<Chess::Move> m_choices;
        std::vector<Glide>       m_glides;
        std::vector<Take>        m_takes;
        std::vector<Claim>       m_claims;
        std::vector<int>         m_trophies;  ///< How many pieces each player has taken.
        Chess::Square            m_selected = Chess::NO_SQUARE;

        // The camera is the local player's, from their seat; it travels there once, as the match
        // begins.
        int       m_viewer    = -1;
        // A debugging bot's move, chosen as its turn begins and played after a moment's thought.
        std::optional<Chess::Move> m_botMove;
        float                      m_botTime = 0.0f;
        glm::vec3 m_eye       = {0.0f, 4.6f, -7.0f};
        float     m_yaw       = 0.0f;
        float     m_pitch     = 0.0f;
        glm::vec3 m_fromEye   = {0.0f, 4.6f, -7.0f};
        float     m_fromYaw   = 0.0f;
        float     m_fromPitch = 0.0f;
        float     m_travel    = 1.0f;  ///< 0..1 along the way to the viewer's seat.

        float m_duelTime = -1.0f;  ///< Seconds into the waiting duel; below zero while there is none.

        /// A duel's needle game: each duelist stops a swinging needle, and the nearer the middle
        /// wins. The local player plays theirs; a bot stops at a moment it picks.
        struct NeedleDuel {
            std::array<int, 2>      players = {-1, -1};       ///< The challenger, the defender.
            std::array<float, 2>    stopAt  = {-1.0f, -1.0f}; ///< When each stopped; below zero, not yet.
            std::array<float, 2>    botAt   = {-1.0f, -1.0f}; ///< When a bot will stop.
            EntityId                panel;
            std::array<EntityId, 2> needle{};
            std::array<EntityId, 2> result{};
        };
        NeedleDuel m_needle;

        Ui::Kit                  m_kit;         ///< The match's own widgets: the promotion picker.
        EntityId                 m_picker;      ///< Asking what a pawn becomes, while it is open.
        std::array<EntityId, 2>  m_lastMarks{}; ///< The last move's squares.

        /// The hearts over each king, a life each: white's, then black's.
        std::array<std::vector<EntityId>, 2> m_hearts;

        // The sun is the clock: white moves by day, from sunrise to sunset, and black by night,
        // from sunset to sunrise. An angle round the sky, 0 at sunrise, that only grows.
        float    m_sun        = 0.0f;
        float    m_turnStart  = 0.0f;  ///< Where this turn's sun began.
        float    m_turnEnd    = 0.0f;  ///< Where it ends it.
        float    m_lapseFrom  = 0.0f;  ///< Where the sun was when it raced on to this turn.
        float    m_lapse      = 1.0f;  ///< 0..1 through that time-lapse.
        float    m_lapseTime  = 1.0f;  ///< Its length, in seconds: a whole night passes slower.
        EntityId m_lamp;

        // The turn card: a disc, half a sun's face and half a moon's, painted afresh as it turns
        // half round through each turn; the seconds left in its hub, and beside it whose move
        // it is.
        TextureHandle         m_dialFace;
        float                 m_dialPainted = -1000.0f;  ///< The turn it was last painted at.
        EntityId              m_clockSeconds;
        EntityId              m_turnName;
        EntityId              m_turnDetail;
        float                 m_hudTime = 0.0f;  ///< For the last seconds' pulse.
        // News - a duel, its outcome, a sunset - takes over the turn card for a moment, in its
        // own colour.
        EntityId              m_card;
        std::string           m_newsTitle;
        std::string           m_newsDetail;
        float                 m_newsTime   = 0.0f;  ///< Seconds the news has left.
        float                 m_newsLength = 0.0f;  ///< Seconds it is shown for.
        glm::vec4             m_newsAccent = {1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<ScoreRow> m_scoreRows;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::ChessGame)
    VKM_F(moveSeconds)
    VKM_F(duelSeconds)
    VKM_F(turnSeconds)
    VKM_F(debugBots)
VKM_REFLECT_END()
