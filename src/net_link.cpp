#define VKM_LOG_CATEGORY "NET"

#include "net_link.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "core/clock.h"
#include "core/math/random.h"
#include "io/project_paths.h"
#include "net/net_session.h"

#include "chess_game.h"
#include "menu.h"
#include "needle.h"

#if defined(_WIN32)
#include "platform/windows_api.h"
#endif

namespace Game {

namespace {

constexpr float TURN_GRACE = 5.0f;  ///< Seconds a turn has past its own, for the moves to be seen.
constexpr float DUEL_GRACE = 2.5f;  ///< Past a duel's own, for a client's later start and its last stop.

// The engine's own folder, where vkm_server sits beside the runtime.
std::filesystem::path engineBin() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#else
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
#endif
}

// The legal move @p wanted names, with what kind of move it is, or nothing.
std::optional<Chess::Move> legal(const Chess::Position& position, const Chess::Move& wanted) {
    for (const Chess::Move& move : position.legalMoves()) {
        if (move.from == wanted.from && move.to == wanted.to && move.promotion == wanted.promotion) return move;
    }
    return std::nullopt;
}

} // namespace

void NetLink::onStart() { m_server = net().role() == NetRole::Server; }

bool NetLink::online() { return !m_server && net().role() == NetRole::Client && net().isPlaying(); }

uint16_t NetLink::localPlayer() { return net().localPlayer(); }

const MatchState* NetLink::match() { return scene().tryGet<MatchState>(matchEntity); }

bool NetLink::isHost() {
    const MatchState* state = match();
    return online() && state && state->host == localPlayer();
}

std::vector<std::pair<int, SeatState>> NetLink::seats() {
    std::vector<std::pair<int, SeatState>> out;
    for (int i = 0; i < SEATS; ++i) {
        const SeatState* seat = scene().tryGet<SeatState>(seatEntities[static_cast<size_t>(i)]);
        if (seat && seat->player) out.emplace_back(i, *seat);
    }
    return out;
}

// --- The client ---------------------------------------------------------------------------

void NetLink::host(uint16_t port) {
    leave();
    const std::filesystem::path server = engineBin() / (
#if defined(_WIN32)
        "vkm_server.exe"
#else
        "vkm_server"
#endif
    );
    m_serverProcess = std::make_unique<ChildProcess>();
    if (!m_serverProcess->start(server, {ProjectPaths::projectRoot().string(), "--port", std::to_string(port)})) {
        m_status = "Could not start a server (" + server.string() + ")";
        m_serverProcess.reset();
        return;
    }
    LOG_INFO("Hosting: started %s on port %u", server.string().c_str(), port);
    m_address   = "127.0.0.1:" + std::to_string(port);
    m_connectIn = 2.0f;  // the server builds its world first
    m_tries     = 0;
    m_connecting = true;
    m_status    = "Starting the server...";
}

bool NetLink::join(const std::string& address) {
    if (!m_serverProcess) leave();
    m_address    = address;
    m_tries      = 0;
    m_connectIn  = 0.0f;
    m_connecting = true;
    m_status     = "Joining " + address + "...";
    return true;
}

void NetLink::leave() {
    if (net().role() == NetRole::Client || net().isDisconnected()) net().close();
    if (m_serverProcess) m_serverProcess->stop();
    m_serverProcess.reset();
    m_serverLog.clear();
    m_outbox.clear();
    input().setPayload(nullptr, 0);
    m_connecting = false;
    m_welcomed   = false;
    m_connectIn  = -1.0f;
    m_lastGame   = 0;
    m_applied    = 0;
    m_phase      = 0;
    m_status.clear();
}

void NetLink::queue(Message kind, std::vector<uint8_t> data) {
    m_nonce = static_cast<uint8_t>(m_nonce % 255 + 1);  // never 0: that is "nothing yet"
    Outgoing out;
    out.nonce = m_nonce;
    out.bytes = {m_nonce, static_cast<uint8_t>(kind)};
    out.bytes.insert(out.bytes.end(), data.begin(), data.end());
    out.bytes.resize(std::min<size_t>(out.bytes.size(), MAX_COMMAND_PAYLOAD));
    m_outbox.push_back(std::move(out));
}

