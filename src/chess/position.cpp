#include "chess/position.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace Chess {

namespace {

constexpr uint8_t WHITE_KING_SIDE  = 1;
constexpr uint8_t WHITE_QUEEN_SIDE = 2;
constexpr uint8_t BLACK_KING_SIDE  = 4;
constexpr uint8_t BLACK_QUEEN_SIDE = 8;

constexpr int KNIGHT_STEPS[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KING_STEPS[8][2]   = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int ROOK_RAYS[4][2]    = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int BISHOP_RAYS[4][2]  = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

constexpr PieceType PROMOTIONS[4] = {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};

bool onBoard(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }

int forward(Color c) { return c == Color::White ? 1 : -1; }

char pieceLetter(Piece p) {
    static const char LETTERS[] = " pnbrqk";
    const char c = LETTERS[static_cast<int>(p.type)];
    return p.color == Color::White ? static_cast<char>(std::toupper(c)) : c;
}

Piece pieceFromLetter(char c) {
    Piece p;
    p.color = std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black;
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'p': p.type = PieceType::Pawn;   break;
        case 'n': p.type = PieceType::Knight; break;
        case 'b': p.type = PieceType::Bishop; break;
        case 'r': p.type = PieceType::Rook;   break;
        case 'q': p.type = PieceType::Queen;  break;
        case 'k': p.type = PieceType::King;   break;
        default:  break;
    }
    return p;
}

// The castling right a move from or onto @p s takes away: a rook's home square.
uint8_t rightsOfCorner(Square s) {
    switch (s) {
        case 0:  return WHITE_QUEEN_SIDE;
        case 7:  return WHITE_KING_SIDE;
        case 56: return BLACK_QUEEN_SIDE;
        case 63: return BLACK_KING_SIDE;
        default: return 0;
    }
}

} // namespace

Position Position::start() {
    return *fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

std::optional<Position> Position::fromFen(std::string_view fen) {
    std::istringstream in{std::string(fen)};
    std::string placement, side, castling, enPassant;
    int halfmoves = 0;
    int fullmoves = 1;
    if (!(in >> placement >> side >> castling >> enPassant)) return std::nullopt;
    in >> halfmoves >> fullmoves;

    Position p;
    int file = 0;
    int rank = 7;
    for (const char c : placement) {
        if (c == '/') {
            if (file != 8) return std::nullopt;
            file = 0;
            --rank;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            file += c - '0';
        } else {
            const Piece piece = pieceFromLetter(c);
            if (piece.empty() || !onBoard(file, rank)) return std::nullopt;
            p.m_board[static_cast<size_t>(squareAt(file, rank))] = piece;
            ++file;
        }
        if (file > 8) return std::nullopt;
    }
    if (rank != 0 || file != 8) return std::nullopt;

    if (side != "w" && side != "b") return std::nullopt;
    p.m_side = side == "w" ? Color::White : Color::Black;

    for (const char c : castling) {
        switch (c) {
            case 'K': p.m_castling |= WHITE_KING_SIDE;  break;
            case 'Q': p.m_castling |= WHITE_QUEEN_SIDE; break;
            case 'k': p.m_castling |= BLACK_KING_SIDE;  break;
            case 'q': p.m_castling |= BLACK_QUEEN_SIDE; break;
            case '-': break;
            default:  return std::nullopt;
        }
    }

    if (enPassant != "-") {
        if (enPassant.size() != 2) return std::nullopt;
        const int f = enPassant[0] - 'a';
        const int r = enPassant[1] - '1';
        if (!onBoard(f, r)) return std::nullopt;
        p.m_enPassant = squareAt(f, r);
    }

    p.m_halfmoves = halfmoves;
    p.m_fullmoves = std::max(fullmoves, 1);
    if (p.kingOf(Color::White) == NO_SQUARE || p.kingOf(Color::Black) == NO_SQUARE) return std::nullopt;
    p.m_history.push_back(p.key());
    return p;
}

std::string Position::fen() const {
    std::string out;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = at(squareAt(file, rank));
            if (piece.empty()) {
                ++empty;
                continue;
            }
            if (empty) out += static_cast<char>('0' + empty);
            empty = 0;
            out += pieceLetter(piece);
        }
        if (empty) out += static_cast<char>('0' + empty);
        if (rank > 0) out += '/';
    }
    out += m_side == Color::White ? " w " : " b ";
    std::string rights;
    if (m_castling & WHITE_KING_SIDE)  rights += 'K';
    if (m_castling & WHITE_QUEEN_SIDE) rights += 'Q';
    if (m_castling & BLACK_KING_SIDE)  rights += 'k';
    if (m_castling & BLACK_QUEEN_SIDE) rights += 'q';
    out += rights.empty() ? "-" : rights;
    out += ' ';
    if (m_enPassant == NO_SQUARE) {
        out += '-';
    } else {
        out += static_cast<char>('a' + fileOf(m_enPassant));
        out += static_cast<char>('1' + rankOf(m_enPassant));
    }
    out += ' ' + std::to_string(m_halfmoves) + ' ' + std::to_string(m_fullmoves);
    return out;
}

