// Every claim style starts as the piece was and ends whole, on its square, facing as it did, in
// its new skin.

#include <cmath>
#include <cstdio>

#include <glm/gtc/constants.hpp>

#include "claims.h"

using namespace Game;

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-66s %s\n", what, ok ? "ok" : "<-- FAILED");
}

bool wholeTurns(float spin) {
    const float turns = spin / glm::two_pi<float>();
    return std::abs(turns - std::round(turns)) < 1e-3f;
}

} // namespace

int main() {
    for (int i = 0; i < static_cast<int>(ClaimStyle::Count); ++i) {
        const ClaimStyle style = static_cast<ClaimStyle>(i);
        std::printf("%s:\n", claimStyleName(style));
        const ClaimPose start = claimPose(style, 0.0f);
        const ClaimPose end   = claimPose(style, 1.0f);
        check("starts on its square, whole", start.lift == 0.0f && std::abs(start.scale - 1.0f) < 1e-4f);
        check("ends on its square, whole", end.lift == 0.0f && end.scale == 1.0f && end.ring == 0.0f);
        check("  facing as it did", wholeTurns(end.spin));
        check("  in its new skin, the flash gone", end.swapped && !end.flash);
        check("takes a moment, or none", claimSeconds(style) > 0.0f && claimSeconds(style) < 2.0f);
        bool once = true;  // the skin changes once, and never back
        bool was  = false;
        for (int s = 0; s <= 200; ++s) {
            const ClaimPose pose = claimPose(style, static_cast<float>(s) / 200.0f);
            once = once && !(was && !pose.swapped) && pose.scale > 0.0f && pose.lift >= 0.0f;
            was  = pose.swapped;
        }
        check("changes skin once and stays changed", once);
    }
    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL OK\n");
    return 0;
}
