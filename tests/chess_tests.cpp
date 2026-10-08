// The rules against the counts every chess engine is checked by (perft: the number of move
// sequences to a depth, from positions chosen to catch castling, en passant and promotion
// mistakes), and the ends of a game.

#include <cstdint>
#include <cstdio>
#include <string>

#include "chess/position.h"

using namespace Chess;

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-60s %s\n", what, ok ? "ok" : "<-- FAILED");
}

uint64_t perft(const Position& p, int depth) {
    if (depth == 0) return 1;
    uint64_t nodes = 0;
    for (const Move& move : p.legalMoves()) {
        Position next = p;
        next.play(move);
        nodes += perft(next, depth - 1);
    }
    return nodes;
}

void testPerft(const char* name, const char* fen, std::initializer_list<uint64_t> counts) {
    std::printf("%s:\n", name);
    const auto p = Position::fromFen(fen);
    check("the FEN reads", p.has_value());
    if (!p) return;
    int depth = 1;
    for (const uint64_t expected : counts) {
        const uint64_t got = perft(*p, depth);
        char line[128];
        std::snprintf(line, sizeof(line), "depth %d: %llu move sequences", depth, static_cast<unsigned long long>(got));
        check(line, got == expected);
        ++depth;
    }
}

Position after(const char* fen, std::initializer_list<const char*> moves) {
    Position p = *Position::fromFen(fen);
    for (const char* uci : moves) {
        const Square from = squareAt(uci[0] - 'a', uci[1] - '1');
        const Square to   = squareAt(uci[2] - 'a', uci[3] - '1');
        const auto move   = p.findMove(from, to);
        if (!move) {
            std::printf("  illegal move in a test line: %s\n", uci);
            ++g_failures;
            return p;
        }
        p.play(*move);
    }
    return p;
}

void testTheEndsOfAGame() {
    std::printf("The ends of a game:\n");
    const char* START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    const Position fools = after(START, {"f2f3", "e7e5", "g2g4", "d8h4"});
    check("fool's mate is checkmate", fools.outcome() == Outcome::Checkmate);

    const Position stale = *Position::fromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    check("a king with no move and no check is stalemate", stale.outcome() == Outcome::Stalemate);

    const Position shuffled = after(START, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"});
    check("the same position three times is a draw", shuffled.outcome() == Outcome::Repetition);
    const Position twice = after(START, {"g1f3", "g8f6", "f3g1", "f6g8"});
    check("  twice is not", twice.outcome() == Outcome::Ongoing);

    check("bare kings are a draw", Position::fromFen("8/8/4k3/8/8/3K4/8/8 w - - 0 1")->outcome() == Outcome::InsufficientMaterial);
    check("  so is a lone knight", Position::fromFen("8/8/4k3/8/8/3K4/5N2/8 w - - 0 1")->outcome() == Outcome::InsufficientMaterial);
    check("  but not a rook", Position::fromFen("8/8/4k3/8/8/3K4/5R2/8 w - - 0 1")->outcome() == Outcome::Ongoing);
    check("fifty moves without a capture or pawn move is a draw",
          Position::fromFen("8/8/4k3/8/8/3K4/5R2/8 w - - 100 80")->outcome() == Outcome::FiftyMoves);

    const Position promoted = after("8/4P3/8/8/8/2k5/8/K7 w - - 0 1", {"e7e8"});
    check("a pawn reaching the last rank becomes a queen", promoted.at(squareAt(4, 7)).type == PieceType::Queen);

    const Position castled = after(START, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "e1g1"});
    check("castling king side moves the rook beside the king",
          castled.at(squareAt(5, 0)).type == PieceType::Rook && castled.at(squareAt(6, 0)).type == PieceType::King);

    const Position passant = after(START, {"e2e4", "a7a6", "e4e5", "d7d5", "e5d6"});
    check("en passant removes the pawn that passed", passant.at(squareAt(3, 4)).empty());

    check("FEN reads back as written",
          Position::fromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1")->fen()
              == "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
}

} // namespace

int main() {
    testPerft("The opening position", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {20, 400, 8902, 197281});
    testPerft("Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", {48, 2039, 97862});
    testPerft("Position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {14, 191, 2812, 43238});
    testPerft("Position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", {6, 264, 9467});
    testPerft("Position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {44, 1486, 62379});
    testTheEndsOfAGame();

    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL OK\n");
    return 0;
}