void NetLink::sendProfile() {
    const Profile& you = localProfile();
    std::vector<uint8_t> name(you.name.begin(), you.name.end());
    name.resize(std::min<size_t>(name.size(), 14));
    queue(Message::Name, name);
    std::vector<uint8_t> looks = {static_cast<uint8_t>(std::lround(std::clamp(you.hue, 0.0f, 1.0f) * 255.0f))};
    const std::array<uint8_t, 13> packed = packLooks(you);
    looks.insert(looks.end(), packed.begin(), packed.end());
    queue(Message::Looks, looks);
}

void NetLink::sendSide(int side) { queue(Message::Side, {static_cast<uint8_t>(side & 1)}); }

void NetLink::sendStart(int turnSeconds, int duelSeconds) {
    queue(Message::Start, {static_cast<uint8_t>(std::clamp(turnSeconds, 10, 250)), static_cast<uint8_t>(std::clamp(duelSeconds, 2, 30))});
}

void NetLink::sendMove(const Chess::Move& move) {
    queue(Message::Move, {static_cast<uint8_t>(move.from), static_cast<uint8_t>(move.to), static_cast<uint8_t>(move.promotion)});
}

void NetLink::sendStop(uint8_t duel, float seconds) {
    const uint16_t cs = static_cast<uint16_t>(std::clamp(std::lround(seconds * 100.0f), 0L, 60000L));
    queue(Message::Stop, {duel, static_cast<uint8_t>(cs >> 8), static_cast<uint8_t>(cs & 0xFF)});
}

void NetLink::sendAgain() { queue(Message::Again, {}); }

// The message at the front of the outbox rides every command until the seat shows it applied.
void NetLink::pump() {
    const SeatState* me = scene().tryGet<SeatState>(net().localEntity());
    while (me && !m_outbox.empty() && me->ack == m_outbox.front().nonce) m_outbox.pop_front();
    if (m_outbox.empty()) {
        input().setPayload(nullptr, 0);
        return;
    }
    input().setPayload(m_outbox.front().bytes.data(), m_outbox.front().bytes.size());
}

void NetLink::onUpdate(float dt) {
    if (m_server) return;

    // The server's own log, drained so it does not grow: its errors are repeated here.
    if (m_serverProcess) {
        m_serverLog += m_serverProcess->takeOutput();
        size_t end = 0;
        while ((end = m_serverLog.find('\n')) != std::string::npos) {
            const std::string line = m_serverLog.substr(0, end);
            if (line.find("[ERROR]") != std::string::npos) LOG_WARNING("Server: %s", line.c_str());
            m_serverLog.erase(0, end + 1);
        }
    }

    if (m_connectIn >= 0.0f) {
        m_connectIn -= dt;
        if (m_connectIn < 0.0f) {
            const NetAddress address = NetAddress::parse(m_address, 27750);
            const uint32_t   rate    = static_cast<uint32_t>(std::lround(1.0f / std::max(clock().getFixedStep(), 1e-4f)));
            if (address.port == 0 || !net().connect(address, rate)) {
                m_status     = "'" + m_address + "' is not an address to join";
                m_connecting = false;
            }
        }
    }
    if (m_connecting && net().isPlaying()) {
        m_connecting = false;
        m_status     = "Joined";
        if (!m_welcomed) {
            m_welcomed = true;
            sendProfile();
        }
        if (m_menu) m_menu->onlineJoined();
    }
    if (m_connecting && net().isDisconnected()) {
        // A server just started may not be listening yet: try again a few times.
        if (m_serverProcess && ++m_tries < 6) {
            net().close();
            m_connectIn = 1.0f;
            m_status    = "Waiting for the server...";
        } else {
            const std::string why = net().lastError();
            m_status = why.find("declined") != std::string::npos ? "That table is full, or in the middle of a match"
                                                                  : "Could not join: " + why;
            m_connecting = false;
            net().close();
            if (m_serverProcess) m_serverProcess->stop();
            m_serverProcess.reset();
            if (m_menu) m_menu->onlineFailed();
        }
    }
    if (m_welcomed && !m_connecting && net().isDisconnected()) {
        const std::string why = net().lastError();
        leave();
        m_status = "Left the game: " + (why.empty() ? std::string("the connection was lost") : why);
        if (m_game) m_game->reset();
        if (m_menu) m_menu->onlineFailed();
        return;
    }
    if (online()) {
        pump();
        follow(dt);
    }
}

