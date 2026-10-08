#include "net_state.h"

#include <algorithm>
#include <cmath>

#include "net/wire/schema.h"

namespace Game {

namespace {

constexpr uint32_t NAME_BITS = 5;  ///< A name's length: up to 31, held to 16.
constexpr size_t   MAX_NAME  = 16;

uint8_t toByte(float v, float lo, float hi) {
    return static_cast<uint8_t>(std::lround(std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) * 255.0f));
}

float fromByte(uint8_t b, float lo, float hi) { return lo + (hi - lo) * static_cast<float>(b) / 255.0f; }

} // namespace

uint16_t packAction(const Action& action) {
    if (action.resolve) return static_cast<uint16_t>(0x8000u | (action.won ? 1u : 0u));
    return static_cast<uint16_t>((action.move.from & 63) | ((action.move.to & 63) << 6) | ((static_cast<unsigned>(action.move.promotion) & 7) << 12));
}

Action unpackAction(uint16_t bits) {
    Action action;
    if (bits & 0x8000u) {
        action.resolve = true;
        action.won     = (bits & 1u) != 0;
        return action;
    }
    action.move.from      = bits & 63;
    action.move.to        = (bits >> 6) & 63;
    action.move.promotion = static_cast<Chess::PieceType>((bits >> 12) & 7);
    return action;
}

void netEncode(const SeatState& seat, BitWriter& out) {
    out.u16(seat.player);
    const size_t length = std::min(seat.name.size(), MAX_NAME);
    out.bits(static_cast<uint32_t>(length), NAME_BITS);
    for (size_t i = 0; i < length; ++i) out.u8(static_cast<uint8_t>(seat.name[i]));
    out.boolean(seat.side != 0);
    out.u8(seat.hue);
    for (const uint8_t b : seat.looks) out.u8(b);
    out.u8(seat.ack);
}

void netDecode(SeatState& seat, BitReader& in) {
    seat.player = in.u16();
    const uint32_t length = std::min<uint32_t>(in.bits(NAME_BITS), MAX_NAME);
    seat.name.clear();
    for (uint32_t i = 0; i < length; ++i) {
        const char c = static_cast<char>(in.u8());
        seat.name += (c >= 32 && c < 127) ? c : '?';
    }
    seat.side = in.boolean() ? 1 : 0;
    seat.hue  = in.u8();
    for (uint8_t& b : seat.looks) b = in.u8();
    seat.ack = in.u8();
}

void netEncode(const MatchState& match, BitWriter& out) {
    out.bits(match.phase, 2);
    out.u16(match.host);
    out.u8(match.turnSeconds);
    out.u8(match.duelSeconds);
    out.u16(match.game);
    out.bits(match.players, 4);
    for (uint8_t i = 0; i < match.players && i < SEATS; ++i) out.bits(match.order[i], 3);
    out.u16(match.actions);
    // Only the actions a client could still need: the last HISTORY, fewer early on.
    const uint16_t kept = static_cast<uint16_t>(std::min<size_t>(match.actions, HISTORY));
    for (uint16_t i = 0; i < kept; ++i) out.u16(match.log[(match.actions - kept + i) % HISTORY]);
    out.u8(match.duel);
    out.u16(match.stops[0]);
    out.u16(match.stops[1]);
}

void netDecode(MatchState& match, BitReader& in) {
    match.phase       = static_cast<uint8_t>(in.bits(2));
    match.host        = in.u16();
    match.turnSeconds = in.u8();
    match.duelSeconds = in.u8();
    match.game        = in.u16();
    match.players     = static_cast<uint8_t>(std::min<uint32_t>(in.bits(4), SEATS));
    for (uint8_t i = 0; i < match.players; ++i) match.order[i] = static_cast<uint8_t>(in.bits(3));
    match.actions = in.u16();
    const uint16_t kept = static_cast<uint16_t>(std::min<size_t>(match.actions, HISTORY));
    for (uint16_t i = 0; i < kept; ++i) match.log[(match.actions - kept + i) % HISTORY] = in.u16();
    match.duel     = in.u8();
    match.stops[0] = in.u16();
    match.stops[1] = in.u16();
}

std::array<uint8_t, 13> packLooks(const Profile& p) {
    return {
        static_cast<uint8_t>(p.pieces.finish), toByte(p.pieces.hue, 0.0f, 1.0f), toByte(p.pieces.saturation, 0.0f, 1.0f),
        toByte(p.pieces.shine, 0.0f, 1.0f), toByte(p.pieces.glow, 0.0f, 1.0f),
        static_cast<uint8_t>(p.take.style), toByte(p.take.speed, 0.5f, 2.0f), toByte(p.take.height, 0.3f, 2.5f),
        static_cast<uint8_t>(std::lround(p.take.spins)), toByte(p.take.hue, 0.0f, 1.0f),
        static_cast<uint8_t>(p.claim.style), toByte(p.claim.speed, 0.5f, 2.0f), toByte(p.claim.hue, 0.0f, 1.0f),
    };
}

void unpackLooks(const std::array<uint8_t, 13>& b, PieceLook& pieces, TakeLook& take, ClaimLook& claim) {
    pieces.finish     = static_cast<Finish>(std::min<int>(b[0], static_cast<int>(Finish::Count) - 1));
    pieces.hue        = fromByte(b[1], 0.0f, 1.0f);
    pieces.saturation = fromByte(b[2], 0.0f, 1.0f);
    pieces.shine      = fromByte(b[3], 0.0f, 1.0f);
    pieces.glow       = fromByte(b[4], 0.0f, 1.0f);
    take.style        = static_cast<TakeStyle>(std::min<int>(b[5], static_cast<int>(TakeStyle::Count) - 1));
    take.speed        = fromByte(b[6], 0.5f, 2.0f);
    take.height       = fromByte(b[7], 0.3f, 2.5f);
    take.spins        = static_cast<float>(std::min<int>(b[8], 4));
    take.hue          = fromByte(b[9], 0.0f, 1.0f);
    claim.style       = static_cast<ClaimStyle>(std::min<int>(b[10], static_cast<int>(ClaimStyle::Count) - 1));
    claim.speed       = fromByte(b[11], 0.5f, 2.0f);
    claim.hue         = fromByte(b[12], 0.0f, 1.0f);
}

void registerNetTypes() {
    NetSchema::get().replicate<SeatState>("SeatState");
    NetSchema::get().replicate<MatchState>("MatchState");
}

} // namespace Game
