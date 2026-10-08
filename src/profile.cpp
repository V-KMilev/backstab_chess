#include "profile.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "system/script/behavior_api.h"

namespace Game {

namespace {

namespace fs = std::filesystem;

// Codes at and past this are mouse buttons, the rest GLFW keys.
constexpr int MOUSE = 1000;

// Where the files live: the user's config folder, under the game's name.
fs::path folder() {
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA")) return fs::path(appData) / "BackstabChess";
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME")) return fs::path(config) / "backstab_chess";
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".config" / "backstab_chess";
#endif
    return fs::current_path() / "backstab_chess";
}

template <typename T>
void read(const nlohmann::json& j, const char* key, T& into) {
    if (j.contains(key)) {
        try {
            into = j.at(key).get<T>();
        } catch (...) {
        }
    }
}

nlohmann::json readFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return nlohmann::json::object();
    try {
        return nlohmann::json::parse(in);
    } catch (...) {
        return nlohmann::json::object();
    }
}

} // namespace

const char* finishName(Finish finish) {
    switch (finish) {
        case Finish::Wood:  return "Wood";
        case Finish::Stone: return "Stone";
        case Finish::Metal: return "Metal";
        case Finish::Glass: return "Glass";
        case Finish::Gem:   return "Gem";
        case Finish::Neon:  return "Neon";
        default:            return "";
    }
}

const char* claimStyleName(ClaimStyle style) {
    switch (style) {
        case ClaimStyle::Instant: return "Instant";
        case ClaimStyle::Flash:   return "Flash";
        case ClaimStyle::Spin:    return "Spin";
        case ClaimStyle::Rise:    return "Rise";
        case ClaimStyle::Wave:    return "Wave";
        default:                  return "";
    }
}

std::vector<KeyBinding> defaultKeys() {
    return {
        {"chess/select", "Pick and move", MOUSE + GLFW_MOUSE_BUTTON_LEFT},
        {"chess/look", "Look around (hold)", MOUSE + GLFW_MOUSE_BUTTON_RIGHT},
        {"chess/forward", "Fly forward", GLFW_KEY_W},
        {"chess/back", "Fly back", GLFW_KEY_S},
        {"chess/left", "Fly left", GLFW_KEY_A},
        {"chess/right", "Fly right", GLFW_KEY_D},
        {"chess/up", "Fly up", GLFW_KEY_SPACE},
        {"chess/down", "Fly down", GLFW_KEY_LEFT_SHIFT},
        {"chess/seat", "Back to your seat", GLFW_KEY_F},
        {"chess/duel", "Duel: stop the needle", GLFW_KEY_SPACE},
    };
}

void applyPreset(Settings& s, int quality) {
    s.quality = quality;
    switch (quality) {
        case 0: s.msaa = 1; s.shadows = 1024; s.reflections = false; s.occlusion = false; s.bloom = false; s.fog = false; s.seaDetail = 0; break;
        case 1: s.msaa = 2; s.shadows = 2048; s.reflections = false; s.occlusion = true; s.bloom = true; s.fog = true; s.seaDetail = 1; break;
        case 2: s.msaa = 4; s.shadows = 4096; s.reflections = true; s.occlusion = true; s.bloom = true; s.fog = true; s.seaDetail = 2; break;
        default: s.msaa = 8; s.shadows = 4096; s.reflections = true; s.occlusion = true; s.bloom = true; s.fog = true; s.seaDetail = 2; break;
    }
}

glm::vec3 hsv(float hue, float saturation, float value) {
    const glm::vec3 k = glm::vec3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f);
    const glm::vec3 p = glm::abs(glm::fract(glm::vec3(hue) + k) * 6.0f - glm::vec3(3.0f));
    return value * glm::mix(glm::vec3(1.0f), glm::clamp(p - glm::vec3(1.0f), 0.0f, 1.0f), saturation);
}

