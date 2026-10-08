// Every take style starts where the piece stood and ends upright, whole and facing as it did,
// in its place in the trophy row.

#include <cmath>
#include <cstdio>

#include <glm/gtc/constants.hpp>

#include "takes.h"

using namespace Game;

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-66s %s\n", what, ok ? "ok" : "<-- FAILED");
}

bool near(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 1e-3f; }

// A whole number of turns, so the piece faces as it did.
bool wholeTurns(float spin) {
    const float turns = spin / glm::two_pi<float>();
    return std::abs(turns - std::round(turns)) < 1e-3f;
}

} // namespace

int main() {
    const glm::vec3 from = {1.0f, 0.17f, -0.5f};
    const glm::vec3 to   = {-3.0f, 0.0f, -3.4f};
    for (int i = 0; i < static_cast<int>(TakeStyle::Count) * 2; ++i) {
        const TakeStyle style = static_cast<TakeStyle>(i % static_cast<int>(TakeStyle::Count));
        // Each style as it comes, and as a player might shape it: higher, turning three times.
        const TakeShape shape = i < static_cast<int>(TakeStyle::Count) ? TakeShape{} : TakeShape{2.2f, 3};
        std::printf("%s%s:\n", takeStyleName(style), shape.turns == 3 ? ", shaped" : "");
        const TakePose start = takePose(style, 0.0f, from, to, shape);
        const TakePose end   = takePose(style, 1.0f, from, to, shape);
        check("starts on its square", near(start.position, from));
        check("  whole", near(start.scale, glm::vec3(1.0f)));
        check("ends in the trophy row", near(end.position, to));
        check("  whole", near(end.scale, glm::vec3(1.0f)));
        check("  facing as it did", wholeTurns(end.spin));
        check("  with its effect gone", !end.showEffect);
        check("takes a moment", takeSeconds(style) > 0.5f && takeSeconds(style) < 2.5f);
        check("begins within the attacker's move", takeStart(style) > 0.0f && takeStart(style) <= 1.0f);

        // Never below the board on its own square, but sinking, and never a negative size.
        bool sane = true;
        for (int s = 0; s <= 200; ++s) {
            const TakePose pose = takePose(style, static_cast<float>(s) / 200.0f, from, to, shape);
            sane = sane && pose.scale.x >= 0.0f && pose.scale.y >= 0.0f && pose.scale.z >= 0.0f;
            sane = sane && std::isfinite(pose.position.x) && std::isfinite(pose.position.y);
        }
        check("never a negative or broken pose", sane);
    }
    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL OK\n");
    return 0;
}