// The menu and the game, from what the server replicates.
void NetLink::follow(float) {
    const MatchState* state = match();
    if (!state) return;

    // The seats, compared byte for byte with last frame's.
    std::vector<uint8_t> now;
    for (int i = 0; i < SEATS; ++i) {
        const SeatState* seat = scene().tryGet<SeatState>(seatEntities[static_cast<size_t>(i)]);
        if (!seat) continue;
        now.push_back(static_cast<uint8_t>(seat->player));
        now.push_back(seat->side);
        now.push_back(seat->hue);
        now.insert(now.end(), seat->name.begin(), seat->name.end());
        now.insert(now.end(), seat->looks.begin(), seat->looks.end());
    }
    now.push_back(static_cast<uint8_t>(state->host));
    now.push_back(state->phase);
    if (now != m_lastSeats) {
        m_lastSeats = now;
        ++m_seatsVersion;
    }

    if (!m_game) return;

    // A new match: its players from their seats, in the server's order, and the game begun.
    if (state->phase >= 1 && state->game != m_lastGame) {
        std::vector<PlayerSetup> players;
        for (uint8_t p = 0; p < state->players; ++p) {
            const SeatState* seat = scene().tryGet<SeatState>(seatEntities[state->order[p]]);
            PlayerSetup setup;
            if (seat) {
                setup.name  = seat->name.empty() ? "Player" : seat->name;
                setup.side  = seat->side ? Chess::Color::Black : Chess::Color::White;
                setup.color = hsv(static_cast<float>(seat->hue) / 255.0f, 0.68f, 1.0f);
                setup.local = seat->player == localPlayer();
                unpackLooks(seat->looks, setup.pieces, setup.take, setup.claim);
            }
            players.push_back(setup);
        }
        m_game->reset();
        m_game->turnSeconds = state->turnSeconds;
        m_game->duelSeconds = state->duelSeconds;
        m_game->setRemote({[this](const Chess::Move& move) { sendMove(move); },
                           [this](uint8_t duel, float seconds) { sendStop(duel, seconds); }});
        m_game->setPlayers(players);
        m_game->begin();
        m_applied   = 0;
        m_shownStops = {0, 0};
        m_lastGame = state->game;
        if (m_menu) m_menu->onlineGameStarted();
    }

    if (state->phase >= 1 && state->game == m_lastGame) {
        // The other duelist's stop, once it is in, for the duel the game has open.
        if (m_game->duelsOpened() == state->duel) {
            for (int i = 0; i < 2; ++i) {
                if (state->stops[static_cast<size_t>(i)] != NO_STOP && !m_shownStops[static_cast<size_t>(i)]) {
                    m_game->applyStop(i, static_cast<float>(state->stops[static_cast<size_t>(i)]) / 100.0f);
                    m_shownStops[static_cast<size_t>(i)] = 1;
                }
            }
        } else {
            m_shownStops = {0, 0};
        }
        // Every action played since, through the same rules.
        while (m_applied < state->actions) {
            if (state->actions - m_applied > HISTORY) {
                LOG_WARNING("Fell %u actions behind the match; it cannot be replayed", state->actions - m_applied);
                m_applied = state->actions;
                break;
            }
            m_game->applyAction(unpackAction(state->log[m_applied % HISTORY]));
            ++m_applied;
            m_shownStops = {0, 0};
        }
    }

    // Back to the lobby, for another.
    if (m_phase != 0 && state->phase == 0) {
        m_game->reset();
        m_game->clearRemote();
        m_lastGame = 0;
        if (m_menu) m_menu->onlineLobby();
    }
    m_phase = state->phase;
}

