#pragma once

#include <vector>

#include "resource/asset/mesh_asset.h"

// The swell on the sea round the table: a few long waves rolling through each other, the same
// for the water's surface and for everything afloat on it (which rides the plain sum; the
// surface also leans its crests together, Gerstner's way). It dies away by NEAR_RADIUS, and the
// sea lies still from there to the horizon.
namespace Game::Sea {

using namespace Vkm::Engine;

constexpr float NEAR_RADIUS = 240.0f;  ///< How far the swell, and the moving surface, reach.

/// The swell's height above the sea's level at (@p x, @p z), @p t seconds in.
float height(float x, float z, float t);

/// The surface's normal there.
glm::vec3 normal(float x, float z, float t);

/**
 * @brief The sea, flat: a disc of rings, close together at the table, wider apart out to
 *        NEAR_RADIUS where the swell dies, and a few more, far apart, out to the horizon.
 *
 * @param rest   Filled with each vertex's place on the plane, for shape() to raise.
 * @param detail 0 low, 1 medium, 2 high: how many rings and segments.
 * @return The mesh, to be shaped each frame.
 */
MeshAsset mesh(std::vector<glm::vec2>& rest, int detail = 2);

/**
 * @brief Raise @p mesh to the swell @p t seconds in, and slide its ripples on.
 *
 * @param mesh The near sea, as mesh() made it.
 * @param rest Its vertices' places on the plane.
 * @param t    Seconds since the sea began.
 * @param tile How many metres the ripples' texture repeats over.
 */
void shape(MeshAsset& mesh, const std::vector<glm::vec2>& rest, float t, float tile);

} // namespace Game::Sea
