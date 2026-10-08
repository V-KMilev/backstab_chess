#pragma once

#include "resource/asset/texture_asset.h"

// Surfaces made in code, so the table and the sea's tiles have grain and veins to catch the
// light rather than a flat colour. Each repeats seamlessly.
namespace Game::Textures {

using namespace Vkm::Engine;

/// Walnut planks running along U, eight across: colour (sRGB).
TextureAsset walnutColor();

/// Their normal map: the grain, and a groove between planks.
TextureAsset walnutNormal();

/// Polished stone with veins: white marble, or black stone veined faintly pale. Colour (sRGB).
TextureAsset marbleColor(bool white);

/// The stone's normal map: a faint unevenness, the veins a hair lower.
TextureAsset marbleNormal();

/// The sea's colour: deep water with streaks of foam drawn out along the wind (U). sRGB.
TextureAsset seaColor();

/// Its occlusion, roughness and metal, packed: glassy water, rough foam.
TextureAsset seaSurface();

} // namespace Game::Textures
