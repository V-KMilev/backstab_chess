#pragma once

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/process/child_process.h"
#include "system/script/behavior_api.h"

#include "chess/match.h"
#include "net_state.h"

namespace Game {

using namespace Vkm::Engine;

class ChessGame;
class Menu;

/**
 * @brief Online play, on both ends.
 *
 * On the server it runs the match: it reads each seat's messages from its commands, keeps the
 * lobby, plays the moves the rules allow, judges the duels by the stops sent, and times turns
 * out; and it writes it all into the replicated seat and match state. On a client it hosts
 * (starting a server for this project and joining it) or joins one by address, sends this
 * player's messages until each is applied, and drives the menu and the game from what the
 * server replicates: a client replays the match's actions through the same rules.
 */
class NetLink : public ReflectedBehavior<NetLink> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;

        // Set from vkmBuildScene, before the game starts: the shared entities.
        EntityId                     matchEntity;
        std::array<EntityId, SEATS>  seatEntities{};
        void link(ChessGame* game, Menu* menu) {
            m_game = game;
            m_menu = menu;
        }

        /// Starts a server for this project on @p port and joins it.
        void host(uint16_t port);

        /// Joins the server at @p address, host[:port].
        bool join(const std::string& address);

        /// Leaves any game, stopping the server this machine started.
        void leave();

        bool online();
        bool connecting() const { return m_connecting; }
        bool isHost();
        const std::string& status() const { return m_status; }

        /// The seats as the server has them, and which is this player's.
        std::vector<std::pair<int, SeatState>> seats();
        uint16_t localPlayer();
        const MatchState* match();
        /// Counts up whenever the seats change, so a screen built from them knows to rebuild.
        uint32_t seatsVersion() const { return m_seatsVersion; }

        // What this player says.
        void sendProfile();
        void sendSide(int side);
        void sendStart(int turnSeconds, int duelSeconds);
        void sendMove(const Chess::Move& move);
        void sendStop(uint8_t duel, float seconds);
        void sendAgain();

    private:
        void queue(Message kind, std::vector<uint8_t> data);
        void pump();
        void follow(float dt);

        void serve(float dt);
        void apply(int seat, Message kind, const uint8_t* data, size_t size);
        void startMatch(int turnSeconds, int duelSeconds);
        void record(const Action& action);
        void openDuel();

    private:
        ChessGame* m_game = nullptr;
        Menu*      m_menu = nullptr;
        bool       m_server = false;

        // The client's side.
        std::unique_ptr<ChildProcess> m_serverProcess;
        std::string m_serverLog;              ///< Its output, short of a whole line.
        std::string m_status;
        std::string m_address;
        float       m_connectIn   = -1.0f;  ///< Seconds till joining the server just started.
        int         m_tries       = 0;
        bool        m_connecting  = false;
        bool        m_welcomed    = false;
        struct Outgoing {
            uint8_t              nonce = 0;
            std::vector<uint8_t> bytes;
        };
        std::deque<Outgoing> m_outbox;
        uint8_t              m_nonce = 0;
        uint16_t             m_lastGame = 0; ///< The match being shown.
        uint16_t             m_applied = 0; ///< Its actions replayed so far.
        uint8_t              m_phase = 0;
        uint32_t             m_seatsVersion = 0;
        std::array<uint8_t, 2> m_shownStops{};  ///< Of the current duel, which stops are shown.
        std::vector<uint8_t> m_lastSeats;       ///< To tell when the seats changed.

        // The server's side.
        std::optional<Chess::Match> m_match;
        std::array<uint8_t, SEATS>  m_lastNonce{};
        std::array<uint16_t, SEATS> m_seatPlayer{};  ///< Who each seat's nonces are from.
        float m_turnTime = 0.0f;
        float m_duelTime = -1.0f;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::NetLink)
VKM_REFLECT_END()