// --- The server ----------------------------------------------------------------------------

void NetLink::onFixedUpdate(float dt) {
    if (!m_server) return;
    for (int i = 0; i < SEATS; ++i) {
        SeatState* seat = scene().tryGet<SeatState>(seatEntities[static_cast<size_t>(i)]);
        if (!seat) continue;
        if (seat->player != m_seatPlayer[static_cast<size_t>(i)]) {
            // A new player in the seat: their messages count from the start.
            m_seatPlayer[static_cast<size_t>(i)] = seat->player;
            m_lastNonce[static_cast<size_t>(i)]  = 0;
        }
        if (!seat->player) continue;
        const InputCommand& command = net().commandFor(seatEntities[static_cast<size_t>(i)]);
        if (command.payloadSize < 2) continue;
        const uint8_t nonce = command.payload[0];
        if (nonce == 0 || nonce == m_lastNonce[static_cast<size_t>(i)]) continue;
        m_lastNonce[static_cast<size_t>(i)] = nonce;
        apply(i, static_cast<Message>(command.payload[1]), command.payload.data() + 2, command.payloadSize - 2u);
        seat->ack = nonce;
    }
    serve(dt);
}

void NetLink::apply(int index, Message kind, const uint8_t* data, size_t size) {
    SeatState*  seat  = scene().tryGet<SeatState>(seatEntities[static_cast<size_t>(index)]);
    MatchState* state = scene().tryGet<MatchState>(matchEntity);
    if (!seat || !state) return;

    switch (kind) {
        case Message::Name: {
            std::string name;
            for (size_t i = 0; i < size; ++i) {
                if (data[i] >= 32 && data[i] < 127) name += static_cast<char>(data[i]);
            }
            if (name.empty()) break;
            // Told apart from anyone else of that name.
            std::string unique = name;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const auto& [other, them] : seats()) taken = taken || (other != index && them.name == unique);
                if (!taken) break;
                unique = name.substr(0, 12) + " " + std::to_string(n);
            }
            seat->name = unique;
            break;
        }
        case Message::Looks:
            if (size >= 14) {
                seat->hue = data[0];
                std::copy(data + 1, data + 14, seat->looks.begin());
            }
            break;
        case Message::Side: {
            if (state->phase != 0 || size < 1) break;
            const uint8_t side  = data[0] & 1;
            int           there = 0;
            for (const auto& [_, other] : seats()) there += other.side == side ? 1 : 0;
            if (there < SEATS / 2) seat->side = side;
            break;
        }
        case Message::Start:
            if (state->phase == 0 && seat->player == state->host && size >= 2) startMatch(data[0], data[1]);
            break;
        case Message::Move: {
            if (state->phase != 1 || !m_match || m_match->duel() || size < 3) break;
            const int player = static_cast<int>(std::find(state->order.begin(), state->order.begin() + state->players, index) - state->order.begin());
            if (player != m_match->current()) break;
            Chess::Move wanted;
            wanted.from      = data[0] & 63;
            wanted.to        = data[1] & 63;
            wanted.promotion = static_cast<Chess::PieceType>(data[2] & 7);
            const std::optional<Chess::Move> move = legal(m_match->position(), wanted);
            if (!move || m_match->attempt(*move) == Chess::Attempt::Illegal) break;
            record({false, false, *move});
            m_turnTime = 0.0f;
            if (m_match->duel()) openDuel();
            break;
        }
        case Message::Stop: {
            if (state->phase != 1 || !m_match || !m_match->duel() || size < 3 || data[0] != state->duel) break;
            const Chess::Duel& duel = *m_match->duel();
            const uint16_t     cs   = static_cast<uint16_t>((data[1] << 8) | data[2]);
            if (state->order[static_cast<size_t>(duel.challenger)] == index && state->stops[0] == NO_STOP) state->stops[0] = cs;
            if (state->order[static_cast<size_t>(duel.defender)] == index && state->stops[1] == NO_STOP) state->stops[1] = cs;
            break;
        }
        case Message::Again:
            if (state->phase == 2 && seat->player == state->host) {
                state->phase = 0;
                m_match.reset();
            }
            break;
        default:
            break;
    }
}

