#include "ui_kit.h"

#include <algorithm>
#include <cmath>

#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_image.h"

#include "profile.h"

namespace Game::Ui {

namespace {

int g_kits = 0;  ///< So two kits' click ids never meet.

constexpr float NAME_SHARE = 0.44f;  ///< Of a row's width, the part its name takes.
constexpr float VALUE_WIDE = 84.0f;  ///< A slider's value, right of its track.

glm::vec4 lighter(const glm::vec4& c, float by) { return glm::vec4(glm::mix(glm::vec3(c), glm::vec3(1.0f), by), c.a); }

} // namespace

void Kit::bind(Scene& scene, ResourceManager& resources, InputMap& input, Spawn spawn, Kill kill) {
    m_scene     = &scene;
    m_resources = &resources;
    m_input     = &input;
    m_spawn     = std::move(spawn);
    m_kill      = std::move(kill);
    m_prefix    = "ui" + std::to_string(g_kits++) + "/";
}

void Kit::reset() {
    m_acts.clear();
    m_heard.clear();
    m_toggles.clear();
    m_sliders.clear();
    m_choices.clear();
    m_keys.clear();
    m_fields.clear();
    m_field     = -1;
    m_listening = -1;
}

std::string Kit::newId() { return m_prefix + std::to_string(m_count++); }

bool Kit::click(const std::string& id) {
    if (id.rfind(m_prefix, 0) != 0) return false;
    m_heard.push_back(id);
    return true;
}

void Kit::update(float dt) {
    m_time += dt;

    // Clicks, a copy of each act taken first: an act may rebuild the screen under it.
    const std::vector<std::string> heard = std::move(m_heard);
    m_heard.clear();
    for (const std::string& id : heard) {
        const auto it = std::find_if(m_acts.begin(), m_acts.end(), [&](const auto& a) { return a.first == id; });
        if (it == m_acts.end()) continue;
        const std::function<void()> act = it->second;
        act();
    }

    for (Slider& slider : m_sliders) {
        const UIButton* hit = m_scene->tryGet<UIButton>(slider.hit);
        if (!hit || !hit->held || hit->resolvedSize.x <= 0.0f) continue;
        const float u     = std::clamp(hit->pointer.x / hit->resolvedSize.x, 0.0f, 1.0f);
        const float value = slider.lo + (slider.hi - slider.lo) * u;
        if (value == slider.value) continue;
        slider.value = value;
        showSlider(slider);
        if (slider.set) slider.set(value);
    }

    if (m_listening >= 0) {
        for (int key = 32; key <= GLFW_KEY_LAST; ++key) {
            if (!m_input->typed(key)) continue;
            Key& binding = m_keys[static_cast<size_t>(m_listening)];
            const int index = m_listening;
            m_listening = -1;
            if (key != GLFW_KEY_ESCAPE) {
                binding.key = key;
                if (binding.set) binding.set(key);
            }
            showKey(static_cast<size_t>(index));
            break;
        }
    }

    if (m_field >= 0) {
        Field& field = m_fields[static_cast<size_t>(m_field)];
        bool   changed = false;
        for (const char32_t c : m_input->text()) {
            if (c >= 32 && c < 127 && field.text.size() < field.max) {
                field.text += static_cast<char>(c);
                changed = true;
            }
        }
        if (m_input->typed(GLFW_KEY_BACKSPACE) && !field.text.empty()) {
            field.text.pop_back();
            changed = true;
        }
        if (m_input->typed(GLFW_KEY_ENTER) || m_input->typed(GLFW_KEY_ESCAPE)) {
            endField();
        } else {
            (void)changed;
            showField(static_cast<size_t>(m_field));
        }
    }
}

void Kit::endField() {
    if (m_field < 0) return;
    Field& field = m_fields[static_cast<size_t>(m_field)];
    const size_t index = static_cast<size_t>(m_field);
    m_field = -1;
    if (field.set) field.set(field.text);
    showField(index);
}

EntityId Kit::group(EntityId parent, UIElement place) {
    const EntityId id = m_spawn("Group", parent);
    m_scene->add(id, std::move(place));
    return id;
}

EntityId Kit::panel(EntityId parent, UIElement place, const glm::vec4& fill, float corner, const glm::vec4& border, float borderWidth) {
    const EntityId id = m_spawn("Panel", parent);
    m_scene->add(id, std::move(place));
    UIImage image;
    image.color              = fill;
    image.shape.cornerRadius = corner;
    image.shape.borderWidth  = borderWidth;
    image.shape.borderColor  = border;
    m_scene->add(id, std::move(image));
    return id;
}

EntityId Kit::image(EntityId parent, UIElement place, TextureHandle texture, float corner) {
    const EntityId id = m_spawn("Image", parent);
    m_scene->add(id, std::move(place));
    UIImage image;
    image.texture            = texture;
    image.shape.cornerRadius = corner;
    m_scene->add(id, std::move(image));
    return id;
}

EntityId Kit::label(EntityId parent, UIElement place, const std::string& text, float size, const glm::vec4& color, UIText::Align align) {
    const EntityId id = m_spawn("Label", parent);
    m_scene->add(id, std::move(place));
    UIText words;
    words.text      = text;
    words.pixelSize = size;
    words.color     = color;
    words.align     = align;
    words.valign    = UIText::VAlign::Middle;
    m_scene->add(id, std::move(words));
    return id;
}

EntityId Kit::button(EntityId parent, UIElement place, const std::string& text, std::function<void()> act, const Style& style) {
    const EntityId entity = m_spawn("Button", parent);
    m_scene->add(entity, std::move(place));
    UIButton press;
    press.normalColor        = style.fill;
    press.hoverColor         = style.fill.a > 0.0f ? lighter(style.fill, 0.16f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.06f);
    press.pressedColor       = glm::vec4(glm::vec3(style.fill) * 0.8f, std::max(style.fill.a, 0.1f));
    press.disabledColor      = glm::vec4(glm::vec3(style.fill) * 0.5f, style.fill.a * 0.6f);
    press.shape.cornerRadius = style.corner;
    press.shape.borderColor  = style.border;
    press.shape.borderWidth  = style.border.a > 0.0f ? 2.0f : 0.0f;
    press.eventId            = newId();
    m_acts.emplace_back(press.eventId, std::move(act));
    m_scene->add(entity, std::move(press));
    if (!text.empty()) {
        UIText words;
        words.text      = style.align == UIText::Align::Left ? "   " + text : text;
        words.pixelSize = style.size;
        words.color     = style.ink;
        words.align     = style.align;
        words.valign    = UIText::VAlign::Middle;
        m_scene->add(entity, std::move(words));
    }
    return entity;
}

void Kit::heading(EntityId parent, float y, float width, const std::string& text) {
    label(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y + 10.0f}, {width, 34.0f}), text, 20.0f, GOLD);
    panel(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y + ROW - 6.0f}, {width, 1.0f}), glm::vec4(glm::vec3(GOLD), 0.25f), 0.0f);
}