Square Position::kingOf(Color side) const {
    for (Square s = 0; s < 64; ++s) {
        const Piece p = at(s);
        if (p.type == PieceType::King && p.color == side) return s;
    }
    return NO_SQUARE;
}

bool Position::attacked(Square s, Color by) const {
    const int file = fileOf(s);
    const int rank = rankOf(s);

    // A pawn attacks diagonally forward, so it stands one rank behind the square it attacks.
    const int pawnRank = rank - forward(by);
    for (const int df : {-1, 1}) {
        if (!onBoard(file + df, pawnRank)) continue;
        const Piece p = at(squareAt(file + df, pawnRank));
        if (p.type == PieceType::Pawn && p.color == by) return true;
    }
    for (const auto& step : KNIGHT_STEPS) {
        if (!onBoard(file + step[0], rank + step[1])) continue;
        const Piece p = at(squareAt(file + step[0], rank + step[1]));
        if (p.type == PieceType::Knight && p.color == by) return true;
    }
    for (const auto& step : KING_STEPS) {
        if (!onBoard(file + step[0], rank + step[1])) continue;
        const Piece p = at(squareAt(file + step[0], rank + step[1]));
        if (p.type == PieceType::King && p.color == by) return true;
    }
    const auto slides = [&](const int (&rays)[4][2], PieceType straight) {
        for (const auto& ray : rays) {
            int f = file + ray[0];
            int r = rank + ray[1];
            while (onBoard(f, r)) {
                const Piece p = at(squareAt(f, r));
                if (!p.empty()) {
                    if (p.color == by && (p.type == straight || p.type == PieceType::Queen)) return true;
                    break;
                }
                f += ray[0];
                r += ray[1];
            }
        }
        return false;
    };
    return slides(ROOK_RAYS, PieceType::Rook) || slides(BISHOP_RAYS, PieceType::Bishop);
}

bool Position::inCheck(Color side) const {
    const Square king = kingOf(side);
    return king != NO_SQUARE && attacked(king, opposite(side));
}

Square Position::takenBy(const Move& move) const {
    if (move.kind == MoveKind::EnPassant) return squareAt(fileOf(move.to), rankOf(move.from));
    return at(move.to).empty() ? NO_SQUARE : move.to;
}