void NetLink::startMatch(int turnSeconds, int duelSeconds) {
    MatchState* state = scene().tryGet<MatchState>(matchEntity);
    if (!state) return;
    std::vector<Chess::Player> players;
    uint8_t count = 0;
    int     sides[2] = {0, 0};
    for (const auto& [index, seat] : seats()) {
        players.push_back({seat.name.empty() ? "Player" : seat.name, seat.side ? Chess::Color::Black : Chess::Color::White});
        state->order[count++] = static_cast<uint8_t>(index);
        ++sides[seat.side & 1];
    }
    if (sides[0] == 0 || sides[1] == 0) return;
    m_match.emplace(std::move(players));
    state->players     = count;
    state->turnSeconds = static_cast<uint8_t>(turnSeconds);
    state->duelSeconds = static_cast<uint8_t>(duelSeconds);
    state->phase       = 1;
    state->game        = static_cast<uint16_t>(state->game + 1);
    state->actions     = 0;
    state->duel        = 0;
    state->stops       = {NO_STOP, NO_STOP};
    m_turnTime = 0.0f;
    m_duelTime = -1.0f;
    LOG_INFO("A match begins: %u players", count);
}

void NetLink::record(const Action& action) {
    MatchState* state = scene().tryGet<MatchState>(matchEntity);
    if (!state) return;
    state->log[state->actions % HISTORY] = packAction(action);
    state->actions = static_cast<uint16_t>(state->actions + 1);
}

void NetLink::openDuel() {
    MatchState* state = scene().tryGet<MatchState>(matchEntity);
    if (!state) return;
    state->duel  = static_cast<uint8_t>(state->duel + 1);
    state->stops = {NO_STOP, NO_STOP};
    m_duelTime   = 0.0f;
}

// The match's clock: duels settle when both have stopped or the time is up; a turn not moved
// in time has a move made for it.
void NetLink::serve(float dt) {
    MatchState* state = scene().tryGet<MatchState>(matchEntity);
    if (!state) return;
    // Everyone gone: the table is open again.
    if (state->phase != 0 && seats().empty()) {
        state->phase = 0;
        m_match.reset();
        return;
    }
    if (state->phase != 1 || !m_match) return;
    if (m_match->over()) {
        state->phase = 2;
        return;
    }
    if (m_match->duel()) {
        m_duelTime += dt;
        const bool both = state->stops[0] != NO_STOP && state->stops[1] != NO_STOP;
        if (!both && m_duelTime < static_cast<float>(state->duelSeconds) + DUEL_GRACE) return;
        const auto judged = [](uint16_t stop) { return stop == NO_STOP ? -1.0f : Needle::score(static_cast<float>(stop) / 100.0f); };
        const bool won    = judged(state->stops[0]) > judged(state->stops[1]);
        m_match->resolveDuel(won);
        record({true, won, {}});
        m_turnTime = 0.0f;
        if (m_match->duel()) openDuel();
        else m_duelTime = -1.0f;
        if (m_match->over()) state->phase = 2;
        return;
    }
    m_turnTime += dt;
    if (m_turnTime < static_cast<float>(state->turnSeconds) + TURN_GRACE) return;
    // Out of time: a move that needs no duel where there is one.
    const std::vector<Chess::Move> moves = m_match->position().legalMoves();
    std::vector<Chess::Move>       free;
    for (const Chess::Move& move : moves) {
        if (!m_match->duelFor(move)) free.push_back(move);
    }
    const std::vector<Chess::Move>& pool = free.empty() ? moves : free;
    m_turnTime = 0.0f;
    if (pool.empty()) return;
    const Chess::Move move = pool[static_cast<size_t>(Math::Random::range(0, static_cast<int>(pool.size()) - 1))];
    if (m_match->attempt(move) == Chess::Attempt::Illegal) return;
    record({false, false, move});
    if (m_match->duel()) openDuel();
    if (m_match->over()) state->phase = 2;
}

} // namespace Game
