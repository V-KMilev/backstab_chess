#pragma once

#include <cstdint>

#include "resource/asset/mesh_asset.h"

// Meshes the engine's generators do not make.
namespace Game::Shapes {

using namespace Vkm::Engine;

/**
 * @brief A flat ring facing up, its outer edge at radius 1.
 *
 * U runs round it, V from the inner edge (0) to the outer (1).
 *
 * @param inner    The inner edge's radius, 0..1.
 * @param segments How many pieces round.
 * @return The ring.
 */
MeshAsset ring(float inner, uint32_t segments);

} // namespace Game::Shapes
