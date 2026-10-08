#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "net/wire/bit_stream.h"

#include "chess/position.h"
#include "profile.h"

// What the server tells every client, as two replicated components: a seat's player, with the
// looks they designed, and the match - its phase, its settings, its players' seats, and every
// action played, in order, which clients replay through the same rules to get the same game.
namespace Game {

using namespace Vkm::Engine;

constexpr int    SEATS   = 8;   ///< Players a game takes, four a side.
constexpr size_t HISTORY = 64;  ///< Actions the match keeps for clients still catching up.

/// One seat at the table, empty while its player is 0.
struct SeatState {
    uint16_t    player = 0;
    std::string name;
    uint8_t     side   = 0;  ///< 0 white, 1 black.
    // The looks, each 0..255.
    uint8_t     hue = 150;
    std::array<uint8_t, 13> looks{};
    uint8_t     ack = 0;     ///< The last message from this seat the server applied.
};

/// An action of the match: a move tried, or a duel settled.
struct Action {
    bool        resolve = false;
    bool        won     = false;  ///< For a duel settled: whether the challenger won.
    Chess::Move move;             ///< For a move: from, to and what a pawn becomes.
};

uint16_t packAction(const Action& action);
Action   unpackAction(uint16_t bits);

/// The match, as the server runs it.
struct MatchState {
    uint8_t  phase       = 0;   ///< 0 the lobby, 1 playing, 2 over.
    uint16_t host        = 0;   ///< The player who may start a match.
    uint8_t  turnSeconds = 60;
    uint8_t  duelSeconds = 6;
    uint16_t game        = 0;   ///< Which match: a new start counts up, and clients begin afresh.
    uint8_t  players     = 0;
    std::array<uint8_t, SEATS> order{};  ///< The seat of each of the match's players, in its order.
    uint16_t actions     = 0;   ///< How many have been played.
    std::array<uint16_t, HISTORY> log{};  ///< Action i at log[i % HISTORY].
    uint8_t  duel        = 0;   ///< Counts the duels opened, so a stop names the duel it is for.
    std::array<uint16_t, 2> stops = {0xFFFF, 0xFFFF};  ///< When each duelist stopped, centiseconds.
};

constexpr uint16_t NO_STOP = 0xFFFF;

void netEncode(const SeatState& seat, BitWriter& out);
void netDecode(SeatState& seat, BitReader& in);
void netEncode(const MatchState& match, BitWriter& out);
void netDecode(MatchState& match, BitReader& in);

/// A profile's looks, packed for a seat, and back.
std::array<uint8_t, 13> packLooks(const Profile& profile);
void unpackLooks(const std::array<uint8_t, 13>& looks, PieceLook& pieces, TakeLook& take, ClaimLook& claim);

/// Registers both components with the network: once, from vkmSetupNetwork.
void registerNetTypes();

// What a client says to the server, in its commands' payload: a nonce, a kind, then the kind's
// bytes. The client holds a message until its seat's ack shows it applied.
enum class Message : uint8_t { None, Name, Looks, Side, Start, Move, Stop, Again };

} // namespace Game
