#pragma once

// Small immediate-mode UI toolkit on top of raylib: animated buttons, toggle
// chips, sliders, swatches, tooltips, clipping and input blocking.

#include "Rgba.hpp"
#include "Vec2.hpp"

#include <raylib.h>

#include <string>
#include <unordered_map>
#include <vector>

[[nodiscard]] inline Color toColor(Rgba c) { return Color{c.r, c.g, c.b, c.a}; }
[[nodiscard]] inline Rgba toRgba(Color c) { return Rgba{c.r, c.g, c.b, c.a}; }
[[nodiscard]] inline Vector2 toVector2(Vec2 v) { return Vector2{static_cast<float>(v.x), static_cast<float>(v.y)}; }
[[nodiscard]] inline Vec2 toVec2(Vector2 v) { return Vec2{v.x, v.y}; }
[[nodiscard]] Color mixColor(Color a, Color b, float t);

namespace theme {
inline constexpr Color kBg{7, 9, 17, 255};
inline constexpr Color kPanel{12, 17, 29, 240};
inline constexpr Color kPanelBorder{58, 80, 118, 210};
inline constexpr Color kCard{17, 24, 39, 235};
inline constexpr Color kButton{27, 37, 58, 245};
inline constexpr Color kGold{255, 200, 70, 255};
inline constexpr Color kSky{100, 200, 255, 255};
inline constexpr Color kText{232, 238, 248, 255};
inline constexpr Color kDim{146, 158, 181, 255};
inline constexpr Color kGood{110, 230, 140, 255};
inline constexpr Color kWarn{255, 176, 66, 255};
inline constexpr Color kDanger{255, 92, 82, 255};
inline constexpr Color kViolet{185, 135, 255, 255};
} // namespace theme

enum class ButtonStyle { Normal, Accent, Good, Danger };

class Ui {
public:
    float scale{1.0f};
    bool inputEnabled{true}; // false while a modal (help) covers the widgets
    [[nodiscard]] float s(float v) const { return v * scale; }

    void loadFonts();
    void unloadFonts();

    void beginFrame(float dt);
    void endFrame(); // draws the pending tooltip

    // --- Text ---
    void text(const char* t, float x, float y, float size, Color c, bool bold = false) const;
    void textCentered(const char* t, Rectangle r, float size, Color c, bool bold = false) const;
    void textRight(const char* t, float rightX, float y, float size, Color c, bool bold = false) const;
    [[nodiscard]] float measure(const char* t, float size, bool bold = false) const;

    // --- Containers (both also block world input under them) ---
    void panel(Rectangle r, Color fill = theme::kPanel, Color border = theme::kPanelBorder);
    // Draws a titled card and returns the y coordinate where content starts.
    float card(Rectangle r, const char* title, Color accent);

    // --- Widgets (return true when clicked / changed) ---
    bool button(const char* id, Rectangle r, const char* label, ButtonStyle style = ButtonStyle::Normal,
                bool active = false, const char* tooltip = nullptr, float fontSize = 0.0f);
    bool chip(const char* id, Rectangle r, const char* label, bool on, Color accent, const char* tooltip = nullptr);
    bool slider(const char* id, Rectangle r, double& value, double lo, double hi, bool logScale, Color accent,
                const char* tooltip = nullptr);
    bool swatch(const char* id, Rectangle r, Color c, bool selected);

    // --- Input routing ---
    void block(Rectangle r) { blocks_.push_back(r); }
    [[nodiscard]] bool mouseOverUI(Vector2 m) const;
    [[nodiscard]] bool capturingMouse() const { return activeId_ != 0; }
    [[nodiscard]] bool wantsHandCursor() const { return hot_; } // call after drawing widgets

    void pushClip(Rectangle r);
    void popClip();

    // Shows a tooltip at the mouse this frame (immediately).
    void showTooltip(const char* text);

    [[nodiscard]] float time() const { return time_; }
    [[nodiscard]] float dt() const { return dt_; }

    // Rounded rectangle helpers with corner radius in pixels.
    void roundedRect(Rectangle r, float radiusPx, Color c) const;
    void roundedOutline(Rectangle r, float radiusPx, float thick, Color c) const;

private:
    [[nodiscard]] static unsigned idOf(const char* id);
    [[nodiscard]] bool hovering(Rectangle r) const;
    float animate(unsigned id, float target, float rate);
    void hoverTooltip(unsigned id, bool hovered, const char* tip);
    [[nodiscard]] bool takeClick(bool hovered);

    Font font_{};
    Font bold_{};
    bool fontLoaded_{false};
    bool boldLoaded_{false};

    std::unordered_map<unsigned, float> anims_;
    unsigned activeId_{0};
    float dt_{1.0f / 60.0f};
    float time_{0.0f};
    std::vector<Rectangle> blocks_;
    std::vector<Rectangle> prevBlocks_;
    std::vector<Rectangle> clipStack_;
    bool hot_{false};
    bool hotPrev_{false};
    bool clickTaken_{false};

    unsigned tipOwner_{0};
    float tipTimer_{0.0f};
    std::string tipText_;
    bool tipActive_{false};
};
