#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_text.h"
#include "resource/asset/texture_asset.h"
#include "system/script/behavior_api.h"

// The game's widgets: panels, labels and buttons, and labelled rows of toggles, sliders, choices,
// key bindings and text fields, all in one look. A screen is built from them and they keep
// themselves up to date in place, so a click or a drag needs no rebuild; the owner forwards its
// clicks and calls update() each frame.
namespace Game::Ui {

using namespace Vkm::Engine;

// The look: dark glass, warm gold to pick things out.
inline const glm::vec4 INK         = {0.96f, 0.94f, 0.9f, 1.0f};
inline const glm::vec4 INK_DIM     = {0.96f, 0.94f, 0.9f, 0.6f};
inline const glm::vec4 GOLD        = {1.0f, 0.76f, 0.3f, 1.0f};
inline const glm::vec4 GLASS       = {0.04f, 0.045f, 0.065f, 0.86f};
inline const glm::vec4 GLASS_LIGHT = {0.12f, 0.13f, 0.17f, 0.9f};
inline const glm::vec4 FIELD       = {0.16f, 0.17f, 0.22f, 1.0f};
inline const glm::vec4 RED         = {1.0f, 0.35f, 0.25f, 1.0f};

/// How a button looks.
struct Style {
    glm::vec4     fill   = FIELD;
    glm::vec4     ink    = INK;
    float         size   = 24.0f;
    float         corner = 12.0f;
    UIText::Align align  = UIText::Align::Center;
    glm::vec4     border = {0.0f, 0.0f, 0.0f, 0.0f};
};

class Kit {
    public:
        using Spawn = std::function<EntityId(const char*, EntityId)>;
        using Kill  = std::function<void(EntityId)>;

        /// What the widgets are made in, and with; once, from the owner's onStart.
        void bind(Scene& scene, ResourceManager& resources, InputMap& input, Spawn spawn, Kill kill);

        /// Forgets every widget, before the screen is built again.
        void reset();

        /// A click the owner heard; true when it was one of this kit's.
        bool click(const std::string& id);

        /// Runs the clicks heard, drags the sliders, types into the field in focus.
        void update(float dt);

        /// Whether a field or a key binding is listening, so the keyboard is the UI's.
        bool capturing() const { return m_field >= 0 || m_listening >= 0; }

        EntityId group(EntityId parent, UIElement place);
        EntityId panel(EntityId parent, UIElement place, const glm::vec4& fill, float corner, const glm::vec4& border = {}, float borderWidth = 0.0f);
        EntityId label(EntityId parent, UIElement place, const std::string& text, float size, const glm::vec4& color,
                       UIText::Align align = UIText::Align::Left);
        EntityId button(EntityId parent, UIElement place, const std::string& text, std::function<void()> act, const Style& style = {});
        EntityId image(EntityId parent, UIElement place, TextureHandle texture, float corner = 0.0f);

        // Rows: a name on the left of @p width, its control on the right, ROW tall from @p y.
        static constexpr float ROW = 54.0f;
        void heading(EntityId parent, float y, float width, const std::string& text);
        void toggleRow(EntityId parent, float y, float width, const std::string& name, bool on, std::function<void(bool)> set);
        void sliderRow(EntityId parent, float y, float width, const std::string& name, float value, float lo, float hi,
                       std::function<void(float)> set, std::function<std::string(float)> show, TextureHandle track = {});
        void choiceRow(EntityId parent, float y, float width, const std::string& name, std::vector<std::string> options, int index,
                       std::function<void(int)> set);
        void keyRow(EntityId parent, float y, float width, const std::string& name, int key, std::function<void(int)> set);
        void fieldRow(EntityId parent, float y, float width, const std::string& name, const std::string& text, size_t max,
                      std::function<void(const std::string&)> set);

    private:
        struct Toggle {
            EntityId pill, knob;
            bool     on = false;
            std::function<void(bool)> set;
        };
        struct Slider {
            EntityId hit, fill, knob, text;
            float    lo = 0.0f, hi = 1.0f, value = 0.0f, width = 0.0f;
            std::function<void(float)>        set;
            std::function<std::string(float)> show;
        };
        struct Choice {
            EntityId text;
            std::vector<std::string> options;
            int index = 0;
            std::function<void(int)> set;
        };
        struct Key {
            EntityId box;
            int      key = 0;
            std::function<void(int)> set;
        };
        struct Field {
            EntityId box;
            std::string text;
            size_t      max = 16;
            std::function<void(const std::string&)> set;
        };

        std::string newId();
        void showToggle(const Toggle& toggle);
        void showSlider(const Slider& slider);
        void showKey(size_t index);
        void showField(size_t index);
        void endField();

    private:
        Scene*           m_scene     = nullptr;
        ResourceManager* m_resources = nullptr;
        InputMap*        m_input     = nullptr;
        Spawn            m_spawn;
        Kill             m_kill;
        std::string      m_prefix;
        int              m_count = 0;

        std::vector<std::pair<std::string, std::function<void()>>> m_acts;
        std::vector<std::string> m_heard;
        std::vector<Toggle>      m_toggles;
        std::vector<Slider>      m_sliders;
        std::vector<Choice>      m_choices;
        std::vector<Key>         m_keys;
        std::vector<Field>       m_fields;
        int                      m_field     = -1;  ///< The field typed into, or none.
        int                      m_listening = -1;  ///< The key binding waiting for a key, or none.
        float                    m_time      = 0.0f;
};

} // namespace Game::Ui