void Kit::toggleRow(EntityId parent, float y, float width, const std::string& name, bool on, std::function<void(bool)> set) {
    const float x = width * NAME_SHARE;
    label(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {x, ROW}), name, 23.0f, INK);
    const size_t index = m_toggles.size();
    Toggle toggle;
    toggle.on  = on;
    toggle.set = std::move(set);
    toggle.pill = button(parent, UIElement::at({0.0f, 0.0f}, {x, y + (ROW - 32.0f) * 0.5f}, {64.0f, 32.0f}), "", [this, index] {
        Toggle& t = m_toggles[index];
        t.on = !t.on;
        showToggle(t);
        if (t.set) t.set(t.on);
    }, Style{FIELD, INK, 20.0f, 16.0f});
    toggle.knob = panel(toggle.pill, UIElement::at({0.0f, 0.5f}, {4.0f, 0.0f}, {24.0f, 24.0f}), INK, 12.0f);
    m_toggles.push_back(toggle);
    showToggle(m_toggles.back());
}

void Kit::showToggle(const Toggle& toggle) {
    if (UIButton* pill = m_scene->tryGet<UIButton>(toggle.pill)) {
        pill->normalColor = toggle.on ? GOLD : FIELD;
        pill->hoverColor  = lighter(pill->normalColor, 0.16f);
    }
    if (UIElement* knob = m_scene->tryGet<UIElement>(toggle.knob)) knob->position.x = toggle.on ? 36.0f : 4.0f;
}

