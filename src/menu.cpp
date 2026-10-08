#include "menu.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iterator>

#include "core/math/random.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_image.h"
#include "platform/window/window_manager.h"
#include "system/render/render_settings.h"
#include "system/ui/ui_events.h"

#include "net/net_session.h"
#include "net_link.h"
#include "scenery.h"
#include "showcase.h"

namespace Game {

namespace {

using Chess::Color;
using Ui::Kit;
using Ui::Style;

constexpr int   MAX_PER_SIDE   = 4;
const float     TURN_CHOICES[] = {30.0f, 45.0f, 60.0f, 90.0f, 120.0f};
const float     DUEL_CHOICES[] = {4.0f, 6.0f, 8.0f, 10.0f};
const int       MSAA_CHOICES[] = {1, 2, 4, 8};
const int       SHADOW_CHOICES[] = {1024, 2048, 4096};
constexpr const char* ACTION_BACK = "menu/back";

const glm::vec3 PALETTE[] = {
    {0.25f, 0.6f, 1.0f},  {0.95f, 0.35f, 0.75f}, {0.35f, 0.85f, 0.4f}, {1.0f, 0.55f, 0.15f},
    {0.65f, 0.4f, 1.0f},  {1.0f, 0.85f, 0.2f},   {0.2f, 0.85f, 0.85f}, {0.95f, 0.25f, 0.25f},
};

// The index of @p value in @p options, or the nearest before it.
template <typename T, size_t N>
int indexOf(const T (&options)[N], T value) {
    for (size_t i = 0; i < N; ++i) {
        if (options[i] == value) return static_cast<int>(i);
    }
    return 0;
}

template <typename T, size_t N>
std::vector<std::string> labels(const T (&options)[N], const char* unit) {
    std::vector<std::string> out;
    for (const T& o : options) out.push_back(std::to_string(static_cast<int>(o)) + unit);
    return out;
}

std::vector<std::string> finishNames() {
    std::vector<std::string> out;
    for (int i = 0; i < static_cast<int>(Finish::Count); ++i) out.push_back(finishName(static_cast<Finish>(i)));
    return out;
}

std::vector<std::string> takeNames() {
    std::vector<std::string> out;
    for (int i = 0; i < static_cast<int>(TakeStyle::Count); ++i) out.push_back(takeStyleName(static_cast<TakeStyle>(i)));
    return out;
}

std::vector<std::string> claimNames() {
    std::vector<std::string> out;
    for (int i = 0; i < static_cast<int>(ClaimStyle::Count); ++i) out.push_back(claimStyleName(static_cast<ClaimStyle>(i)));
    return out;
}

std::string percent(float v) { return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%"; }
std::string times(float v) {
    const int tenths = static_cast<int>(std::lround(v * 10.0f));
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "x";
}

// The look a bot is given: anything, so a table of bots is a mix.
PlayerSetup botLook(PlayerSetup bot) {
    bot.pieces.finish     = static_cast<Finish>(Math::Random::range(0, static_cast<int>(Finish::Count) - 1));
    bot.pieces.hue        = Math::Random::value();
    bot.pieces.saturation = Math::Random::range(0.4f, 0.9f);
    bot.pieces.shine      = Math::Random::range(0.3f, 0.95f);
    bot.pieces.glow       = Math::Random::value() < 0.3f ? Math::Random::range(0.1f, 0.5f) : 0.0f;
    bot.take.style        = static_cast<TakeStyle>(Math::Random::range(0, static_cast<int>(TakeStyle::Count) - 1));
    bot.take.speed        = Math::Random::range(0.8f, 1.3f);
    bot.take.height       = Math::Random::range(0.8f, 1.6f);
    bot.take.spins        = static_cast<float>(Math::Random::range(0, 2));
    bot.take.hue          = Math::Random::value();
    bot.claim.style       = static_cast<ClaimStyle>(Math::Random::range(1, static_cast<int>(ClaimStyle::Count) - 1));
    bot.claim.hue         = Math::Random::value();
    return bot;
}

// A rainbow along U, for a hue slider's track.
TextureAsset rainbow() {
    constexpr uint32_t WIDE = 256;
    TextureAsset texture;
    texture.params.width           = WIDE;
    texture.params.height          = 1;
    texture.params.internalFormat  = TextureInternalFormat::SRGBA8;
    texture.params.format          = TexturePixelFormat::RGBA;
    texture.params.generateMipmaps = false;
    texture.pixelData.resize(WIDE * 4);
    for (uint32_t x = 0; x < WIDE; ++x) {
        const glm::vec3 c = hsv(static_cast<float>(x) / (WIDE - 1), 0.8f, 1.0f);
        texture.pixelData[x * 4 + 0] = static_cast<uint8_t>(c.r * 255.0f);
        texture.pixelData[x * 4 + 1] = static_cast<uint8_t>(c.g * 255.0f);
        texture.pixelData[x * 4 + 2] = static_cast<uint8_t>(c.b * 255.0f);
        texture.pixelData[x * 4 + 3] = 255;
    }
    return texture;
}

} // namespace

void Menu::link(ChessGame* game, Showcase* showcase, Scenery* scenery, NetLink* net) {
    m_game     = game;
    m_showcase = showcase;
    m_scenery  = scenery;
    m_net      = net;
}

void Menu::onlineJoined() { go(Screen::Lobby); }

void Menu::onlineFailed() {
    if (m_game) m_game->reset();
    go(Screen::Online);
}

void Menu::onlineGameStarted() { go(Screen::Playing); }

void Menu::onlineLobby() { go(Screen::Lobby); }

void Menu::onStart() {
    if (net().role() == NetRole::Server) {
        m_off = true;
        return;
    }
    m_kit.bind(scene(), resources(), input(),
               [this](const char* name, EntityId parent) { return spawn(name, parent); },
               [this](EntityId id) { destroy(id); });
    subscribe([this](const UIClickEvent& click) { m_kit.click(click.eventId); });

    m_canvas = spawn("Menu");
    UICanvas layer;
    layer.sortOrder = 30;
    scene().add(m_canvas, std::move(layer));
    m_hue = resources().add(rainbow(), "ui:rainbow");

    if (window()) m_fullscreen = window()->mode() == WindowMode::Fullscreen;
    syncYou();
    addBot(Color::Black);
    apply();
    go(Screen::Main);
}

void Menu::onUpdate(float dt) {
    if (m_off) return;
    m_kit.update(dt);

    // The online screens follow the network: the status as it goes, the seats as they change.
    if (m_screen == Screen::Online && m_net) {
        if (UIText* text = scene().tryGet<UIText>(m_statusLabel)) text->text = m_net->status();
    }
    if (m_screen == Screen::Lobby && m_net && m_net->seatsVersion() != m_seatsSeen) m_rebuild = true;

    if (input().pressed(ACTION_BACK) && !m_kit.capturing()) {
        switch (m_screen) {
            case Screen::Playing:   go(Screen::Paused); break;
            case Screen::Paused:    go(Screen::Playing); break;
            case Screen::Settings:  go(m_return); break;
            case Screen::Play:
            case Screen::Online:    go(Screen::Main); break;
            case Screen::Customize: go(m_customReturn); break;
            default:                break;
        }
    }

    // The match over, its results after a moment for the last move to land.
    if (m_screen == Screen::Playing && m_game && m_game->over()) {
        m_overDelay += dt;
        if (m_overDelay > 2.5f) go(Screen::Over);
    } else {
        m_overDelay = 0.0f;
    }

    if (m_rebuild) {
        m_rebuild = false;
        build();
    }
}

void Menu::go(Screen screen) {
    const Screen was = m_screen;
    if (was == Screen::Customize && screen != Screen::Customize) {
        if (m_showcase) m_showcase->hide();
        if (m_game) m_game->unfocus();
        syncYou();
        storeLocal();
        if (m_net && m_net->online()) m_net->sendProfile();
    }
    if (screen == Screen::Customize && was != Screen::Customize) m_customReturn = was == Screen::Lobby ? Screen::Lobby : Screen::Main;
    if (was == Screen::Settings && screen != Screen::Settings) storeLocal();
    if (screen == Screen::Settings && was != Screen::Settings) m_return = was == Screen::Paused ? Screen::Paused : Screen::Main;
    if (screen == Screen::Customize && was != Screen::Customize) {
        if (m_showcase) m_showcase->show(localProfile(), m_previewBlack ? Color::Black : Color::White);
        if (m_game) m_game->focus(Showcase::eye(), Showcase::target());
    }
    m_screen = screen;
    if (m_game) m_game->setInputEnabled(screen == Screen::Playing);
    build();
}

void Menu::build() {
    m_kit.reset();
    if (m_root) destroy(m_root);
    m_root = spawn("Menu Screen", m_canvas);
    UIElement whole;
    whole.relativeSize = {1.0f, 1.0f};
    whole.size         = {0.0f, 0.0f};
    scene().add(m_root, std::move(whole));
    switch (m_screen) {
        case Screen::Main:      buildMain(); break;
        case Screen::Play:      buildPlay(); break;
        case Screen::Online:    buildOnline(); break;
        case Screen::Lobby:     buildLobby(); break;
        case Screen::Customize: buildCustomize(); break;
        case Screen::Settings:  buildSettings(); break;
        case Screen::Paused:    buildPaused(); break;
        case Screen::Over:      buildOver(); break;
        case Screen::Playing:   break;
    }
}

// A darkened, pointer-blocking cover over the game, for screens shown on top of it.
EntityId Menu::backdrop(float alpha) {
    UIElement whole;
    whole.relativeSize = {1.0f, 1.0f};
    whole.size         = {0.0f, 0.0f};
    return m_kit.panel(m_root, std::move(whole), {0.0f, 0.0f, 0.02f, alpha}, 0.0f);
}

// A glass card with a gold title along its top.
EntityId Menu::card(UIElement place, const std::string& title) {
    const EntityId panel = m_kit.panel(m_root, std::move(place), Ui::GLASS, 26.0f, glm::vec4(glm::vec3(Ui::GOLD), 0.18f), 1.5f);
    m_kit.label(panel, UIElement::at({0.0f, 0.0f}, {44.0f, 26.0f}, {900.0f, 64.0f}), title, 50.0f, Ui::GOLD);
    return panel;
}

// A row of tab buttons; the active one gold, a click on another switches and rebuilds.
void Menu::tabs(EntityId parent, float x, float y, const std::vector<std::string>& names, int& active) {
    float at = x;
    for (size_t i = 0; i < names.size(); ++i) {
        const bool  on   = static_cast<int>(i) == active;
        const float wide = 40.0f + 15.0f * static_cast<float>(names[i].size());
        Style style{on ? Ui::GOLD : Ui::FIELD, on ? glm::vec4(0.08f, 0.07f, 0.06f, 1.0f) : Ui::INK, 22.0f, 20.0f};
        m_kit.button(parent, UIElement::at({0.0f, 0.0f}, {at, y}, {wide, 44.0f}), names[i], [this, &active, i] {
            active    = static_cast<int>(i);
            m_rebuild = true;
        }, style);
        at += wide + 12.0f;
    }
}

void Menu::buildMain() {
    const float x = 110.0f;
    m_kit.label(m_root, UIElement::at({0.0f, 0.0f}, {x, 110.0f}, {900.0f, 120.0f}), "BACKSTAB", 118.0f, Ui::GOLD);
    m_kit.label(m_root, UIElement::at({0.0f, 0.0f}, {x, 215.0f}, {900.0f, 120.0f}), "CHESS", 118.0f, Ui::INK);
    m_kit.label(m_root, UIElement::at({0.0f, 0.0f}, {x + 6.0f, 320.0f}, {900.0f, 40.0f}), "Chess where your own team is out to get you",
                26.0f, Ui::INK_DIM);

    const Style big{Ui::GLASS, Ui::INK, 34.0f, 16.0f, UIText::Align::Left, glm::vec4(glm::vec3(Ui::GOLD), 0.22f)};
    const struct {
        const char*           text;
        std::function<void()> act;
    } items[] = {
        {"PRACTICE", [this] { go(Screen::Play); }},
        {"PLAY ONLINE", [this] { go(Screen::Online); }},
        {"CUSTOMIZE", [this] { go(Screen::Customize); }},
        {"SETTINGS", [this] { go(Screen::Settings); }},
        {"QUIT", [this] { if (window()) window()->requestClose(); }},
    };
    float y = 420.0f;
    for (const auto& item : items) {
        m_kit.button(m_root, UIElement::at({0.0f, 0.0f}, {x, y}, {470.0f, 70.0f}), item.text, item.act, big);
        y += 84.0f;
    }

    // Who is playing, bottom left.
    const Profile& you = localProfile();
    m_kit.panel(m_root, UIElement::at({0.0f, 1.0f}, {x, -64.0f}, {22.0f, 22.0f}), glm::vec4(you.color, 1.0f), 11.0f);
    m_kit.label(m_root, UIElement::at({0.0f, 1.0f}, {x + 36.0f, -52.0f}, {700.0f, 46.0f}), "Playing as " + you.name, 26.0f, Ui::INK);
}

void Menu::buildPlay() {
    syncYou();
    if (m_game) m_game->setPlayers(m_players);  // the heads round the table, as the teams stand
    m_kit.label(m_root, UIElement::at({0.5f, 0.0f}, {0.0f, 60.0f}, {1400.0f, 80.0f}), "CHOOSE YOUR TEAMS", 60.0f, Ui::GOLD, UIText::Align::Center);
    m_kit.label(m_root, UIElement::at({0.5f, 0.0f}, {0.0f, 134.0f}, {1400.0f, 36.0f}),
                "Practice against bots - play them on either side, as many as four a side", 24.0f, Ui::INK_DIM, UIText::Align::Center);

    constexpr float WIDE = 600.0f;
    for (const Color side : {Color::White, Color::Black}) {
        const bool      white = side == Color::White;
        const int       n     = count(side);
        const float     high  = 82.0f + 74.0f * static_cast<float>(n) + (n < MAX_PER_SIDE ? 64.0f : 0.0f) + 16.0f;
        const glm::vec4 ink   = white ? glm::vec4(0.1f, 0.1f, 0.12f, 1.0f) : Ui::INK;
        const glm::vec4 fill  = white ? glm::vec4(0.84f, 0.81f, 0.76f, 1.0f) : glm::vec4(0.14f, 0.14f, 0.17f, 1.0f);
        const EntityId  team  = m_kit.panel(m_root, UIElement::at({0.5f, 0.0f}, {white ? -320.0f : 320.0f, 210.0f}, {WIDE, high}),
                                            white ? glm::vec4(0.93f, 0.91f, 0.86f, 0.94f) : glm::vec4(0.05f, 0.05f, 0.07f, 0.92f), 24.0f);
        m_kit.label(team, UIElement::at({0.0f, 0.0f}, {28.0f, 18.0f}, {300.0f, 48.0f}), white ? "WHITE" : "BLACK", 40.0f, ink);
        m_kit.label(team, UIElement::at({1.0f, 0.0f}, {-28.0f, 18.0f}, {200.0f, 48.0f}), std::to_string(n) + " / 4", 24.0f,
                    glm::vec4(glm::vec3(ink), 0.6f), UIText::Align::Right);
        float y = 82.0f;
        for (size_t i = 0; i < m_players.size(); ++i) {
            const PlayerSetup& p = m_players[i];
            if (p.side != side) continue;
            const EntityId row = m_kit.panel(team, UIElement::at({0.0f, 0.0f}, {16.0f, y}, {WIDE - 32.0f, 64.0f}), fill, 16.0f);
            m_kit.panel(row, UIElement::at({0.0f, 0.5f}, {14.0f, 0.0f}, {36.0f, 36.0f}), glm::vec4(p.color, 1.0f), 18.0f);
            m_kit.label(row, UIElement::at({0.0f, 0.0f}, {66.0f, 0.0f}, {300.0f, 64.0f}), p.name, 27.0f, ink);
            const glm::vec4 you = white ? glm::vec4(0.62f, 0.4f, 0.05f, 1.0f) : Ui::GOLD;
            m_kit.label(row, UIElement::at({0.0f, 0.0f}, {300.0f, 0.0f}, {120.0f, 64.0f}), p.local ? "YOU" : "BOT", 20.0f,
                        p.local ? you : glm::vec4(glm::vec3(ink), 0.5f));
            const Style knob{white ? glm::vec4(0.74f, 0.71f, 0.66f, 1.0f) : glm::vec4(0.23f, 0.23f, 0.28f, 1.0f), ink, 20.0f, 12.0f};
            m_kit.button(row, UIElement::at({0.0f, 0.0f}, {WIDE - 32.0f - 150.0f, 10.0f}, {92.0f, 44.0f}), "Switch", [this, i] {
                const Color other = Chess::opposite(m_players[i].side);
                if (count(other) < MAX_PER_SIDE) m_players[i].side = other;
                m_rebuild = true;
            }, knob);
            if (!p.local) {
                m_kit.button(row, UIElement::at({0.0f, 0.0f}, {WIDE - 32.0f - 50.0f, 10.0f}, {40.0f, 44.0f}), "X", [this, i] {
                    m_players.erase(m_players.begin() + static_cast<std::ptrdiff_t>(i));
                    m_rebuild = true;
                }, knob);
            }
            y += 74.0f;
        }
        if (n < MAX_PER_SIDE) {
            m_kit.button(team, UIElement::at({0.0f, 0.0f}, {16.0f, y}, {WIDE - 32.0f, 54.0f}), "+  Add a bot", [this, side] {
                addBot(side);
                m_rebuild = true;
            }, Style{glm::vec4(glm::vec3(fill), 0.55f), ink, 24.0f, 16.0f});
        }
    }

    // Along the bottom: back, the turn's length, and the start.
    const Style plain{Ui::GLASS, Ui::INK, 28.0f, 34.0f};
    m_kit.button(m_root, UIElement::at({0.5f, 1.0f}, {-520.0f, -48.0f}, {220.0f, 70.0f}), "Back", [this] { go(Screen::Main); }, plain);
    const EntityId time = m_kit.panel(m_root, UIElement::at({0.5f, 1.0f}, {-120.0f, -48.0f}, {440.0f, 70.0f}), Ui::GLASS, 34.0f);
    Settings& settings = localSettings();
    m_kit.choiceRow(time, 8.0f, 400.0f, "   Turn time", labels(TURN_CHOICES, "s"), indexOf(TURN_CHOICES, settings.turnSeconds), [this](int i) {
        localSettings().turnSeconds = TURN_CHOICES[i];
        apply();
    });
    const bool     ready = count(Color::White) > 0 && count(Color::Black) > 0;
    const EntityId start = m_kit.button(m_root, UIElement::at({0.5f, 1.0f}, {330.0f, -44.0f}, {340.0f, 78.0f}), "START", [this] { startMatch(); },
                                        Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 38.0f, 39.0f});
    if (UIButton* press = scene().tryGet<UIButton>(start)) press->interactable = ready;
}

void Menu::buildCustomize() {
    Profile& you = localProfile();
    constexpr float WIDE = 640.0f;
    constexpr float ROWS = 560.0f;  // the rows' width inside the card
    const EntityId panel = card(UIElement::at({0.0f, 0.5f}, {56.0f, 0.0f}, {WIDE, 960.0f}), "CUSTOMIZE");
    const EntityId body  = m_kit.group(panel, UIElement::at({0.0f, 0.0f}, {40.0f, 110.0f}, {ROWS, 820.0f}));

    const auto refresh = [this] {
        if (m_showcase) m_showcase->refresh(localProfile());
    };
    float y = 0.0f;
    m_kit.fieldRow(body, y, ROWS, "Name", you.name, 16, [this](const std::string& name) {
        localProfile().name = name.empty() ? "Player" : name;
        syncYou();
    });
    y += Kit::ROW;
    m_kit.sliderRow(body, y, ROWS, "Your colour", you.hue, 0.0f, 1.0f, [this](float h) {
        localProfile().hue   = h;
        localProfile().color = hsv(h, 0.68f, 1.0f);
        syncYou();
    }, [](float) { return std::string(); }, m_hue);
    y += Kit::ROW;
    m_kit.choiceRow(body, y, ROWS, "Preview as", {"White", "Black"}, m_previewBlack ? 1 : 0, [this](int i) {
        m_previewBlack = i == 1;
        if (m_showcase) m_showcase->setSide(m_previewBlack ? Color::Black : Color::White);
    });
    y += Kit::ROW + 14.0f;

    tabs(body, 0.0f, y, {"PIECES", "TAKE", "CLAIM"}, m_customTab);
    y += 70.0f;

    if (m_customTab == 0) {
        PieceLook& look = you.pieces;
        m_kit.choiceRow(body, y, ROWS, "Material", finishNames(), static_cast<int>(look.finish), [this, refresh](int i) {
            localProfile().pieces.finish = static_cast<Finish>(i);
            refresh();
        });
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Tint", look.hue, 0.0f, 1.0f, [refresh](float v) {
            localProfile().pieces.hue = v;
            refresh();
        }, [](float) { return std::string(); }, m_hue);
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Tint strength", look.saturation, 0.0f, 1.0f, [refresh](float v) {
            localProfile().pieces.saturation = v;
            refresh();
        }, percent);
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Shine", look.shine, 0.0f, 1.0f, [refresh](float v) {
            localProfile().pieces.shine = v;
            refresh();
        }, percent);
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Glow", look.glow, 0.0f, 1.0f, [refresh](float v) {
            localProfile().pieces.glow = v;
            refresh();
        }, percent);
        y += Kit::ROW;
    } else if (m_customTab == 1) {
        TakeLook& take = you.take;
        m_kit.choiceRow(body, y, ROWS, "Style", takeNames(), static_cast<int>(take.style), [](int i) {
            localProfile().take.style = static_cast<TakeStyle>(i);
        });
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Speed", take.speed, 0.5f, 2.0f, [](float v) { localProfile().take.speed = v; }, times);
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Height", take.height, 0.3f, 2.5f, [](float v) { localProfile().take.height = v; }, times);
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Turns", take.spins, 0.0f, 4.0f, [](float v) { localProfile().take.spins = std::round(v); },
                        [](float v) { return std::to_string(static_cast<int>(std::lround(v))); });
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Effect colour", take.hue, 0.0f, 1.0f, [refresh](float v) {
            localProfile().take.hue = v;
            refresh();
        }, [](float) { return std::string(); }, m_hue);
        y += Kit::ROW + 20.0f;
        m_kit.button(body, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {ROWS, 62.0f}), "Show the take", [this] {
            if (m_showcase) m_showcase->playTake();
        }, Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 28.0f, 31.0f});
    } else {
        ClaimLook& claim = you.claim;
        m_kit.choiceRow(body, y, ROWS, "Style", claimNames(), static_cast<int>(claim.style), [](int i) {
            localProfile().claim.style = static_cast<ClaimStyle>(i);
        });
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Speed", claim.speed, 0.5f, 2.0f, [](float v) { localProfile().claim.speed = v; }, times);
        y += Kit::ROW;
        m_kit.sliderRow(body, y, ROWS, "Colour", claim.hue, 0.0f, 1.0f, [refresh](float v) {
            localProfile().claim.hue = v;
            refresh();
        }, [](float) { return std::string(); }, m_hue);
        y += Kit::ROW + 20.0f;
        m_kit.button(body, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {ROWS, 62.0f}), "Show the claim", [this] {
            if (m_showcase) m_showcase->playClaim();
        }, Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 28.0f, 31.0f});
    }

    m_kit.button(panel, UIElement::at({0.0f, 1.0f}, {40.0f, -36.0f}, {ROWS, 64.0f}), "Done", [this] { go(Screen::Main); },
                 Style{Ui::FIELD, Ui::INK, 28.0f, 32.0f});
}