void Position::pseudoLegalMoves(std::vector<Move>& out) const {
    const Color us   = m_side;
    const Color them = opposite(us);

    const auto addPawnMove = [&](Square from, Square to, MoveKind kind) {
        if (rankOf(to) == 0 || rankOf(to) == 7) {
            for (const PieceType promotion : PROMOTIONS) out.push_back({from, to, kind, promotion});
        } else {
            out.push_back({from, to, kind, PieceType::None});
        }
    };

    for (Square from = 0; from < 64; ++from) {
        const Piece piece = at(from);
        if (piece.empty() || piece.color != us) continue;
        const int file = fileOf(from);
        const int rank = rankOf(from);

        switch (piece.type) {
            case PieceType::Pawn: {
                const int dir      = forward(us);
                const int homeRank = us == Color::White ? 1 : 6;
                if (onBoard(file, rank + dir) && at(squareAt(file, rank + dir)).empty()) {
                    addPawnMove(from, squareAt(file, rank + dir), MoveKind::Quiet);
                    if (rank == homeRank && at(squareAt(file, rank + 2 * dir)).empty()) {
                        out.push_back({from, squareAt(file, rank + 2 * dir), MoveKind::DoublePush, PieceType::None});
                    }
                }
                for (const int df : {-1, 1}) {
                    if (!onBoard(file + df, rank + dir)) continue;
                    const Square to     = squareAt(file + df, rank + dir);
                    const Piece  target = at(to);
                    if (!target.empty() && target.color == them) addPawnMove(from, to, MoveKind::Capture);
                    if (to == m_enPassant) out.push_back({from, to, MoveKind::EnPassant, PieceType::None});
                }
                break;
            }
            case PieceType::Knight:
            case PieceType::King: {
                const auto& steps = piece.type == PieceType::Knight ? KNIGHT_STEPS : KING_STEPS;
                for (const auto& step : steps) {
                    if (!onBoard(file + step[0], rank + step[1])) continue;
                    const Square to     = squareAt(file + step[0], rank + step[1]);
                    const Piece  target = at(to);
                    if (target.empty()) out.push_back({from, to, MoveKind::Quiet, PieceType::None});
                    else if (target.color == them) out.push_back({from, to, MoveKind::Capture, PieceType::None});
                }
                break;
            }
            case PieceType::Bishop:
            case PieceType::Rook:
            case PieceType::Queen: {
                const auto ray = [&](int df, int dr) {
                    int f = file + df;
                    int r = rank + dr;
                    while (onBoard(f, r)) {
                        const Square to     = squareAt(f, r);
                        const Piece  target = at(to);
                        if (!target.empty()) {
                            if (target.color == them) out.push_back({from, to, MoveKind::Capture, PieceType::None});
                            break;
                        }
                        out.push_back({from, to, MoveKind::Quiet, PieceType::None});
                        f += df;
                        r += dr;
                    }
                };
                if (piece.type != PieceType::Bishop) {
                    for (const auto& r : ROOK_RAYS) ray(r[0], r[1]);
                }
                if (piece.type != PieceType::Rook) {
                    for (const auto& r : BISHOP_RAYS) ray(r[0], r[1]);
                }
                break;
            }
            case PieceType::None:
                break;
        }
    }

    // Castling: the rights, an empty path, and a king neither in check nor crossing an attack.
    const int    homeRank  = us == Color::White ? 0 : 7;
    const Square king      = squareAt(4, homeRank);
    const uint8_t kingSide = us == Color::White ? WHITE_KING_SIDE : BLACK_KING_SIDE;
    const uint8_t queenSide = us == Color::White ? WHITE_QUEEN_SIDE : BLACK_QUEEN_SIDE;
    if (at(king).type == PieceType::King && at(king).color == us && (m_castling & (kingSide | queenSide))
        && !attacked(king, them)) {
        if ((m_castling & kingSide) && at(king + 1).empty() && at(king + 2).empty()
            && !attacked(king + 1, them) && !attacked(king + 2, them)) {
            out.push_back({king, king + 2, MoveKind::CastleKing, PieceType::None});
        }
        if ((m_castling & queenSide) && at(king - 1).empty() && at(king - 2).empty() && at(king - 3).empty()
            && !attacked(king - 1, them) && !attacked(king - 2, them)) {
            out.push_back({king, king - 2, MoveKind::CastleQueen, PieceType::None});
        }
    }
}

std::vector<Move> Position::legalMoves() const {
    std::vector<Move> candidates;
    candidates.reserve(64);
    pseudoLegalMoves(candidates);

    std::vector<Move> legal;
    legal.reserve(candidates.size());
    for (const Move& move : candidates) {
        Position after = *this;
        after.play(move);
        if (!after.inCheck(m_side)) legal.push_back(move);
    }
    return legal;
}

std::optional<Move> Position::findMove(Square from, Square to, PieceType promotion) const {
    for (const Move& move : legalMoves()) {
        if (move.from != from || move.to != to) continue;
        if (move.promotion != PieceType::None && move.promotion != promotion) continue;
        return move;
    }
    return std::nullopt;
}

