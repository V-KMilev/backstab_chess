#pragma once

#include <array>
#include <vector>

#include "system/script/behavior_api.h"

#include "avatar.h"
#include "chess/position.h"
#include "chess_look.h"

namespace Game {

using namespace Vkm::Engine;

/**
 * @brief One game of chess on the table: the board, its pieces, and the hand that moves them.
 *
 * Click a piece of the side to move to see where it can go, then a square to move it there.
 * WASD flies round the table, Space and Shift up and down, right-drag looks, F sits back down.
 * Keys 1-4 change what the pieces are made of.
 */
class ChessGame : public ReflectedBehavior<ChessGame> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        int   pieceSet    = 0;      ///< A PieceSet.
        float moveSeconds = 0.45f;  ///< How long a piece takes from one square to the next.

    private:
        /// A piece gliding to its square, and what it knocks off the board on arrival.
        struct Glide {
            EntityId         piece;
            glm::vec3        from;
            glm::vec3        to;
            float            t        = 0.0f;
            float            lift     = 0.0f;  ///< How high it arcs, in metres; a knight jumps.
            EntityId         victim;
            Chess::PieceType becomes  = Chess::PieceType::None;
        };

        /// A drawn piece and the side whose material it wears; a bishop is two.
        struct Drawn {
            EntityId     entity;
            Chess::Color side;
        };

    private:
        void spawnTable();
        void spawnPieces();
        EntityId spawnPiece(Chess::Piece piece, Chess::Square square);
        void setMesh(EntityId entity, Chess::PieceType type);
        glm::vec3 squareCentre(Chess::Square square) const;
        Chess::Square squareUnderPointer();
        void click(Chess::Square square);
        void showChoices(Chess::Square square);
        void clearChoices();
        void play(const Chess::Move& move);
        void knockOff(EntityId piece, const glm::vec3& from);
        void advanceGlides(float dt);
        void applySet();
        void updateCamera(float dt);
        void updateAvatars(float dt);
        void updateStatus();

    private:
        Chess::Position m_position = Chess::Position::start();

        std::array<EntityId, 64> m_pieces{};  ///< The piece standing on each square.
        std::vector<Drawn>       m_drawn;     ///< Every piece entity, captured ones too.
        std::vector<EntityId>    m_hints;
        std::vector<Chess::Move> m_choices;
        std::vector<Glide>       m_glides;
        Chess::Square            m_selected = Chess::NO_SQUARE;
        int                      m_shownSet = -1;

        EntityId  m_status;
        glm::vec3 m_eye   = {0.0f, 4.6f, -7.0f};
        float     m_yaw   = glm::pi<float>();
        float     m_pitch = glm::radians(-25.0f);

        Avatar* m_me    = nullptr;
        Avatar* m_ghost = nullptr;  ///< A demo player until there are real ones.
        float   m_ghostTime     = 0.0f;
        int     m_ghostSquare   = 28;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::ChessGame)
    VKM_F(pieceSet)
    VKM_F(moveSeconds)
VKM_REFLECT_END()