void Menu::buildSettings() {
    if (m_return == Screen::Paused) backdrop(0.55f);
    Settings& s = localSettings();
    const EntityId panel = card(UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {1240.0f, 820.0f}), "SETTINGS");
    tabs(panel, 44.0f, 104.0f, {"GRAPHICS", "GAME", "CONTROLS"}, m_settingsTab);

    constexpr float COLUMN = 540.0f;
    const EntityId left  = m_kit.group(panel, UIElement::at({0.0f, 0.0f}, {44.0f, 180.0f}, {COLUMN, 520.0f}));
    const EntityId right = m_kit.group(panel, UIElement::at({0.0f, 0.0f}, {656.0f, 180.0f}, {COLUMN, 520.0f}));
    // Anything set by hand makes the quality custom.
    const auto custom = [this] {
        localSettings().quality = 4;
        apply();
    };

    if (m_settingsTab == 0) {
        float y = 0.0f;
        m_kit.choiceRow(left, y, COLUMN, "Quality", {"Low", "Medium", "High", "Ultra", "Custom"}, s.quality, [this](int i) {
            if (i < 4) applyPreset(localSettings(), i);
            apply();
            m_rebuild = true;
        });
        y += Kit::ROW;
        m_kit.choiceRow(left, y, COLUMN, "Anti-aliasing", {"Off", "2x", "4x", "8x"}, indexOf(MSAA_CHOICES, s.msaa), [custom](int i) {
            localSettings().msaa = MSAA_CHOICES[i];
            custom();
        });
        y += Kit::ROW;
        m_kit.choiceRow(left, y, COLUMN, "Shadows", {"Low", "Medium", "High"}, indexOf(SHADOW_CHOICES, s.shadows), [custom](int i) {
            localSettings().shadows = SHADOW_CHOICES[i];
            custom();
        });
        y += Kit::ROW;
        m_kit.toggleRow(left, y, COLUMN, "Reflections", s.reflections, [custom](bool on) { localSettings().reflections = on; custom(); });
        y += Kit::ROW;
        m_kit.toggleRow(left, y, COLUMN, "Ambient occlusion", s.occlusion, [custom](bool on) { localSettings().occlusion = on; custom(); });
        y += Kit::ROW;
        m_kit.toggleRow(left, y, COLUMN, "Bloom", s.bloom, [custom](bool on) { localSettings().bloom = on; custom(); });

        y = 0.0f;
        m_kit.toggleRow(right, y, COLUMN, "Fog", s.fog, [custom](bool on) { localSettings().fog = on; custom(); });
        y += Kit::ROW;
        m_kit.choiceRow(right, y, COLUMN, "Sea detail", {"Low", "Medium", "High"}, s.seaDetail, [custom](int i) {
            localSettings().seaDetail = i;
            custom();
        });
        y += Kit::ROW;
        m_kit.sliderRow(right, y, COLUMN, "Brightness", s.brightness, -1.0f, 2.5f, [this](float v) {
            localSettings().brightness = v;
            apply();
        }, [](float v) { return std::to_string(static_cast<int>(std::lround((v + 1.0f) / 3.5f * 100.0f))) + "%"; });
        y += Kit::ROW;
        m_kit.choiceRow(right, y, COLUMN, "Tone mapping", {"Reinhard", "Filmic", "Neutral", "AgX"}, s.tonemap, [this](int i) {
            localSettings().tonemap = i;
            apply();
        });
        y += Kit::ROW;
        m_kit.toggleRow(right, y, COLUMN, "VSync", s.vsync, [this](bool on) { localSettings().vsync = on; apply(); });
        y += Kit::ROW;
        m_kit.toggleRow(right, y, COLUMN, "Fullscreen", s.fullscreen, [this](bool on) { localSettings().fullscreen = on; apply(); });
    } else if (m_settingsTab == 1) {
        float y = 0.0f;
        m_kit.choiceRow(left, y, COLUMN, "Turn time", labels(TURN_CHOICES, "s"), indexOf(TURN_CHOICES, s.turnSeconds), [this](int i) {
            localSettings().turnSeconds = TURN_CHOICES[i];
            apply();
        });
        y += Kit::ROW;
        m_kit.choiceRow(left, y, COLUMN, "Duel time", labels(DUEL_CHOICES, "s"), indexOf(DUEL_CHOICES, s.duelSeconds), [this](int i) {
            localSettings().duelSeconds = DUEL_CHOICES[i];
            apply();
        });
        y += Kit::ROW;
        m_kit.toggleRow(left, y, COLUMN, "Show moves", s.showHints, [this](bool on) { localSettings().showHints = on; apply(); });
        y = 0.0f;
        m_kit.sliderRow(right, y, COLUMN, "Look speed", s.lookSpeed, 0.3f, 2.5f, [this](float v) { localSettings().lookSpeed = v; apply(); }, times);
        y += Kit::ROW;
        m_kit.sliderRow(right, y, COLUMN, "Fly speed", s.flySpeed, 0.3f, 3.0f, [this](float v) { localSettings().flySpeed = v; apply(); }, times);
    } else {
        for (size_t i = 0; i < s.keys.size(); ++i) {
            const EntityId column = i < (s.keys.size() + 1) / 2 ? left : right;
            const float    y      = Kit::ROW * static_cast<float>(i < (s.keys.size() + 1) / 2 ? i : i - (s.keys.size() + 1) / 2);
            m_kit.keyRow(column, y, COLUMN, s.keys[i].label, s.keys[i].key, [this, i](int key) {
                localSettings().keys[i].key = key;
                apply();
            });
        }
    }

    const Style plain{Ui::FIELD, Ui::INK, 26.0f, 30.0f};
    m_kit.button(panel, UIElement::at({0.0f, 1.0f}, {44.0f, -32.0f}, {300.0f, 60.0f}), "Reset to defaults", [this] {
        Settings& settings = localSettings();
        if (m_settingsTab == 2) {
            settings.keys = defaultKeys();
        } else {
            const std::vector<KeyBinding> keys = settings.keys;
            settings      = Settings{};
            settings.keys = keys;
            applyPreset(settings, 2);
        }
        apply();
        m_rebuild = true;
    }, plain);
    m_kit.button(panel, UIElement::at({1.0f, 1.0f}, {-44.0f, -32.0f}, {240.0f, 60.0f}), "Back", [this] { go(m_return); },
                 Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 28.0f, 30.0f});
}

