#include "chess/match.h"

namespace Chess {

namespace {

size_t sideIndex(Color side) { return side == Color::White ? 0 : 1; }

} // namespace

int captureValue(PieceType type) {
    switch (type) {
        case PieceType::Pawn:   return Points::PAWN;
        case PieceType::Knight: return Points::KNIGHT;
        case PieceType::Bishop: return Points::BISHOP;
        case PieceType::Rook:   return Points::ROOK;
        case PieceType::Queen:  return Points::QUEEN;
        default:                return 0;
    }
}

Match::Match(std::vector<Player> players)
    : m_players(std::move(players)) {
    m_owner.fill(-1);
    for (size_t i = 0; i < m_players.size(); ++i) {
        m_rota[sideIndex(m_players[i].side)].push_back(static_cast<int>(i));
    }
    handTo(Color::White);
}

void Match::handTo(Color side) {
    const std::vector<int>& rota = m_rota[sideIndex(side)];
    m_current = rota.empty() ? -1 : rota[m_next[sideIndex(side)] % rota.size()];
}

Attempt Match::attempt(const Move& move) {
    if (m_duel || over() || m_current < 0) return Attempt::Illegal;
    bool legal = false;
    for (const Move& m : m_position.legalMoves()) legal = legal || m == move;
    if (!legal) return Attempt::Illegal;

    const Color side  = m_position.sideToMove();
    const int   mover = ownerOf(move.from);
    Square taken = NO_SQUARE;
    if (move.kind == MoveKind::EnPassant) taken = squareAt(fileOf(move.to), rankOf(move.from));
    else if (!m_position.at(move.to).empty()) taken = move.to;
    const int victim = taken != NO_SQUARE ? ownerOf(taken) : -1;

    if (mover >= 0 && mover != m_current) {
        m_duel = Duel{m_current, mover, move, DuelKind::Teammate};
        return Attempt::Duel;
    }
    if (victim >= 0 && !m_position.inCheck(side)) {
        m_duel = Duel{m_current, victim, move, DuelKind::Capture};
        return Attempt::Duel;
    }
    play(m_current, move);
    return Attempt::Played;
}

void Match::resolveDuel(bool challengerWon) {
    if (!m_duel) return;
    const Duel  duel = *m_duel;
    const Color side = m_position.sideToMove();
    m_duel.reset();

    if (challengerWon) {
        m_players[static_cast<size_t>(duel.challenger)].score += Points::DUEL_WON;
        // Won the piece off a teammate to take an enemy's: that owner defends too.
        if (duel.kind == DuelKind::Teammate) {
            Square taken = NO_SQUARE;
            if (duel.move.kind == MoveKind::EnPassant) taken = squareAt(fileOf(duel.move.to), rankOf(duel.move.from));
            else if (!m_position.at(duel.move.to).empty()) taken = duel.move.to;
            const int victim = taken != NO_SQUARE ? ownerOf(taken) : -1;
            if (victim >= 0 && !m_position.inCheck(side)) {
                m_duel = Duel{duel.challenger, victim, duel.move, DuelKind::Capture};
                return;
            }
        }
        play(duel.challenger, duel.move);
        return;
    }

    m_players[static_cast<size_t>(duel.defender)].score += Points::DUEL_WON;
    m_current = duel.defender;
    if (duel.kind == DuelKind::Capture) {
        // The challenger's colour passes, its turn spent; the defender moves out of turn.
        ++m_next[sideIndex(side)];
        m_position.pass();
        m_bonus = true;
    }
}

void Match::play(int player, const Move& move) {
    const Color side = m_position.sideToMove();

    Square taken = NO_SQUARE;
    if (move.kind == MoveKind::EnPassant) taken = squareAt(fileOf(move.to), rankOf(move.from));
    else if (!m_position.at(move.to).empty()) taken = move.to;
    if (taken != NO_SQUARE) {
        m_players[static_cast<size_t>(player)].score += captureValue(m_position.at(taken).type);
        m_owner[static_cast<size_t>(taken)] = -1;
    }

    // The mover owns what it moved; castling moves the rook too.
    m_owner[static_cast<size_t>(move.to)]   = player;
    m_owner[static_cast<size_t>(move.from)] = -1;
    if (move.kind == MoveKind::CastleKing) {
        m_owner[static_cast<size_t>(move.to - 1)] = player;
        m_owner[static_cast<size_t>(move.to + 1)] = -1;
    } else if (move.kind == MoveKind::CastleQueen) {
        m_owner[static_cast<size_t>(move.to + 1)] = player;
        m_owner[static_cast<size_t>(move.to - 2)] = -1;
    }

    m_position.play(move);
    if (m_position.outcome() == Outcome::Checkmate) m_players[static_cast<size_t>(player)].score += Points::CHECKMATE;

    // Every move spends its colour's turn but a won defence's, which came out of turn.
    if (m_bonus) m_bonus = false;
    else ++m_next[sideIndex(side)];
    handTo(m_position.sideToMove());
}

} // namespace Chess
