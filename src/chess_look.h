#pragma once

#include "resource/asset/material_asset.h"
#include "system/script/behavior_api.h"

#include "chess/position.h"

namespace Game {
struct PieceLook;
}

namespace Game {

using namespace Vkm::Engine;

/// How the pieces are made: each set is a white and a black material over the same shapes.
enum class PieceSet : int { Classic, Glass, Metal, Stone, Count };

/// The display name of @p set.
const char* pieceSetName(PieceSet set);

namespace ChessLook {

/**
 * @brief Make every material the game draws with, under its "chess:" name.
 *
 * Call once, after the chess set's textures are loaded; a name made before is made again.
 *
 * @param resources Where the materials go, and where the set's textures are found.
 */
void build(ResourceManager& resources);

/**
 * @brief A side's material in @p set.
 *
 * @param resources Holds the materials build() made.
 * @param set       The set the pieces are drawn in.
 * @param side      Whose pieces.
 * @return The material, or a null handle before build().
 */
MaterialHandle piece(ResourceManager& resources, PieceSet set, Chess::Color side);

MaterialHandle board(ResourceManager& resources);   ///< The wooden board, as the set has it.
MaterialHandle table(ResourceManager& resources);   ///< The lacquered table under it.
MaterialHandle sea(ResourceManager& resources);     ///< The sea the table stands in.
MaterialHandle foam(ResourceManager& resources);    ///< Foam where something afloat meets the water.
MaterialHandle tile(ResourceManager& resources, bool white);  ///< A board's square adrift on it.
MaterialHandle brass(ResourceManager& resources);   ///< The table's rim and foot.
MaterialHandle glow(ResourceManager& resources);       ///< The ring glowing under the table's rim.
MaterialHandle lantern(ResourceManager& resources);    ///< The lanterns adrift round the table.

/**
 * @brief A player's pieces as they designed them, for @p side, under @p name; made again in
 *        place when it exists, so what wears it changes with it.
 */
MaterialHandle designed(ResourceManager& resources, const std::string& name, const PieceLook& look, Chess::Color side);

/// A glowing see-through colour under @p name, made or made again: an effect, a ring, a flash.
MaterialHandle glowing(ResourceManager& resources, const std::string& name, const glm::vec3& color, float alpha, float strength);

/// The name of the mesh a piece of @p type is drawn with; a bishop's ball is chess:bishop_top.
const char* pieceMesh(Chess::PieceType type);
MaterialHandle hint(ResourceManager& resources);    ///< A square a selected piece may move to.
MaterialHandle chosen(ResourceManager& resources);  ///< The selected piece's square.
MaterialHandle duel(ResourceManager& resources);    ///< A square only a duel can reach.

} // namespace ChessLook

} // namespace Game