void Menu::buildPaused() {
    backdrop(0.55f);
    const EntityId panel = card(UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {520.0f, 480.0f}), "PAUSED");
    const Style    big{Ui::FIELD, Ui::INK, 30.0f, 16.0f};
    const struct {
        const char*           text;
        std::function<void()> act;
    } items[] = {
        {"Resume", [this] { go(Screen::Playing); }},
        {"Settings", [this] { go(Screen::Settings); }},
        {"Leave the match", [this] {
            if (m_net) m_net->leave();
            if (m_game) {
                m_game->reset();
                m_game->clearRemote();
            }
            go(Screen::Main);
        }},
        {"Quit", [this] { if (window()) window()->requestClose(); }},
    };
    float y = 120.0f;
    for (const auto& item : items) {
        m_kit.button(panel, UIElement::at({0.5f, 0.0f}, {0.0f, y}, {420.0f, 66.0f}), item.text, item.act, big);
        y += 86.0f;
    }
}

void Menu::buildOver() {
    backdrop(0.45f);
    std::vector<Chess::Player> players = m_game ? m_game->results() : std::vector<Chess::Player>{};
    std::vector<size_t>        order(players.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return players[a].score > players[b].score; });

    const EntityId panel = card(UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {760.0f, 260.0f + 66.0f * static_cast<float>(players.size())}), "GAME OVER");
    const std::string why = m_game ? m_game->overReason() : "";
    m_kit.label(panel, UIElement::at({0.0f, 0.0f}, {46.0f, 92.0f}, {680.0f, 40.0f}), why, 26.0f, Ui::INK_DIM);
    float y = 146.0f;
    for (size_t rank = 0; rank < order.size(); ++rank) {
        const size_t i    = order[rank];
        const bool   best = rank == 0 || players[i].score == players[order[0]].score;
        const glm::vec3 color = i < m_players.size() ? m_players[i].color : glm::vec3(1.0f);
        const EntityId row = m_kit.panel(panel, UIElement::at({0.0f, 0.0f}, {40.0f, y}, {680.0f, 56.0f}),
                                         best ? glm::vec4(glm::vec3(Ui::GOLD) * 0.35f, 0.9f) : Ui::GLASS_LIGHT, 14.0f,
                                         best ? Ui::GOLD : glm::vec4(0.0f), best ? 2.0f : 0.0f);
        m_kit.label(row, UIElement::at({0.0f, 0.0f}, {20.0f, 0.0f}, {60.0f, 56.0f}), std::to_string(rank + 1), 28.0f, best ? Ui::GOLD : Ui::INK_DIM);
        m_kit.panel(row, UIElement::at({0.0f, 0.5f}, {70.0f, 0.0f}, {22.0f, 22.0f}), glm::vec4(color, 1.0f), 11.0f);
        m_kit.label(row, UIElement::at({0.0f, 0.0f}, {108.0f, 0.0f}, {400.0f, 56.0f}), players[i].name + (best ? "   WINNER" : ""), 28.0f, Ui::INK);
        m_kit.label(row, UIElement::at({1.0f, 0.0f}, {-24.0f, 0.0f}, {160.0f, 56.0f}), std::to_string(players[i].score), 30.0f, Ui::INK,
                    UIText::Align::Right);
        y += 66.0f;
    }
    const bool online = m_net && m_net->online();
    m_kit.button(panel, UIElement::at({0.0f, 1.0f}, {40.0f, -32.0f}, {320.0f, 64.0f}), online ? "Leave" : "Main menu", [this] {
        if (m_net) m_net->leave();
        if (m_game) {
            m_game->reset();
            m_game->clearRemote();
        }
        go(Screen::Main);
    }, Style{Ui::FIELD, Ui::INK, 28.0f, 32.0f});
    if (!online || m_net->isHost()) {
        m_kit.button(panel, UIElement::at({1.0f, 1.0f}, {-40.0f, -32.0f}, {320.0f, 64.0f}), online ? "Back to the lobby" : "Play again", [this] {
            if (m_net && m_net->online()) {
                m_net->sendAgain();
                return;
            }
            if (m_game) m_game->reset();
            startMatch();
        }, Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 28.0f, 32.0f});
    } else {
        m_kit.label(panel, UIElement::at({1.0f, 1.0f}, {-40.0f, -32.0f}, {360.0f, 64.0f}), "The host decides what next", 22.0f, Ui::INK_DIM,
                    UIText::Align::Right);
    }
}

