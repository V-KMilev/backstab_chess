// What the server replicates comes back as it was sent, inside the engine's 512-byte ceiling for
// one component, and a player's looks survive being packed for their seat.

#include <cmath>
#include <cstdio>
#include <vector>

#include "net_state.h"

using namespace Game;

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-66s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// Encodes @p value, returning its size in bytes, and decodes it into @p back.
template <typename T>
size_t roundTrip(const T& value, T& back) {
    std::vector<uint8_t> bytes;
    BitWriter            out(bytes, 4096);
    netEncode(value, out);
    out.finish();
    BitReader in(bytes.data(), bytes.size());
    netDecode(back, in);
    return in.failed() || out.overflowed() ? 0 : bytes.size();
}

} // namespace

int main() {
    // Every kind of action, through 16 bits and back.
    bool actions = true;
    for (int from = 0; from < 64; from += 7) {
        for (int to = 0; to < 64; to += 5) {
            for (const auto promotion : {Chess::PieceType::None, Chess::PieceType::Queen, Chess::PieceType::Knight}) {
                Action action;
                action.move.from      = static_cast<Chess::Square>(from);
                action.move.to        = static_cast<Chess::Square>(to);
                action.move.promotion = promotion;
                const Action back = unpackAction(packAction(action));
                actions = actions && !back.resolve && back.move.from == action.move.from && back.move.to == action.move.to
                    && back.move.promotion == promotion;
            }
        }
    }
    check("a move packs and unpacks", actions);
    for (const bool won : {false, true}) {
        const Action back = unpackAction(packAction({true, won, {}}));
        check(won ? "a duel won packs and unpacks" : "a duel lost packs and unpacks", back.resolve && back.won == won);
    }

    // A full seat.
    SeatState seat;
    seat.player = 517;
    seat.name   = "Long Name Here";
    seat.side   = 1;
    seat.hue    = 201;
    for (size_t i = 0; i < seat.looks.size(); ++i) seat.looks[i] = static_cast<uint8_t>(i * 19 + 3);
    seat.ack = 254;
    SeatState sat;
    const size_t seatSize = roundTrip(seat, sat);
    check("a seat comes back as sent", seatSize > 0 && sat.player == seat.player && sat.name == seat.name && sat.side == seat.side
                                           && sat.hue == seat.hue && sat.looks == seat.looks && sat.ack == seat.ack);

    // A match at its largest: every seat playing and the whole log full.
    MatchState match;
    match.phase       = 1;
    match.host        = 3;
    match.turnSeconds = 90;
    match.duelSeconds = 8;
    match.game        = 40000;
    match.players     = SEATS;
    for (int i = 0; i < SEATS; ++i) match.order[static_cast<size_t>(i)] = static_cast<uint8_t>(SEATS - 1 - i);
    match.actions = 1000;
    for (size_t i = 0; i < HISTORY; ++i) match.log[i] = static_cast<uint16_t>(i * 1021);
    match.duel  = 77;
    match.stops = {543, NO_STOP};
    MatchState back;
    const size_t matchSize = roundTrip(match, back);
    check("a match comes back as sent", matchSize > 0 && back.phase == match.phase && back.host == match.host
                                            && back.turnSeconds == match.turnSeconds && back.duelSeconds == match.duelSeconds
                                            && back.game == match.game && back.players == match.players && back.order == match.order
                                            && back.actions == match.actions && back.log == match.log && back.duel == match.duel
                                            && back.stops == match.stops);
    check("a match fits the engine's 512 bytes for a component", matchSize > 0 && matchSize <= 512);
    check("a seat fits the engine's 512 bytes for a component", seatSize > 0 && seatSize <= 512);

    // Looks, through a seat's 13 bytes.
    Profile profile;
    profile.pieces = {Finish::Gem, 0.3f, 0.8f, 0.6f, 0.4f};
    profile.take   = {TakeStyle::Vortex, 1.5f, 0.7f, 2.0f, 0.9f};
    profile.claim  = {ClaimStyle::Wave, 1.2f, 0.25f};
    PieceLook pieces;
    TakeLook  take;
    ClaimLook claim;
    unpackLooks(packLooks(profile), pieces, take, claim);
    const auto near = [](float a, float b, float range) { return std::abs(a - b) <= range / 200.0f; };
    check("a profile's looks survive its seat",
          pieces.finish == profile.pieces.finish && near(pieces.hue, profile.pieces.hue, 1.0f) && take.style == profile.take.style
              && near(take.speed, profile.take.speed, 3.0f) && claim.style == profile.claim.style
              && near(claim.hue, profile.claim.hue, 1.0f));

    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL OK\n");
    return 0;
}