std::string keyName(int key) {
    if (key >= MOUSE) {
        switch (key - MOUSE) {
            case GLFW_MOUSE_BUTTON_LEFT:   return "Left mouse";
            case GLFW_MOUSE_BUTTON_RIGHT:  return "Right mouse";
            case GLFW_MOUSE_BUTTON_MIDDLE: return "Middle mouse";
            default:                       return "Mouse " + std::to_string(key - MOUSE + 1);
        }
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return std::string(1, static_cast<char>('A' + key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return std::string(1, static_cast<char>('0' + key - GLFW_KEY_0));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    switch (key) {
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_LEFT_SHIFT:    return "Left Shift";
        case GLFW_KEY_RIGHT_SHIFT:   return "Right Shift";
        case GLFW_KEY_LEFT_CONTROL:  return "Left Ctrl";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_LEFT_ALT:      return "Left Alt";
        case GLFW_KEY_RIGHT_ALT:     return "Right Alt";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_DOWN:          return "Down";
        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_Q:             return "Q";
        case GLFW_KEY_E:             return "E";
        case GLFW_KEY_CAPS_LOCK:     return "Caps Lock";
        default:                     return "Key " + std::to_string(key);
    }
}

void load(Profile& profile, Settings& settings) {
    settings.keys = defaultKeys();
    const nlohmann::json p = readFile(folder() / "profile.json");
    read(p, "name", profile.name);
    read(p, "hue", profile.hue);
    profile.color = hsv(profile.hue, 0.68f, 1.0f);
    if (p.contains("pieces")) {
        const auto& j = p["pieces"];
        int finish = static_cast<int>(profile.pieces.finish);
        read(j, "finish", finish);
        profile.pieces.finish = static_cast<Finish>(std::clamp(finish, 0, static_cast<int>(Finish::Count) - 1));
        read(j, "hue", profile.pieces.hue);
        read(j, "saturation", profile.pieces.saturation);
        read(j, "shine", profile.pieces.shine);
        read(j, "glow", profile.pieces.glow);
    }
    if (p.contains("take")) {
        const auto& j = p["take"];
        int style = static_cast<int>(profile.take.style);
        read(j, "style", style);
        profile.take.style = static_cast<TakeStyle>(std::clamp(style, 0, static_cast<int>(TakeStyle::Count) - 1));
        read(j, "speed", profile.take.speed);
        read(j, "height", profile.take.height);
        read(j, "spins", profile.take.spins);
        read(j, "hue", profile.take.hue);
    }
    if (p.contains("claim")) {
        const auto& j = p["claim"];
        int style = static_cast<int>(profile.claim.style);
        read(j, "style", style);
        profile.claim.style = static_cast<ClaimStyle>(std::clamp(style, 0, static_cast<int>(ClaimStyle::Count) - 1));
        read(j, "speed", profile.claim.speed);
        read(j, "hue", profile.claim.hue);
    }

    const nlohmann::json s = readFile(folder() / "settings.json");
    read(s, "quality", settings.quality);
    read(s, "msaa", settings.msaa);
    read(s, "shadows", settings.shadows);
    read(s, "reflections", settings.reflections);
    read(s, "occlusion", settings.occlusion);
    read(s, "bloom", settings.bloom);
    read(s, "fog", settings.fog);
    read(s, "seaDetail", settings.seaDetail);
    read(s, "brightness", settings.brightness);
    read(s, "tonemap", settings.tonemap);
    read(s, "vsync", settings.vsync);
    read(s, "fullscreen", settings.fullscreen);
    read(s, "turnSeconds", settings.turnSeconds);
    read(s, "duelSeconds", settings.duelSeconds);
    read(s, "lookSpeed", settings.lookSpeed);
    read(s, "flySpeed", settings.flySpeed);
    read(s, "showHints", settings.showHints);
    read(s, "hostPort", settings.hostPort);
    read(s, "joinAddress", settings.joinAddress);
    if (s.contains("keys") && s["keys"].is_object()) {
        for (KeyBinding& binding : settings.keys) read(s["keys"], binding.action.c_str(), binding.key);
    }
}

namespace {

struct Local {
    Profile  profile;
    Settings settings;
    Local() { load(profile, settings); }
};

Local& local() {
    static Local held;
    return held;
}

} // namespace

Profile&  localProfile() { return local().profile; }
Settings& localSettings() { return local().settings; }
void      storeLocal() { save(local().profile, local().settings); }

void save(const Profile& profile, const Settings& settings) {
    std::error_code error;
    fs::create_directories(folder(), error);

    nlohmann::json p;
    p["name"]   = profile.name;
    p["hue"]    = profile.hue;
    p["pieces"] = {{"finish", static_cast<int>(profile.pieces.finish)}, {"hue", profile.pieces.hue},
                   {"saturation", profile.pieces.saturation}, {"shine", profile.pieces.shine}, {"glow", profile.pieces.glow}};
    p["take"]   = {{"style", static_cast<int>(profile.take.style)}, {"speed", profile.take.speed}, {"height", profile.take.height},
                   {"spins", profile.take.spins}, {"hue", profile.take.hue}};
    p["claim"]  = {{"style", static_cast<int>(profile.claim.style)}, {"speed", profile.claim.speed}, {"hue", profile.claim.hue}};
    std::ofstream(folder() / "profile.json") << p.dump(2);

    nlohmann::json s;
    s["quality"]     = settings.quality;
    s["msaa"]        = settings.msaa;
    s["shadows"]     = settings.shadows;
    s["reflections"] = settings.reflections;
    s["occlusion"]   = settings.occlusion;
    s["bloom"]       = settings.bloom;
    s["fog"]         = settings.fog;
    s["seaDetail"]   = settings.seaDetail;
    s["brightness"]  = settings.brightness;
    s["tonemap"]     = settings.tonemap;
    s["vsync"]       = settings.vsync;
    s["fullscreen"]  = settings.fullscreen;
    s["turnSeconds"] = settings.turnSeconds;
    s["duelSeconds"] = settings.duelSeconds;
    s["lookSpeed"]   = settings.lookSpeed;
    s["flySpeed"]    = settings.flySpeed;
    s["showHints"]   = settings.showHints;
    s["hostPort"]    = settings.hostPort;
    s["joinAddress"] = settings.joinAddress;
    for (const KeyBinding& binding : settings.keys) s["keys"][binding.action] = binding.key;
    std::ofstream(folder() / "settings.json") << s.dump(2);
}

} // namespace Game
