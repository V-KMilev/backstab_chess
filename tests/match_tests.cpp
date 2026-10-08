// Backstab's rules over chess's: whose turn it is, who owns what, the duels and the scores.

#include <cstdio>
#include <string>
#include <vector>

#include "chess/match.h"

using namespace Chess;

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-66s %s\n", what, ok ? "ok" : "<-- FAILED");
}

Move moveOf(const Match& m, const char* uci) {
    const Square from = squareAt(uci[0] - 'a', uci[1] - '1');
    const Square to   = squareAt(uci[2] - 'a', uci[3] - '1');
    const auto   move = m.position().findMove(from, to);
    if (!move) {
        std::printf("  illegal move in a test line: %s\n", uci);
        ++g_failures;
        return {};
    }
    return *move;
}

Attempt play(Match& m, const char* uci) { return m.attempt(moveOf(m, uci)); }

Square sq(const char* name) { return squareAt(name[0] - 'a', name[1] - '1'); }

// A, C white; B, D black: the rota goes A, B, C, D.
Match twoAside() {
    return Match({{"A", Color::White}, {"B", Color::Black}, {"C", Color::White}, {"D", Color::Black}});
}

void testTurnsGoRoundEachColour() {
    std::printf("Turns:\n");
    Match m = twoAside();
    std::string order;
    for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"}) {
        order += m.players()[static_cast<size_t>(m.current())].name;
        play(m, uci);
    }
    check("two a side go A, B, C, D and round again", order == "ABCDA");

    Match uneven({{"A", Color::White}, {"B", Color::Black}, {"C", Color::Black}});
    order.clear();
    for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"}) {
        order += uneven.players()[static_cast<size_t>(uneven.current())].name;
        play(uneven, uci);
    }
    check("one against two: the one plays every white move", order == "ABACA");
}

void testWhatYouMoveIsYours() {
    std::printf("Ownership:\n");
    Match m = twoAside();
    check("a piece nobody has moved is nobody's", m.ownerOf(sq("d2")) == -1);
    check("moving it is no duel", play(m, "d2d4") == Attempt::Played);
    check("  and it is the mover's", m.ownerOf(sq("d4")) == 0);
    play(m, "e7e6");  // B
    play(m, "g1f3");  // C
    play(m, "b8c6");  // D
    check("its owner moves it again freely", play(m, "d4d5") == Attempt::Played);
}

void testATeammatesPieceIsADuel() {
    std::printf("A teammate's piece:\n");
    {
        Match m = twoAside();
        play(m, "g1f3");  // A owns the knight
        play(m, "e7e5");  // B
        check("C moving A's knight is a duel", play(m, "f3g5") == Attempt::Duel);
        check("  against A", m.duel() && m.duel()->defender == 0 && m.duel()->kind == DuelKind::Teammate);
        m.resolveDuel(true);
        check("won, C's move is played", m.position().at(sq("g5")).type == PieceType::Knight);
        check("  the knight is C's now", m.ownerOf(sq("g5")) == 2);
        check("  C scores the duel", m.players()[2].score == Points::DUEL_WON);
        check("  and it is D's turn", m.current() == 3);
    }
    {
        Match m = twoAside();
        play(m, "g1f3");  // A
        play(m, "e7e5");  // B
        play(m, "f3g5");  // C challenges A
        m.resolveDuel(false);
        check("lost, A plays the turn instead", m.current() == 0);
        check("  A scores the duel", m.players()[0].score == Points::DUEL_WON);
        check("  A plays any white move", play(m, "d2d4") == Attempt::Played);
        check("  then black's turn goes on as it would: D", m.current() == 3);
        play(m, "d7d6");  // D
        check("  and white's next turn is A's again, C's having been spent", m.current() == 0);
    }
}

void testTakingAnEnemysPieceIsADuel() {
    std::printf("An enemy's piece:\n");
    const auto toTheCapture = [](Match& m) {
        play(m, "e2e4");  // A owns e4
        play(m, "d7d5");  // B owns d5
        play(m, "a2a3");  // C
        play(m, "a7a6");  // D
    };
    {
        Match m = twoAside();
        toTheCapture(m);
        check("taking it is a duel", play(m, "e4d5") == Attempt::Duel);
        check("  against its owner", m.duel() && m.duel()->defender == 1 && m.duel()->kind == DuelKind::Capture);
        m.resolveDuel(true);
        check("won, the pawn is taken", m.position().at(sq("d5")).color == Color::White);
        check("  and the taker scores it and the duel", m.players()[0].score == Points::PAWN + Points::DUEL_WON);
    }
    {
        Match m = twoAside();
        toTheCapture(m);
        play(m, "e4d5");  // A challenges B
        m.resolveDuel(false);
        check("lost, nothing is taken", m.position().at(sq("d5")).color == Color::Black);
        check("  white has passed", m.position().sideToMove() == Color::Black);
        check("  and B moves at once", m.current() == 1);
        play(m, "g8f6");  // B, out of turn
        check("  then white's turn goes on: C", m.current() == 2);
        play(m, "b1c3");  // C
        check("  and black's is B's own turn, the last having been a bonus", m.current() == 1);
    }
    {
        Match m = twoAside();
        play(m, "e2e4");  // A owns e4
        play(m, "d7d5");  // B owns d5
        check("taking with a teammate's piece duels the teammate first", play(m, "e4d5") == Attempt::Duel
            && m.duel()->defender == 0);
        m.resolveDuel(true);
        check("  then the piece's owner", m.duel() && m.duel()->defender == 1 && m.duel()->kind == DuelKind::Capture);
        m.resolveDuel(true);
        check("  and both won, the pawn is taken", m.position().at(sq("d5")).color == Color::White);
        check("  scoring both duels and the pawn", m.players()[2].score == 2 * Points::DUEL_WON + Points::PAWN);
    }
    {
        Match m = twoAside();
        play(m, "e2e4");  // A
        play(m, "a7a6");  // B
        play(m, "d1h5");  // C owns the queen
        play(m, "b7b6");  // D
        play(m, "a2a3");  // A
        play(m, "h7h6");  // B
        check("a piece nobody moved is taken freely", play(m, "h5f7") == Attempt::Played);
        check("  for its points", m.players()[2].score == Points::PAWN);
    }
}

void testTheMatingPlayerScoresTheWin() {
    std::printf("Checkmate:\n");
    Match m = twoAside();
    play(m, "f2f3");  // A
    play(m, "e7e5");  // B
    play(m, "g2g4");  // C
    play(m, "d8h4");  // D mates
    check("the game is over", m.over());
    check("the player who mated scores it", m.players()[3].score == Points::CHECKMATE);
}

} // namespace

int main() {
    testTurnsGoRoundEachColour();
    testWhatYouMoveIsYours();
    testATeammatesPieceIsADuel();
    testTakingAnEnemysPieceIsADuel();
    testTheMatingPlayerScoresTheWin();
    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL OK\n");
    return 0;
}