void Menu::buildOnline() {
    const EntityId panel = card(UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {760.0f, 690.0f}), "PLAY ONLINE");
    const EntityId body  = m_kit.group(panel, UIElement::at({0.0f, 0.0f}, {44.0f, 110.0f}, {672.0f, 460.0f}));
    float y = 0.0f;
    m_kit.heading(body, y, 672.0f, "HOST A GAME ON THIS MACHINE");
    y += Kit::ROW;
    Settings& settings = localSettings();
    m_kit.fieldRow(body, y, 672.0f, "Port", settings.hostPort, 5, [](const std::string& port) { localSettings().hostPort = port; });
    y += Kit::ROW + 8.0f;
    m_kit.button(body, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {672.0f, 58.0f}), "Host", [this] {
        if (!m_net) return;
        int port = 27750;
        try {
            port = std::stoi(localSettings().hostPort);
        } catch (...) {
        }
        storeLocal();
        m_net->host(static_cast<uint16_t>(std::clamp(port, 1024, 65535)));
    }, Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 26.0f, 29.0f});
    y += 58.0f + 26.0f;
    m_kit.heading(body, y, 672.0f, "JOIN A GAME");
    y += Kit::ROW;
    m_kit.fieldRow(body, y, 672.0f, "Address", settings.joinAddress, 40, [](const std::string& address) { localSettings().joinAddress = address; });
    y += Kit::ROW + 8.0f;
    m_kit.button(body, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {672.0f, 58.0f}), "Join", [this] {
        if (!m_net) return;
        storeLocal();
        m_net->join(localSettings().joinAddress);
    }, Style{Ui::FIELD, Ui::INK, 26.0f, 29.0f});
    y += 58.0f + 14.0f;
    m_statusLabel = m_kit.label(body, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {672.0f, 40.0f}), m_net ? m_net->status() : "", 22.0f, Ui::INK_DIM);

    m_kit.button(panel, UIElement::at({0.0f, 1.0f}, {44.0f, -30.0f}, {220.0f, 60.0f}), "Back", [this] {
        if (m_net) m_net->leave();
        go(Screen::Main);
    }, Style{Ui::FIELD, Ui::INK, 26.0f, 30.0f});
}