void Position::play(const Move& move) {
    const Piece moving   = at(move.from);
    const Piece captured = at(move.to);
    const Color us       = moving.color;

    m_board[static_cast<size_t>(move.to)]   = moving;
    m_board[static_cast<size_t>(move.from)] = Piece{};
    if (move.promotion != PieceType::None) m_board[static_cast<size_t>(move.to)].type = move.promotion;

    if (move.kind == MoveKind::EnPassant) {
        m_board[static_cast<size_t>(squareAt(fileOf(move.to), rankOf(move.from)))] = Piece{};
    } else if (move.kind == MoveKind::CastleKing) {
        m_board[static_cast<size_t>(move.to - 1)] = at(move.to + 1);
        m_board[static_cast<size_t>(move.to + 1)] = Piece{};
    } else if (move.kind == MoveKind::CastleQueen) {
        m_board[static_cast<size_t>(move.to + 1)] = at(move.to - 2);
        m_board[static_cast<size_t>(move.to - 2)] = Piece{};
    }

    if (moving.type == PieceType::King) {
        m_castling &= us == Color::White ? ~(WHITE_KING_SIDE | WHITE_QUEEN_SIDE) : ~(BLACK_KING_SIDE | BLACK_QUEEN_SIDE);
    }
    m_castling &= static_cast<uint8_t>(~(rightsOfCorner(move.from) | rightsOfCorner(move.to)));

    m_enPassant = move.kind == MoveKind::DoublePush ? (move.from + move.to) / 2 : NO_SQUARE;

    const bool irreversible = moving.type == PieceType::Pawn || !captured.empty();
    m_halfmoves = irreversible ? 0 : m_halfmoves + 1;
    if (us == Color::Black) ++m_fullmoves;
    m_side = opposite(us);

    if (irreversible) m_history.clear();
    m_history.push_back(key());
}

bool Position::pass() {
    if (inCheck(m_side)) return false;
    if (m_side == Color::Black) ++m_fullmoves;
    m_side      = opposite(m_side);
    m_enPassant = NO_SQUARE;
    ++m_halfmoves;
    m_history.push_back(key());
    return true;
}

uint64_t Position::key() const {
    // FNV-1a over what makes two positions the same for repetition: the pieces, the side to
    // move, the castling rights and an en-passant square only where a pawn could take there.
    uint64_t h = 14695981039346656037ull;
    const auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    for (const Piece p : m_board) mix((static_cast<uint64_t>(p.type) << 1) | static_cast<uint64_t>(p.color));
    mix(static_cast<uint64_t>(m_side));
    mix(m_castling);
    Square ep = NO_SQUARE;
    if (m_enPassant != NO_SQUARE) {
        const int captureRank = rankOf(m_enPassant) - forward(m_side);
        for (const int df : {-1, 1}) {
            const int f = fileOf(m_enPassant) + df;
            if (!onBoard(f, captureRank)) continue;
            const Piece p = at(squareAt(f, captureRank));
            if (p.type == PieceType::Pawn && p.color == m_side) ep = m_enPassant;
        }
    }
    mix(static_cast<uint64_t>(ep + 1));
    return h;
}

Outcome Position::outcome() const {
    if (legalMoves().empty()) return inCheck(m_side) ? Outcome::Checkmate : Outcome::Stalemate;
    if (m_halfmoves >= 100) return Outcome::FiftyMoves;
    if (std::count(m_history.begin(), m_history.end(), m_history.back()) >= 3) return Outcome::Repetition;

    // Insufficient material: bare kings, one minor piece, or bishops all on one colour.
    int minors = 0;
    int bishopsOnLight = 0;
    int bishopsOnDark  = 0;
    for (Square s = 0; s < 64; ++s) {
        const Piece p = at(s);
        switch (p.type) {
            case PieceType::Pawn:
            case PieceType::Rook:
            case PieceType::Queen:
                return Outcome::Ongoing;
            case PieceType::Knight:
                ++minors;
                break;
            case PieceType::Bishop:
                ++minors;
                ((fileOf(s) + rankOf(s)) % 2 ? bishopsOnLight : bishopsOnDark)++;
                break;
            default:
                break;
        }
    }
    const int bishops = bishopsOnLight + bishopsOnDark;
    if (minors <= 1) return Outcome::InsufficientMaterial;
    if (bishops == minors && (bishopsOnLight == 0 || bishopsOnDark == 0)) return Outcome::InsufficientMaterial;
    return Outcome::Ongoing;
}

std::string Position::toUci(const Move& move) {
    std::string out;
    out += static_cast<char>('a' + fileOf(move.from));
    out += static_cast<char>('1' + rankOf(move.from));
    out += static_cast<char>('a' + fileOf(move.to));
    out += static_cast<char>('1' + rankOf(move.to));
    if (move.promotion != PieceType::None) out += " pnbrqk"[static_cast<int>(move.promotion)];
    return out;
}

} // namespace Chess