void Kit::sliderRow(EntityId parent, float y, float width, const std::string& name, float value, float lo, float hi,
                    std::function<void(float)> set, std::function<std::string(float)> show, TextureHandle track) {
    const float x    = width * NAME_SHARE;
    const float trackWide = width - x - VALUE_WIDE - 20.0f;
    label(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {x, ROW}), name, 23.0f, INK);
    Slider slider;
    slider.lo    = lo;
    slider.hi    = hi;
    slider.value = std::clamp(value, lo, hi);
    slider.width = trackWide;
    slider.set   = std::move(set);
    slider.show  = std::move(show);
    const float    trackY = y + ROW * 0.5f;
    const float    tall   = track ? 12.0f : 8.0f;
    const EntityId bar    = track ? image(parent, UIElement::at({0.0f, 0.0f}, {x, trackY - tall * 0.5f}, {trackWide, tall}), track, 6.0f)
                                  : panel(parent, UIElement::at({0.0f, 0.0f}, {x, trackY - tall * 0.5f}, {trackWide, tall}), FIELD, 4.0f);
    slider.fill = track ? EntityId{} : panel(bar, UIElement::at({0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, tall}), GOLD, 4.0f);
    slider.knob = panel(parent, UIElement::at({0.0f, 0.0f}, {x - 11.0f, trackY - 11.0f}, {22.0f, 22.0f}), INK, 11.0f,
                        glm::vec4(0.0f, 0.0f, 0.0f, 0.5f), 2.0f);
    slider.text = label(parent, UIElement::at({0.0f, 0.0f}, {width - VALUE_WIDE, y}, {VALUE_WIDE, ROW}), "", 22.0f, INK_DIM, UIText::Align::Right);
    // The hit area, clear over the track: a drag anywhere along it sets the value.
    Style clear{{0.0f, 0.0f, 0.0f, 0.0f}, INK, 1.0f, 0.0f};
    slider.hit = button(parent, UIElement::at({0.0f, 0.0f}, {x, trackY - 18.0f}, {trackWide, 36.0f}), "", [] {}, clear);
    if (UIButton* hit = m_scene->tryGet<UIButton>(slider.hit)) hit->hoverColor = {0.0f, 0.0f, 0.0f, 0.0f};
    m_sliders.push_back(slider);
    showSlider(m_sliders.back());
}

void Kit::showSlider(const Slider& slider) {
    const float u = slider.hi > slider.lo ? (slider.value - slider.lo) / (slider.hi - slider.lo) : 0.0f;
    if (UIElement* fill = m_scene->tryGet<UIElement>(slider.fill)) fill->size.x = u * slider.width;
    if (UIElement* knob = m_scene->tryGet<UIElement>(slider.knob)) {
        // The knob's left edge rides from the track's start, centred over the value.
        if (const UIElement* hit = m_scene->tryGet<UIElement>(slider.hit)) knob->position.x = hit->position.x + u * slider.width - 11.0f;
    }
    if (UIText* text = m_scene->tryGet<UIText>(slider.text)) text->text = slider.show ? slider.show(slider.value) : "";
}