// The seats the server has, in their teams; you can switch side, the host can start.
void Menu::buildLobby() {
    if (!m_net) return;
    m_seatsSeen = m_net->seatsVersion();
    const MatchState* state = m_net->match();
    const EntityId head = m_kit.panel(m_root, UIElement::at({0.5f, 0.0f}, {0.0f, 50.0f}, {760.0f, 130.0f}), Ui::GLASS, 30.0f);
    m_kit.label(head, UIElement::at({0.5f, 0.0f}, {0.0f, 12.0f}, {740.0f, 72.0f}), "THE TABLE", 56.0f, Ui::GOLD, UIText::Align::Center);
    m_kit.label(head, UIElement::at({0.5f, 0.0f}, {0.0f, 82.0f}, {740.0f, 34.0f}),
                m_net->isHost() ? "You are the host: start when the teams are set" : "Waiting for the host to start", 24.0f, Ui::INK_DIM,
                UIText::Align::Center);

    const auto seats = m_net->seats();
    int counts[2] = {0, 0};
    for (const auto& [_, seat] : seats) ++counts[seat.side & 1];
    constexpr float WIDE = 600.0f;
    for (int side = 0; side < 2; ++side) {
        const bool      white = side == 0;
        const int       n     = counts[side];
        const float     high  = 82.0f + 74.0f * static_cast<float>(std::max(n, 1)) + 16.0f;
        const glm::vec4 ink   = white ? glm::vec4(0.1f, 0.1f, 0.12f, 1.0f) : Ui::INK;
        const glm::vec4 fill  = white ? glm::vec4(0.84f, 0.81f, 0.76f, 1.0f) : glm::vec4(0.14f, 0.14f, 0.17f, 1.0f);
        const EntityId  team  = m_kit.panel(m_root, UIElement::at({0.5f, 0.0f}, {white ? -320.0f : 320.0f, 210.0f}, {WIDE, high}),
                                            white ? glm::vec4(0.93f, 0.91f, 0.86f, 0.94f) : glm::vec4(0.05f, 0.05f, 0.07f, 0.92f), 24.0f);
        m_kit.label(team, UIElement::at({0.0f, 0.0f}, {28.0f, 18.0f}, {300.0f, 48.0f}), white ? "WHITE" : "BLACK", 40.0f, ink);
        float y = 82.0f;
        for (const auto& [index, seat] : seats) {
            if ((seat.side & 1) != side) continue;
            const bool     me  = seat.player == m_net->localPlayer();
            const EntityId row = m_kit.panel(team, UIElement::at({0.0f, 0.0f}, {16.0f, y}, {WIDE - 32.0f, 64.0f}), fill, 16.0f);
            m_kit.panel(row, UIElement::at({0.0f, 0.5f}, {14.0f, 0.0f}, {36.0f, 36.0f}), glm::vec4(hsv(seat.hue / 255.0f, 0.68f, 1.0f), 1.0f), 18.0f);
            m_kit.label(row, UIElement::at({0.0f, 0.0f}, {66.0f, 0.0f}, {260.0f, 64.0f}), seat.name, 27.0f, ink);
            std::string tags = me ? "YOU" : "";
            if (state && seat.player == state->host) tags += tags.empty() ? "HOST" : "  HOST";
            m_kit.label(row, UIElement::at({0.0f, 0.0f}, {300.0f, 0.0f}, {140.0f, 64.0f}), tags, 20.0f,
                        white ? glm::vec4(0.62f, 0.4f, 0.05f, 1.0f) : Ui::GOLD);
            if (me) {
                const Style knob{white ? glm::vec4(0.74f, 0.71f, 0.66f, 1.0f) : glm::vec4(0.23f, 0.23f, 0.28f, 1.0f), ink, 20.0f, 12.0f};
                m_kit.button(row, UIElement::at({0.0f, 0.0f}, {WIDE - 32.0f - 106.0f, 10.0f}, {92.0f, 44.0f}), "Switch", [this, side] {
                    if (m_net) m_net->sendSide(1 - side);
                }, knob);
            }
            y += 74.0f;
        }
        if (n == 0) m_kit.label(team, UIElement::at({0.0f, 0.0f}, {28.0f, y}, {WIDE - 56.0f, 64.0f}), "Nobody yet", 24.0f, glm::vec4(glm::vec3(ink), 0.5f));
    }

    // A guest's two buttons sit centred; the host's share the row with the turn time and START.
    const Style plain{Ui::GLASS, Ui::INK, 28.0f, 34.0f};
    const float shift = m_net->isHost() ? 0.0f : 380.0f;
    m_kit.button(m_root, UIElement::at({0.5f, 1.0f}, {-520.0f + shift, -48.0f}, {220.0f, 70.0f}), "Leave", [this] {
        if (m_net) m_net->leave();
        go(Screen::Main);
    }, plain);
    m_kit.button(m_root, UIElement::at({0.5f, 1.0f}, {-250.0f + shift, -48.0f}, {260.0f, 70.0f}), "Customize", [this] { go(Screen::Customize); }, plain);
    if (m_net->isHost()) {
        const EntityId time = m_kit.panel(m_root, UIElement::at({0.5f, 1.0f}, {80.0f, -48.0f}, {360.0f, 70.0f}), Ui::GLASS, 34.0f);
        Settings& settings = localSettings();
        m_kit.choiceRow(time, 8.0f, 330.0f, "   Turn", labels(TURN_CHOICES, "s"), indexOf(TURN_CHOICES, settings.turnSeconds), [](int i) {
            localSettings().turnSeconds = TURN_CHOICES[i];
        });
        const bool     ready = counts[0] > 0 && counts[1] > 0;
        const EntityId start = m_kit.button(m_root, UIElement::at({0.5f, 1.0f}, {460.0f, -44.0f}, {300.0f, 78.0f}), "START", [this] {
            const Settings& s = localSettings();
            if (m_net) m_net->sendStart(static_cast<int>(s.turnSeconds), static_cast<int>(s.duelSeconds));
        }, Style{Ui::GOLD, {0.08f, 0.07f, 0.06f, 1.0f}, 38.0f, 39.0f});
        if (UIButton* press = scene().tryGet<UIButton>(start)) press->interactable = ready;
    }
}

