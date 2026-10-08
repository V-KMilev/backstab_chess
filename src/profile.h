#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "takes.h"

// What a player is and how they like things: their name, colour and the looks they designed for
// their pieces, their takes and their claims; and this machine's settings. Both are kept as JSON
// in the user's config folder, so they last from one game to the next.
namespace Game {

/// What a player's pieces are made of; their side keeps them light or dark.
enum class Finish : int { Wood, Stone, Metal, Glass, Gem, Neon, Count };

const char* finishName(Finish finish);

/// A player's pieces, designed: a finish, tinted, polished and glowing as they like.
struct PieceLook {
    Finish finish     = Finish::Metal;
    float  hue        = 0.11f;  ///< 0..1 round the colour wheel.
    float  saturation = 0.55f;  ///< 0 is no tint, 1 the strongest.
    float  shine      = 0.75f;  ///< 0 matte, 1 mirror.
    float  glow       = 0.0f;   ///< 0 none, 1 bright.
};

/// How the pieces a player takes leave the board.
struct TakeLook {
    TakeStyle style  = TakeStyle::Float;
    float     speed  = 1.0f;   ///< 0.5 slow, 2 fast.
    float     height = 1.0f;   ///< How high it goes, times the style's own.
    float     spins  = 1.0f;   ///< Turns it makes on the way.
    float     hue    = 0.55f;  ///< Its effect's colour.
};

/// How a piece changes into a player's look when they claim it.
enum class ClaimStyle : int { Instant, Flash, Spin, Rise, Wave, Count };

const char* claimStyleName(ClaimStyle style);

struct ClaimLook {
    ClaimStyle style = ClaimStyle::Flash;
    float      speed = 1.0f;   ///< 0.5 slow, 2 fast.
    float      hue   = 0.11f;  ///< The flash's and the wave's colour.
};

/// Who plays at this machine, and how they look.
struct Profile {
    std::string name  = "Player";
    float       hue   = 0.58f;               ///< Their colour, round the wheel; color follows it.
    glm::vec3   color = {0.25f, 0.6f, 1.0f};
    PieceLook   pieces;
    TakeLook    take;
    ClaimLook   claim;
};

/// A key bound to an action, by the action's name.
struct KeyBinding {
    std::string action;
    std::string label;  ///< What the controls tab calls it.
    int         key = 0;
};

/// This machine's settings.
struct Settings {
    // Graphics
    int   quality    = 2;      ///< A preset: 0 low, 1 medium, 2 high, 3 ultra; 4 when changed by hand.
    int   msaa       = 4;      ///< 1, 2, 4 or 8.
    int   shadows    = 4096;   ///< 1024, 2048 or 4096.
    bool  reflections = true;
    bool  occlusion  = true;
    bool  bloom      = true;
    bool  fog        = true;
    int   seaDetail  = 2;      ///< 0 low, 1 medium, 2 high.
    float brightness = 1.0f;   ///< Exposure in stops.
    int   tonemap    = 1;      ///< Into the engine's tonemaps: Reinhard, ACES, Khronos neutral, AgX.
    bool  vsync      = false;
    bool  fullscreen = false;
    // Game
    float turnSeconds  = 60.0f;
    float duelSeconds  = 6.0f;
    float lookSpeed    = 1.0f;
    float flySpeed     = 1.0f;
    bool  showHints    = true;
    // Controls
    std::vector<KeyBinding> keys;
};

/// The keys as the game ships them.
std::vector<KeyBinding> defaultKeys();

/// A quality preset's graphics, written over @p settings' graphics.
void applyPreset(Settings& settings, int quality);

/// A colour from a hue, a saturation and a value, each 0..1.
glm::vec3 hsv(float hue, float saturation, float value);

/// The name of a GLFW key or mouse button code, for showing it.
std::string keyName(int key);

/// Read the profile and the settings from the config folder; defaults for what is missing.
void load(Profile& profile, Settings& settings);

/// Write them back.
void save(const Profile& profile, const Settings& settings);

/// This machine's player, read from the config folder the first time it is asked for.
Profile& localProfile();

/// This machine's settings, read with the profile.
Settings& localSettings();

/// Write the local profile and settings back to the config folder.
void storeLocal();

} // namespace Game
