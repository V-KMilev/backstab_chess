#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The rules of chess, with nothing of the engine in them: a position, the legal moves from
// it, and what playing one leaves. The board, the server and the tests all read the same
// rules from here.
namespace Chess {

enum class Color : uint8_t { White, Black };

enum class PieceType : uint8_t { None, Pawn, Knight, Bishop, Rook, Queen, King };

struct Piece {
    PieceType type  = PieceType::None;
    Color     color = Color::White;

    bool empty() const { return type == PieceType::None; }
    bool operator==(const Piece& other) const { return type == other.type && color == other.color; }
    bool operator!=(const Piece& other) const { return !(*this == other); }
};

/// 0..63: a1 is 0, h1 is 7, a8 is 56. File is square % 8, rank square / 8.
using Square = int;

constexpr Square NO_SQUARE = -1;

constexpr int    fileOf(Square s) { return s & 7; }
constexpr int    rankOf(Square s) { return s >> 3; }
constexpr Square squareAt(int file, int rank) { return rank * 8 + file; }

constexpr Color opposite(Color c) { return c == Color::White ? Color::Black : Color::White; }

/// What a move does besides carrying a piece from one square to another.
enum class MoveKind : uint8_t { Quiet, Capture, DoublePush, EnPassant, CastleKing, CastleQueen };

struct Move {
    Square    from      = NO_SQUARE;
    Square    to        = NO_SQUARE;
    MoveKind  kind      = MoveKind::Quiet;
    PieceType promotion = PieceType::None;  ///< What a pawn reaching the last rank becomes.

    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promotion == other.promotion;
    }
    bool operator!=(const Move& other) const { return !(*this == other); }
};

/// Where a game stands after the last move.
enum class Outcome : uint8_t { Ongoing, Checkmate, Stalemate, FiftyMoves, Repetition, InsufficientMaterial };

/**
 * @brief A chess position: the pieces, whose move it is, and the rights the past left.
 *
 * Holds the keys of every position since the last capture or pawn move, so it can tell a
 * threefold repetition; play() is the only way forward.
 */
class Position {
    public:
        /// The opening position.
        static Position start();

        /**
         * @brief A position read from Forsyth-Edwards notation.
         *
         * @param fen Six space-separated fields; the move counters may be left out.
         * @return The position, or nothing when the text is not one.
         */
        static std::optional<Position> fromFen(std::string_view fen);

        /// This position in Forsyth-Edwards notation.
        std::string fen() const;

        Piece at(Square s) const { return m_board[static_cast<size_t>(s)]; }
        Color sideToMove() const { return m_side; }
        Square enPassantSquare() const { return m_enPassant; }

        /// Every move the side to move may play.
        std::vector<Move> legalMoves() const;

        /**
         * @brief The legal move from @p from to @p to, if there is one.
         *
         * @param promotion For a pawn reaching the last rank; a queen when left out.
         */
        std::optional<Move> findMove(Square from, Square to, PieceType promotion = PieceType::Queen) const;

        /// Plays @p move, which must be one legalMoves() returned.
        void play(const Move& move);

        /**
         * @brief Hands the move to the other side without one: Backstab's lost duels do this.
         *
         * Only while the side to move is not in check, or the other side could take its king.
         *
         * @return False, changing nothing, when the side to move is in check.
         */
        bool pass();

        /// Where the piece @p move takes stands: beside its arrival for en passant; NO_SQUARE for none.
        Square takenBy(const Move& move) const;

        /// Whether @p side's king is attacked.
        bool inCheck(Color side) const;

        /// Whether the game is over, and how; Ongoing while the side to move has a move to make.
        Outcome outcome() const;

        /// @p move as long algebraic text, e.g. "e2e4" or "e7e8q".
        static std::string toUci(const Move& move);

    private:
        bool attacked(Square s, Color by) const;
        Square kingOf(Color side) const;
        void pseudoLegalMoves(std::vector<Move>& out) const;
        uint64_t key() const;

    private:
        std::array<Piece, 64> m_board{};
        Color    m_side      = Color::White;
        uint8_t  m_castling  = 0;  ///< Bits: white king side, white queen side, black king, black queen.
        Square   m_enPassant = NO_SQUARE;
        int      m_halfmoves = 0;  ///< Since the last capture or pawn move.
        int      m_fullmoves = 1;
        std::vector<uint64_t> m_history;  ///< Keys since the last capture or pawn move, this one last.
};

} // namespace Chess
