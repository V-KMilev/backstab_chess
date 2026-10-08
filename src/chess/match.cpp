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

Match::Match(std::vector<Player> players, const Position& start)
    : m_position(start), m_players(std::move(players)) {
    m_owner.fill(-1);
    m_lifePoints.assign(m_players.size(), 0);
    for (size_t i = 0; i < m_players.size(); ++i) {
        m_rota[sideIndex(m_players[i].side)].push_back(static_cast<int>(i));
    }
    handTo(Color::White);
}

void Match::handTo(Color side) {
    const std::vector<int>& rota = m_rota[sideIndex(side)];
    m_current = rota.empty() ? -1 : rota[m_next[sideIndex(side)] % rota.size()];
}

std::optional<Duel> Match::duelFor(const Move& move) const {
    const int mover = ownerOf(move.from);
    if (mover >= 0 && mover != m_current) return Duel{m_current, mover, move, DuelKind::Teammate};
    return captureDuel(m_current, move);
}

std::optional<Duel> Match::captureDuel(int challenger, const Move& move) const {
    const Square taken  = m_position.takenBy(move);
    const int    victim = taken != NO_SQUARE ? ownerOf(taken) : -1;
    if (victim < 0 || m_position.inCheck(m_position.sideToMove())) return std::nullopt;
    return Duel{challenger, victim, move, DuelKind::Capture};
}

Attempt Match::attempt(const Move& move) {
    if (m_duel || over() || m_current < 0) return Attempt::Illegal;
    bool legal = false;
    for (const Move& m : m_position.legalMoves()) legal = legal || m == move;
    if (!legal) return Attempt::Illegal;

    m_duel = duelFor(move);
    if (m_duel) return Attempt::Duel;
    play(m_current, move);
    return Attempt::Played;
}

void Match::resolveDuel(bool challengerWon) {
    if (!m_duel) return;
    const Duel  duel = *m_duel;
    const Color side = m_position.sideToMove();
    m_duel.reset();

    // A strike: its move is played already, and the side struck at is the one to move.
    if (duel.kind == DuelKind::Strike) {
        if (!challengerWon) {
            m_players[static_cast<size_t>(duel.defender)].score += Points::DUEL_WON;
            return;
        }
        Player& striker = m_players[static_cast<size_t>(duel.challenger)];
        striker.score += Points::KING_LIFE;
        m_lifePoints[static_cast<size_t>(duel.challenger)] += Points::KING_LIFE;
        if (--m_lives[sideIndex(side)] > 0) return;
        // The king falls: the striker takes every life's points and the mate's.
        m_fallen = static_cast<int>(sideIndex(side));
        for (size_t p = 0; p < m_players.size(); ++p) {
            if (static_cast<int>(p) == duel.challenger) continue;
            m_players[p].score -= m_lifePoints[p];
            striker.score      += m_lifePoints[p];
            m_lifePoints[static_cast<size_t>(duel.challenger)] += m_lifePoints[p];
            m_lifePoints[p] = 0;
        }
        striker.score += Points::CHECKMATE;
        return;
    }

    if (challengerWon) {
        m_players[static_cast<size_t>(duel.challenger)].score += Points::DUEL_WON;
        // Won the piece off a teammate to take an enemy's: that owner defends too.
        if (duel.kind == DuelKind::Teammate) {
            m_duel = captureDuel(duel.challenger, duel.move);
            if (m_duel) return;
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

    const Square taken = m_position.takenBy(move);
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

    // Check, short of mate, is a strike at the king.
    const Color struck = m_position.sideToMove();
    if (m_position.outcome() == Outcome::Ongoing && m_position.inCheck(struck)) {
        const int owner = ownerOf(m_position.king(struck));
        m_duel = Duel{player, owner >= 0 ? owner : m_current, move, DuelKind::Strike};
    }
}

} // namespace Chess