// The settings, to the renderer, the window, the keys, the world and the game.
void Menu::apply() {
    const Settings& s = localSettings();
    RenderSettings& r = render();
    r.msaaSamples      = static_cast<uint32_t>(s.msaa);
    r.shadowResolution = static_cast<uint32_t>(s.shadows);
    r.ssr              = s.reflections;
    r.gtao             = s.occlusion;
    r.bloom            = s.bloom;
    r.exposure         = s.brightness;
    r.tonemap          = static_cast<Tonemap>(std::clamp(s.tonemap, 0, static_cast<int>(Tonemap::Count) - 1));
    scene().environment().fog.enabled = s.fog;
    if (WindowManager* w = window()) {
        w->setVSync(s.vsync);
        if (s.fullscreen != m_fullscreen) {
            w->updateMode(s.fullscreen ? WindowMode::Fullscreen : WindowMode::Windowed);
            m_fullscreen = s.fullscreen;
        }
    }
    for (const KeyBinding& binding : s.keys) {
        const bool mouse = binding.key >= 1000;
        input().define(binding.action, {InputBinding{mouse ? InputSource::MouseButton : InputSource::Key, mouse ? binding.key - 1000 : binding.key, 1.0f}});
    }
    if (m_scenery && s.seaDetail != m_seaDetail) {
        m_scenery->setDetail(s.seaDetail);
        m_seaDetail = s.seaDetail;
    }
    if (m_game) {
        m_game->turnSeconds = s.turnSeconds;
        m_game->duelSeconds = s.duelSeconds;
        m_game->lookScale   = s.lookSpeed;
        m_game->flyScale    = s.flySpeed;
        m_game->showHints   = s.showHints;
    }
}

