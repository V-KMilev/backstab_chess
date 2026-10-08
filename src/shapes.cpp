#include "shapes.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Game::Shapes {

MeshAsset ring(float inner, uint32_t segments) {
    MeshAsset       mesh;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);
    for (uint32_t i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float c = std::cos(u * glm::two_pi<float>());
        const float s = std::sin(u * glm::two_pi<float>());
        mesh.vertices.push_back({{c * inner, 0.0f, s * inner}, up, {u, 0.0f}, tangent});
        mesh.vertices.push_back({{c, 0.0f, s}, up, {u, 1.0f}, tangent});
    }
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t a = i * 2;  // inner, then outer, then the next pair
        mesh.indices.insert(mesh.indices.end(), {a, a + 2, a + 3, a, a + 3, a + 1});
    }
    mesh.boundsMin = {-1.0f, -0.001f, -1.0f};
    mesh.boundsMax = {1.0f, 0.001f, 1.0f};
    return mesh;
}

} // namespace Game::Shapes
