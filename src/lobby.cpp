#include "lobby.h"

#include <algorithm>
#include <cctype>
#include <iterator>

#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "system/ui/ui_events.h"

namespace Game {

namespace {

using Chess::Color;

constexpr int   MAX_PER_SIDE   = 4;
constexpr float TURN_CHOICES[] = {30.0f, 45.0f, 60.0f, 90.0f, 120.0f};

const glm::vec3 PALETTE[] = {
    {0.25f, 0.6f, 1.0f},  {0.95f, 0.35f, 0.75f}, {0.35f, 0.85f, 0.4f}, {1.0f, 0.55f, 0.15f},
    {0.65f, 0.4f, 1.0f},  {1.0f, 0.85f, 0.2f},   {0.2f, 0.85f, 0.85f}, {0.95f, 0.25f, 0.25f},
};
const PieceSet SKIN_ORDER[] = {PieceSet::Metal, PieceSet::Glass, PieceSet::Stone, PieceSet::Classic};

// The layout, in the canvas's 1080-high pixels.
constexpr float PANEL_WIDTH = 600.0f;
constexpr float PANEL_GAP   = 40.0f;
constexpr float PANEL_TOP   = 236.0f;
constexpr float CARD_HEIGHT = 64.0f;
constexpr float CARD_STEP   = CARD_HEIGHT + 8.0f;
constexpr float CARDS_TOP   = 76.0f;

const glm::vec4 GOLD     = {1.0f, 0.76f, 0.3f, 1.0f};
const glm::vec4 PALE_INK = {0.96f, 0.94f, 0.9f, 1.0f};
const glm::vec4 DARK_INK = {0.1f, 0.1f, 0.12f, 1.0f};

// What a skin is called on each side: the white set and the black are different stuff.
const char* skinLabel(PieceSet set, Color side) {
    const bool white = side == Color::White;
    switch (set) {
        case PieceSet::Metal: return white ? "Gold" : "Gunmetal";
        case PieceSet::Glass: return white ? "Glass" : "Smoked glass";
        case PieceSet::Stone: return white ? "Marble" : "Obsidian";
        default:              return white ? "Boxwood" : "Ebony";
    }
}

template <typename T, size_t N>
T nextOf(const T (&order)[N], T current) {
    const auto at = std::find(std::begin(order), std::end(order), current);
    return at == std::end(order) || at + 1 == std::end(order) ? order[0] : *(at + 1);
}

// The clicked button's verb and the player it is about, from "lobby/verb/index".
struct Click {
    std::string verb;
    std::string arg;
};

Click parse(const std::string& id) {
    const size_t a = id.find('/');
    const size_t b = id.find('/', a + 1);
    if (b == std::string::npos) return {id.substr(a + 1), ""};
    return {id.substr(a + 1, b - a - 1), id.substr(b + 1)};
}

} // namespace

void Lobby::onStart() {
    m_canvas = spawn("Lobby");
    UICanvas layer;
    layer.sortOrder = 20;
    scene().add(m_canvas, std::move(layer));

    // Two a side to begin with, each looking different.
    addPlayer(Color::White);
    addPlayer(Color::Black);
    addPlayer(Color::White);
    addPlayer(Color::Black);
    m_players[0].local = true;  // until there is a network, the first player is the one here
    for (size_t i = 0; i < m_players.size(); ++i) {
        m_players[i].skin  = SKIN_ORDER[(i / 2) % std::size(SKIN_ORDER)];
        m_players[i].takes = static_cast<TakeStyle>(i % static_cast<size_t>(TakeStyle::Count));
    }

    subscribe([this](const UIClickEvent& click) {
        if (click.eventId.rfind("lobby/", 0) == 0) m_clicks.push_back(click.eventId);
    });
    rebuild();
}

void Lobby::onUpdate(float) {
    if (m_clicks.empty() || !m_game || m_game->started()) return;
    const std::vector<std::string> clicks = std::move(m_clicks);
    m_clicks.clear();
    for (const std::string& id : clicks) handle(id);
    if (!m_game->started()) rebuild();
}

void Lobby::addPlayer(Color side) {
    if (count(side) >= MAX_PER_SIDE) return;
    PlayerSetup player;
    player.name  = "Player " + std::to_string(m_nextNumber++);
    player.side  = side;
    player.color = freeColor(PALETTE[std::size(PALETTE) - 1], m_players.size());
    m_players.push_back(player);
}

// The next colour in the palette after @p after that nobody but @p self wears.
glm::vec3 Lobby::freeColor(const glm::vec3& after, size_t self) const {
    size_t start = 0;
    for (size_t i = 0; i < std::size(PALETTE); ++i) {
        if (PALETTE[i] == after) start = i + 1;
    }
    for (size_t step = 0; step < std::size(PALETTE); ++step) {
        const glm::vec3& color = PALETTE[(start + step) % std::size(PALETTE)];
        bool taken = false;
        for (size_t p = 0; p < m_players.size(); ++p) taken = taken || (p != self && m_players[p].color == color);
        if (!taken) return color;
    }
    return after;
}

int Lobby::count(Color side) const {
    return static_cast<int>(std::count_if(m_players.begin(), m_players.end(), [&](const PlayerSetup& p) { return p.side == side; }));
}

void Lobby::handle(const std::string& id) {
    const Click  click = parse(id);
    const size_t i     = click.arg.empty() || !std::isdigit(static_cast<unsigned char>(click.arg[0])) ? m_players.size() : std::stoul(click.arg);
    const bool   valid = i < m_players.size();

    if (click.verb == "add") addPlayer(click.arg == "white" ? Color::White : Color::Black);
    else if (click.verb == "time") m_turnChoice = (m_turnChoice + 1) % std::size(TURN_CHOICES);
    else if (click.verb == "color" && valid) m_players[i].color = freeColor(m_players[i].color, i);
    else if (click.verb == "skin" && valid) m_players[i].skin = nextOf(SKIN_ORDER, m_players[i].skin);
    else if (click.verb == "takes" && valid) {
        m_players[i].takes = static_cast<TakeStyle>((static_cast<int>(m_players[i].takes) + 1) % static_cast<int>(TakeStyle::Count));
    } else if (click.verb == "team" && valid) {
        const Color other = Chess::opposite(m_players[i].side);
        if (count(other) < MAX_PER_SIDE) m_players[i].side = other;
    } else if (click.verb == "remove" && valid) {
        m_players.erase(m_players.begin() + static_cast<std::ptrdiff_t>(i));
    } else if (click.verb == "start" && count(Color::White) > 0 && count(Color::Black) > 0) {
        m_game->turnSeconds = TURN_CHOICES[m_turnChoice];
        m_game->setPlayers(m_players);
        m_game->begin();
        destroy(m_canvas);
    }
}

// The whole screen made again from the players, and the heads round the table set to match.
void Lobby::rebuild() {
    if (m_root) destroy(m_root);
    m_root = spawn("Lobby Screen", m_canvas);
    UIElement whole;
    whole.relativeSize = {1.0f, 1.0f};
    whole.size         = {0.0f, 0.0f};
    scene().add(m_root, std::move(whole));

    label(m_root, UIElement::at({0.5f, 0.0f}, {0.0f, 50.0f}, {1400.0f, 110.0f}), "BACKSTAB CHESS", 96.0f, GOLD, UIText::Align::Center);
    label(m_root, UIElement::at({0.5f, 0.0f}, {0.0f, 160.0f}, {1400.0f, 40.0f}), "Chess where your own team is out to get you",
          26.0f, glm::vec4(glm::vec3(PALE_INK), 0.8f), UIText::Align::Center);

    const float offset = (PANEL_WIDTH + PANEL_GAP) * 0.5f;
    spawnTeam(Color::White, -offset);
    spawnTeam(Color::Black, offset);

    label(m_root, UIElement::at({0.5f, 1.0f}, {0.0f, -150.0f}, {1200.0f, 30.0f}), "Click a colour, a skin or a take style to change it",
          20.0f, glm::vec4(glm::vec3(PALE_INK), 0.65f), UIText::Align::Center);
    const std::string time = "Turn  " + std::to_string(static_cast<int>(TURN_CHOICES[m_turnChoice])) + "s";
    button(m_root, UIElement::at({0.5f, 1.0f}, {-190.0f, -48.0f}, {260.0f, 68.0f}), time, "lobby/time", 28.0f,
           {0.1f, 0.1f, 0.14f, 0.9f}, PALE_INK, 34.0f);
    const bool     ready = count(Color::White) > 0 && count(Color::Black) > 0;
    const EntityId start = button(m_root, UIElement::at({0.5f, 1.0f}, {160.0f, -44.0f}, {320.0f, 76.0f}), "START", "lobby/start", 38.0f,
                                  GOLD, DARK_INK, 38.0f);
    if (UIButton* go = scene().tryGet<UIButton>(start)) go->interactable = ready;

    if (m_game) m_game->setPlayers(m_players);
}

void Lobby::spawnTeam(Color side, float x) {
    const bool      white = side == Color::White;
    const int       n     = count(side);
    const float     high  = CARDS_TOP + CARD_STEP * static_cast<float>(n) + (n < MAX_PER_SIDE ? 60.0f : 0.0f) + 14.0f;
    const glm::vec4 ink   = white ? DARK_INK : PALE_INK;
    const glm::vec4 card  = white ? glm::vec4(0.84f, 0.81f, 0.76f, 1.0f) : glm::vec4(0.14f, 0.14f, 0.17f, 1.0f);
    const glm::vec4 knob  = white ? glm::vec4(0.74f, 0.71f, 0.66f, 1.0f) : glm::vec4(0.23f, 0.23f, 0.28f, 1.0f);

    const EntityId team = panel(m_root, UIElement::at({0.5f, 0.0f}, {x, PANEL_TOP}, {PANEL_WIDTH, high}),
                                white ? glm::vec4(0.93f, 0.91f, 0.86f, 0.94f) : glm::vec4(0.05f, 0.05f, 0.07f, 0.92f), 24.0f);
    label(team, UIElement::at({0.0f, 0.0f}, {26.0f, 16.0f}, {300.0f, 48.0f}), white ? "WHITE" : "BLACK", 38.0f, ink);
    label(team, UIElement::at({1.0f, 0.0f}, {-26.0f, 16.0f}, {200.0f, 48.0f}), std::to_string(n) + " / " + std::to_string(MAX_PER_SIDE),
          24.0f, glm::vec4(glm::vec3(ink), 0.6f), UIText::Align::Right);

    float y = CARDS_TOP;
    for (size_t i = 0; i < m_players.size(); ++i) {
        const PlayerSetup& p = m_players[i];
        if (p.side != side) continue;
        const std::string  index = std::to_string(i);
        const EntityId     row = panel(team, UIElement::at({0.0f, 0.0f}, {16.0f, y}, {PANEL_WIDTH - 32.0f, CARD_HEIGHT}), card, 16.0f);
        button(row, UIElement::at({0.0f, 0.0f}, {10.0f, 10.0f}, {44.0f, 44.0f}), "", "lobby/color/" + index, 1.0f, glm::vec4(p.color, 1.0f), ink, 22.0f);
        label(row, UIElement::at({0.0f, 0.0f}, {66.0f, 0.0f}, {150.0f, CARD_HEIGHT}), p.name, 26.0f, ink);
        button(row, UIElement::at({0.0f, 0.0f}, {214.0f, 10.0f}, {156.0f, 44.0f}), skinLabel(p.skin, p.side), "lobby/skin/" + index, 22.0f, knob, ink, 12.0f);
        button(row, UIElement::at({0.0f, 0.0f}, {378.0f, 10.0f}, {104.0f, 44.0f}), takeStyleName(p.takes), "lobby/takes/" + index, 22.0f, knob, ink, 12.0f);
        button(row, UIElement::at({0.0f, 0.0f}, {490.0f, 10.0f}, {34.0f, 44.0f}), "<>", "lobby/team/" + index, 20.0f, knob, ink, 12.0f);
        button(row, UIElement::at({0.0f, 0.0f}, {530.0f, 10.0f}, {28.0f, 44.0f}), "X", "lobby/remove/" + index, 20.0f, knob, ink, 12.0f);
        y += CARD_STEP;
    }
    if (n < MAX_PER_SIDE) {
        button(team, UIElement::at({0.0f, 0.0f}, {16.0f, y}, {PANEL_WIDTH - 32.0f, 52.0f}), "+  Add player",
               white ? "lobby/add/white" : "lobby/add/black", 24.0f, glm::vec4(glm::vec3(card), 0.55f), ink, 16.0f);
    }
}

EntityId Lobby::panel(EntityId parent, UIElement place, const glm::vec4& color, float corner) {
    const EntityId id = spawn("Panel", parent);
    scene().add(id, std::move(place));
    UIImage fill;
    fill.color              = color;
    fill.shape.cornerRadius = corner;
    scene().add(id, std::move(fill));
    return id;
}

EntityId Lobby::label(EntityId parent, UIElement place, const std::string& text, float size, const glm::vec4& color, UIText::Align align) {
    const EntityId id = spawn("Label", parent);
    scene().add(id, std::move(place));
    UIText words;
    words.text      = text;
    words.pixelSize = size;
    words.color     = color;
    words.align     = align;
    words.valign    = UIText::VAlign::Middle;
    scene().add(id, std::move(words));
    return id;
}

// A button with its label on it: lighter under the pointer, darker pressed.
EntityId Lobby::button(EntityId parent, UIElement place, const std::string& text, const std::string& id, float size,
                       const glm::vec4& fill, const glm::vec4& ink, float corner) {
    const EntityId entity = spawn("Button", parent);
    scene().add(entity, std::move(place));
    UIButton press;
    press.normalColor        = fill;
    press.hoverColor         = glm::vec4(glm::mix(glm::vec3(fill), glm::vec3(1.0f), 0.18f), fill.a);
    press.pressedColor       = glm::vec4(glm::vec3(fill) * 0.8f, fill.a);
    press.disabledColor      = glm::vec4(glm::vec3(fill) * 0.5f, fill.a * 0.6f);
    press.shape.cornerRadius = corner;
    press.eventId            = id;
    scene().add(entity, std::move(press));
    if (!text.empty()) {
        UIText words;
        words.text      = text;
        words.pixelSize = size;
        words.color     = ink;
        words.align     = UIText::Align::Center;
        words.valign    = UIText::VAlign::Middle;
        scene().add(entity, std::move(words));
    }
    return entity;
}

} // namespace Game
