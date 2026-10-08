#pragma once

#include <cstdint>
#include <vector>

// The turn clock's face: a disc, half sun and half moon, painted pixel by pixel so it can turn
// smoothly to any angle and keep a clean edge.
namespace Game::Dial {

constexpr int SIZE = 176;  ///< Pixels across: twice the dial's size on screen, for a crisp edge.

/**
 * @brief Paint the face into @p rgba, sRGB with straight alpha, SIZE by SIZE.
 *
 * @param rgba  Resized to fit, and filled.
 * @param turn  Radians the face has turned clockwise from the sun's half on top.
 * @param alarm 0..1: how far the rim has gone over to the hurry red, which pulses.
 */
void paint(std::vector<uint8_t>& rgba, float turn, float alarm);

} // namespace Game::Dial
