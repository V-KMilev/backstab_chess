#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "system/script/behavior_api.h"

#include "avatar.h"
#include "chess/match.h"
#include "chess_look.h"
#include "takes.h"

namespace Game {

using namespace Vkm::Engine;

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

    public:
        int   whitePlayers = 2;
        int   blackPlayers = 2;
        float moveSeconds  = 0.45f;  ///< How long a piece takes from one square to the next.
        float duelSeconds  = 2.0f;   ///< How long a duel's coin spins.
        float turnSeconds  = 60.0f;  ///< A day or a night: how long a player has to move.

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
            TakeStyle style = TakeStyle::Float;
            float     t     = 0.0f;
            glm::vec3 from;
            glm::vec3 to;
            glm::quat facing = {1.0f, 0.0f, 0.0f, 0.0f};
        };

        /// A piece's entities: its body, a bishop's ball, and the ring in its owner's colour.
        struct DrawnPiece {
            EntityId     body;
            EntityId     top;
            EntityId     ring;
            Chess::Color side = Chess::Color::White;
        };

        /// A player's place at the table, and how they look.
        struct Seat {
            Avatar*        avatar = nullptr;
            glm::vec3      eye    = {0.0f, 0.0f, 0.0f};
            float          yaw    = 0.0f;
            float          pitch  = 0.0f;
            // Where the player left their view, which their next turn returns them to.
            glm::vec3      viewEye   = {0.0f, 0.0f, 0.0f};
            float          viewYaw   = 0.0f;
            float          viewPitch = 0.0f;
            glm::vec3      color  = {1.0f, 1.0f, 1.0f};
            PieceSet       skin   = PieceSet::Classic;
            TakeStyle      takes  = TakeStyle::Float;  ///< How the pieces they take leave the board.
            MaterialHandle ring;
            MaterialHandle beam;
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
        void mark(Chess::Square square, float size, MaterialHandle material);

        void click(Chess::Square square);
        void showChoices(Chess::Square square);
        void clearChoices();
        void tryMove(const Chess::Move& move);
        void startDuel(const std::string& title);
        void settleDuel(float dt);
        void finishDuel(bool challengerWon);
        void showMove(const Chess::Move& move, const Chess::Position& before);
        void advanceGlides(float dt);
        void startTake(EntityId piece, int taker);
        void advanceTakes(float dt);
        glm::vec3 trophySpot(int taker);
        bool settled() const;

        void updateSun(float dt);
        void newTurn();
        void timeOut();

        void refreshLooks();
        void updateCamera(float dt);
        void updateAvatars();
        void updateHud(float dt);
        void showNews(const std::string& title, const std::string& detail, float seconds, const glm::vec4& accent);
        const char* nameOf(int player) const;
        glm::vec4 seatColor(int player) const;

    private:
        std::optional<Chess::Match> m_match;

        std::array<EntityId, 64> m_pieces{};  ///< The body of the piece on each square.
        std::vector<DrawnPiece>  m_drawn;     ///< Every piece, captured ones too.
        std::vector<Seat>        m_seats;     ///< One per player, as the match numbers them.
        std::vector<EntityId>    m_hints;
        std::vector<Chess::Move> m_choices;
        std::vector<Glide>       m_glides;
        std::vector<Take>        m_takes;
        std::vector<int>         m_trophies;  ///< How many pieces each player has taken.
        Chess::Square            m_selected = Chess::NO_SQUARE;

        // The camera is whoever's turn it is, and travels to their seat when that changes.
        int       m_viewer    = -1;
        glm::vec3 m_eye       = {0.0f, 4.6f, -7.0f};
        float     m_yaw       = 0.0f;
        float     m_pitch     = 0.0f;
        glm::vec3 m_fromEye   = {0.0f, 4.6f, -7.0f};
        float     m_fromYaw   = 0.0f;
        float     m_fromPitch = 0.0f;
        float     m_travel    = 1.0f;  ///< 0..1 along the way to the viewer's seat.

        float m_duelTime = -1.0f;  ///< Seconds into the waiting duel; below zero while there is none.

        // The sun is the clock: white moves by day, from sunrise to sunset, and black by night,
        // from sunset to sunrise. An angle round the sky, 0 at sunrise, that only grows.
        float    m_sun        = 0.0f;
        float    m_turnStart  = 0.0f;  ///< Where this turn's sun began.
        float    m_turnEnd    = 0.0f;  ///< Where it ends it.
        float    m_blink      = 1.0f;  ///< 0..1 through the blink the sun jumps to this turn in.
        EntityId m_fade;               ///< What the screen dips to for it.
        EntityId m_lamp;

        /// A mark on the dial's faces, where it sits with the sun's half on top.
        struct DialMark {
            EntityId  id;
            glm::vec2 at;
            float     size = 0.0f;
        };

        // The turn card: a disc, half a sun's face and half a moon's, that turns half round
        // through each turn; the seconds left in its hub, and beside it whose move it is.
        std::vector<EntityId> m_dialStrips;  ///< Two a row: the left part and the right.
        std::vector<DialMark> m_dialMarks;
        EntityId              m_dialRim;
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
    VKM_F(whitePlayers)
    VKM_F(blackPlayers)
    VKM_F(moveSeconds)
    VKM_F(duelSeconds)
    VKM_F(turnSeconds)
VKM_REFLECT_END()