// You, from the profile, at the head of the players.
void Menu::syncYou() {
    const Profile& you = localProfile();
    PlayerSetup    me;
    me.name   = you.name;
    me.color  = you.color;
    me.pieces = you.pieces;
    me.take   = you.take;
    me.claim  = you.claim;
    me.local  = true;
    if (!m_players.empty() && m_players.front().local) {
        me.side = m_players.front().side;
        m_players.front() = me;
    } else {
        m_players.insert(m_players.begin(), me);
    }
}

void Menu::addBot(Color side) {
    if (count(side) >= MAX_PER_SIDE) return;
    PlayerSetup bot;
    bot.name = "Bot " + std::to_string(m_nextBot++);
    bot.side = side;
    // A colour nobody wears yet.
    for (const glm::vec3& color : PALETTE) {
        const bool worn = std::any_of(m_players.begin(), m_players.end(), [&](const PlayerSetup& p) { return glm::length(p.color - color) < 0.15f; });
        if (!worn) {
            bot.color = color;
            break;
        }
    }
    m_players.push_back(botLook(bot));
}

int Menu::count(Color side) const {
    return static_cast<int>(std::count_if(m_players.begin(), m_players.end(), [&](const PlayerSetup& p) { return p.side == side; }));
}

void Menu::startMatch() {
    if (!m_game || count(Color::White) == 0 || count(Color::Black) == 0) return;
    syncYou();
    apply();
    m_game->setPlayers(m_players);
    m_game->begin();
    go(Screen::Playing);
}

} // namespace Game