void Kit::choiceRow(EntityId parent, float y, float width, const std::string& name, std::vector<std::string> options, int index,
                    std::function<void(int)> set) {
    const float x    = width * NAME_SHARE;
    const float wide = width - x;
    label(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {x, ROW}), name, 23.0f, INK);
    const size_t slot = m_choices.size();
    Choice choice;
    choice.options = std::move(options);
    choice.index   = std::clamp(index, 0, static_cast<int>(choice.options.size()) - 1);
    choice.set     = std::move(set);
    panel(parent, UIElement::at({0.0f, 0.0f}, {x, y + 7.0f}, {wide, ROW - 14.0f}), FIELD, 10.0f);
    choice.text = label(parent, UIElement::at({0.0f, 0.0f}, {x + 44.0f, y}, {wide - 88.0f, ROW}), choice.options[static_cast<size_t>(choice.index)],
                        22.0f, INK, UIText::Align::Center);
    const auto step = [this, slot](int by) {
        return [this, slot, by] {
            Choice& c = m_choices[slot];
            const int n = static_cast<int>(c.options.size());
            c.index = (c.index + by + n) % n;
            if (UIText* text = m_scene->tryGet<UIText>(c.text)) text->text = c.options[static_cast<size_t>(c.index)];
            if (c.set) c.set(c.index);
        };
    };
    const Style arrow{{0.0f, 0.0f, 0.0f, 0.0f}, GOLD, 26.0f, 10.0f};
    button(parent, UIElement::at({0.0f, 0.0f}, {x, y + 7.0f}, {44.0f, ROW - 14.0f}), "<", step(-1), arrow);
    button(parent, UIElement::at({0.0f, 0.0f}, {width - 44.0f, y + 7.0f}, {44.0f, ROW - 14.0f}), ">", step(1), arrow);
    m_choices.push_back(choice);
}

void Kit::keyRow(EntityId parent, float y, float width, const std::string& name, int key, std::function<void(int)> set) {
    const float x = width * NAME_SHARE;
    label(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {x, ROW}), name, 23.0f, INK);
    const size_t index = m_keys.size();
    Key binding;
    binding.key = key;
    binding.set = std::move(set);
    binding.box = button(parent, UIElement::at({0.0f, 0.0f}, {x, y + 7.0f}, {width - x, ROW - 14.0f}), keyName(key), [this, index] {
        endField();
        const int was = m_listening;
        m_listening = static_cast<int>(index);
        if (was >= 0) showKey(static_cast<size_t>(was));
        showKey(index);
    }, Style{FIELD, INK, 22.0f, 10.0f});
    m_keys.push_back(binding);
}

void Kit::showKey(size_t index) {
    const Key& binding = m_keys[index];
    const bool waiting = m_listening == static_cast<int>(index);
    if (UIText* text = m_scene->tryGet<UIText>(binding.box)) {
        text->text  = waiting ? "Press a key...  (Esc to keep)" : keyName(binding.key);
        text->color = waiting ? GOLD : INK;
    }
}

void Kit::fieldRow(EntityId parent, float y, float width, const std::string& name, const std::string& text, size_t max,
                   std::function<void(const std::string&)> set) {
    const float x = width * NAME_SHARE;
    label(parent, UIElement::at({0.0f, 0.0f}, {0.0f, y}, {x, ROW}), name, 23.0f, INK);
    const size_t index = m_fields.size();
    Field field;
    field.text = text;
    field.max  = max;
    field.set  = std::move(set);
    field.box  = button(parent, UIElement::at({0.0f, 0.0f}, {x, y + 7.0f}, {width - x, ROW - 14.0f}), text, [this, index] {
        if (m_field == static_cast<int>(index)) return;
        endField();
        m_listening = -1;
        m_field     = static_cast<int>(index);
        showField(index);
    }, Style{FIELD, INK, 22.0f, 10.0f, UIText::Align::Left});
    m_fields.push_back(field);
}

void Kit::showField(size_t index) {
    const Field& field = m_fields[index];
    const bool   focus = m_field == static_cast<int>(index);
    if (UIText* text = m_scene->tryGet<UIText>(field.box)) {
        const bool caret = focus && std::fmod(m_time, 1.0f) < 0.55f;
        text->text  = "   " + field.text + (caret ? "|" : "");
        text->color = focus ? GOLD : INK;
    }
}

} // namespace Game::Ui
