#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "chess/position.h"

// Backstab's rules over chess's: players who take turns for their colour, own what they move,
// duel over what someone else owns, and score for themselves.
namespace Chess {

struct Player {
    std::string name;
    Color       side  = Color::White;
    int         score = 0;
};

/// Why a move needs a duel before it happens.
enum class DuelKind : uint8_t {
    Teammate,  ///< Moving a piece a teammate owns.
    Capture,   ///< Taking a piece an enemy owns.
};

struct Duel {
    int      challenger = -1;
    int      defender   = -1;
    Move     move;
    DuelKind kind = DuelKind::Teammate;
};

/// What a move asked of a match did.
enum class Attempt : uint8_t { Played, Duel, Illegal };

/// The points an action scores for whoever does it.
namespace Points {
    constexpr int PAWN      = 1;
    constexpr int KNIGHT    = 3;
    constexpr int BISHOP    = 3;
    constexpr int ROOK      = 5;
    constexpr int QUEEN     = 9;
    constexpr int CHECKMATE = 15;
    constexpr int DUEL_WON  = 2;
}

/// What taking a piece of @p type is worth.
int captureValue(PieceType type);

/**
 * @brief A game of Backstab Chess: one board, several players a side.
 *
 * Turns go colour by colour and, within a colour, player by player: two a side play W1, B1,
 * W2, B2. A piece belongs to whoever last moved it. Moving a piece a teammate owns, or taking
 * a piece an enemy owns, is a duel against its owner; a piece nobody has moved is anyone's.
 * The duel's winner plays. Each turn is spent once, whoever plays it: a defender who wins
 * plays the challenger's turn, any move of their colour; a defender of a capture is of the
 * other colour, so the challenger's colour passes and the defender moves at once, without
 * spending their colour's next turn. A capture that gets its side out of check cannot be
 * dueled. Taking an enemy's piece with a teammate's is both duels, the teammate's first.
 */
class Match {
    public:
        /// The players, in the order each colour's turns go round them.
        explicit Match(std::vector<Player> players);

        const Position&            position() const { return m_position; }
        const std::vector<Player>& players() const { return m_players; }

        /// Who moves next: the player whose turn it is, or a duel's winner.
        int current() const { return m_current; }

        /// The player who owns the piece on @p square, -1 for nobody.
        int ownerOf(Square square) const { return m_owner[static_cast<size_t>(square)]; }

        /// The duel waiting to be settled, if there is one.
        const std::optional<Duel>& duel() const { return m_duel; }

        /// The duel @p move would start if the current player tried it; nothing when it would be played.
        std::optional<Duel> duelFor(const Move& move) const;

        /**
         * @brief The current player plays @p move, or challenges for it.
         *
         * @param move One of position().legalMoves().
         * @return Played, Duel (settle it with resolveDuel) or Illegal (nothing changes).
         */
        Attempt attempt(const Move& move);

        /**
         * @brief Settles the waiting duel.
         *
         * @param challengerWon Whether the challenger won it.
         */
        void resolveDuel(bool challengerWon);

        /// Whether the game is over: the position's outcome is not Ongoing.
        bool over() const { return m_position.outcome() != Outcome::Ongoing; }

    private:
        std::optional<Duel> captureDuel(int challenger, const Move& move) const;
        void play(int player, const Move& move);
        void handTo(Color side);

    private:
        Position              m_position = Position::start();
        std::vector<Player>   m_players;
        std::array<int, 64>   m_owner{};
        std::array<std::vector<int>, 2> m_rota;  ///< Each colour's players, in turn order.
        std::array<size_t, 2> m_next{};           ///< Whose turn each colour's is next.
        int                   m_current = -1;
        bool                  m_bonus   = false;  ///< The next move is a won defence's, out of turn.
        std::optional<Duel>   m_duel;
};

} // namespace Chess
