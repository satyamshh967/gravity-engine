#include "Ui.hpp"

#include <algorithm>
#include <cmath>

Color mixColor(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto m = [t](unsigned char p, unsigned char q) {
        return static_cast<unsigned char>(std::clamp(p + (q - p) * t, 0.0f, 255.0f));
    };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

// ---------------------------------------------------------------------------
// Fonts: try common system fonts on Windows, Linux and macOS, fall back to
// raylib's built-in font. Loaded large with mipmaps + trilinear filtering so
// text stays crisp when drawn small or scaled for high-DPI displays.
// ---------------------------------------------------------------------------

namespace {
Font loadCrispFont(const char* const* candidates, bool& ok) {
    for (const char* const* p = candidates; *p; ++p) {
        if (!FileExists(*p)) continue;
        Font f = LoadFontEx(*p, 64, nullptr, 0);
        if (f.texture.id == 0) continue;
        GenTextureMipmaps(&f.texture);
        SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
        ok = true;
        return f;
    }
    ok = false;
    return Font{};
}
} // namespace

void Ui::loadFonts() {
    static const char* const regular[] = {
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf", nullptr};
    static const char* const bold[] = {
        "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf", nullptr};
    font_ = loadCrispFont(regular, fontLoaded_);
    if (fontLoaded_) bold_ = loadCrispFont(bold, boldLoaded_);
}

void Ui::unloadFonts() {
    if (boldLoaded_) UnloadFont(bold_);
    if (fontLoaded_) UnloadFont(font_);
    boldLoaded_ = fontLoaded_ = false;
}

void Ui::text(const char* t, float x, float y, float size, Color c, bool bold) const {
    if (fontLoaded_) {
        const Font& f = (bold && boldLoaded_) ? bold_ : font_;
        DrawTextEx(f, t, Vector2{std::round(x), std::round(y)}, size, size * 0.02f, c);
    } else {
        DrawTextEx(GetFontDefault(), t, Vector2{std::round(x), std::round(y)}, size, size * 0.1f, c);
    }
}

float Ui::measure(const char* t, float size, bool bold) const {
    if (fontLoaded_) {
        const Font& f = (bold && boldLoaded_) ? bold_ : font_;
        return MeasureTextEx(f, t, size, size * 0.02f).x;
    }
    return MeasureTextEx(GetFontDefault(), t, size, size * 0.1f).x;
}

void Ui::textCentered(const char* t, Rectangle r, float size, Color c, bool bold) const {
    const float w = measure(t, size, bold);
    text(t, r.x + (r.width - w) * 0.5f, r.y + (r.height - size) * 0.5f, size, c, bold);
}

void Ui::textRight(const char* t, float rightX, float y, float size, Color c, bool bold) const {
    text(t, rightX - measure(t, size, bold), y, size, c, bold);
}

// ---------------------------------------------------------------------------
// Frame lifecycle & input routing
// ---------------------------------------------------------------------------

void Ui::beginFrame(float dt) {
    dt_ = dt;
    time_ += dt;
    prevBlocks_.swap(blocks_);
    blocks_.clear();
    clipStack_.clear();
    hotPrev_ = hot_;
    hot_ = false;
    clickTaken_ = false;
    tipActive_ = false;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) activeId_ = 0;
}

bool Ui::mouseOverUI(Vector2 m) const {
    if (activeId_ != 0) return true;
    for (const auto& r : prevBlocks_) {
        if (CheckCollisionPointRec(m, r)) return true;
    }
    for (const auto& r : blocks_) {
        if (CheckCollisionPointRec(m, r)) return true;
    }
    return false;
}

void Ui::pushClip(Rectangle r) {
    clipStack_.push_back(r);
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
}

void Ui::popClip() {
    EndScissorMode();
    if (!clipStack_.empty()) clipStack_.pop_back();
    if (!clipStack_.empty()) {
        const Rectangle& r = clipStack_.back();
        BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
    }
}

unsigned Ui::idOf(const char* id) {
    unsigned h = 2166136261u; // FNV-1a
    for (const char* p = id; *p; ++p) {
        h ^= static_cast<unsigned char>(*p);
        h *= 16777619u;
    }
    return h ? h : 1u;
}

bool Ui::hovering(Rectangle r) const {
    if (!inputEnabled) return false;
    const Vector2 m = GetMousePosition();
    if (!CheckCollisionPointRec(m, r)) return false;
    if (!clipStack_.empty() && !CheckCollisionPointRec(m, clipStack_.back())) return false;
    if (activeId_ != 0) return false; // a slider drag owns the mouse
    return true;
}

float Ui::animate(unsigned id, float target, float rate) {
    float& v = anims_[id];
    v += (target - v) * std::min(1.0f, dt_ * rate);
    return v;
}

bool Ui::takeClick(bool hovered) {
    if (!hovered || clickTaken_ || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    clickTaken_ = true;
    return true;
}

void Ui::hoverTooltip(unsigned id, bool hovered, const char* tip) {
    if (!tip || !hovered) {
        if (tipOwner_ == id) tipOwner_ = 0;
        return;
    }
    if (tipOwner_ != id) {
        tipOwner_ = id;
        tipTimer_ = 0.0f;
    }
    tipTimer_ += dt_;
    if (tipTimer_ > 0.45f) {
        tipText_ = tip;
        tipActive_ = true;
    }
}

void Ui::showTooltip(const char* t) {
    tipText_ = t;
    tipActive_ = true;
}

void Ui::endFrame() {
    if (!tipActive_ || tipText_.empty()) return;
    const float size = s(14.0f);
    const float pad = s(9.0f);
    // Support multi-line tooltips
    float w = 0.0f;
    int lines = 0;
    std::size_t start = 0;
    while (start <= tipText_.size()) {
        std::size_t end = tipText_.find('\n', start);
        if (end == std::string::npos) end = tipText_.size();
        w = std::max(w, measure(tipText_.substr(start, end - start).c_str(), size));
        ++lines;
        start = end + 1;
    }
    const float lineH = size * 1.3f;
    Vector2 m = GetMousePosition();
    Rectangle r{m.x + s(16), m.y + s(18), w + pad * 2, lineH * lines + pad * 2 - (lineH - size)};
    if (r.x + r.width > GetScreenWidth() - 4) r.x = m.x - r.width - s(8);
    if (r.y + r.height > GetScreenHeight() - 4) r.y = m.y - r.height - s(8);
    roundedRect(r, s(7), Color{6, 10, 20, 245});
    roundedOutline(r, s(7), 1.5f, Fade(theme::kGold, 0.7f));
    start = 0;
    float y = r.y + pad;
    while (start <= tipText_.size()) {
        std::size_t end = tipText_.find('\n', start);
        if (end == std::string::npos) end = tipText_.size();
        text(tipText_.substr(start, end - start).c_str(), r.x + pad, y, size, theme::kText);
        y += lineH;
        start = end + 1;
    }
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

void Ui::roundedRect(Rectangle r, float radiusPx, Color c) const {
    const float m = std::min(r.width, r.height);
    if (m <= 0.0f) return;
    DrawRectangleRounded(r, std::clamp(2.0f * radiusPx / m, 0.0f, 1.0f), 8, c);
}

void Ui::roundedOutline(Rectangle r, float radiusPx, float thick, Color c) const {
    const float m = std::min(r.width, r.height);
    if (m <= 0.0f) return;
    DrawRectangleRoundedLinesEx(r, std::clamp(2.0f * radiusPx / m, 0.0f, 1.0f), 8, thick, c);
}

void Ui::panel(Rectangle r, Color fill, Color border) {
    roundedRect(r, s(12), fill);
    roundedOutline(r, s(12), 1.5f, border);
    block(r);
}

float Ui::card(Rectangle r, const char* title, Color accent) {
    roundedRect(r, s(10), theme::kCard);
    roundedOutline(r, s(10), 1.2f, Fade(theme::kPanelBorder, 0.8f));
    // accent bar
    DrawRectangleRec(Rectangle{r.x + s(12), r.y + s(13), s(3), s(14)}, accent);
    text(title, r.x + s(22), r.y + s(11), s(15), accent, true);
    return r.y + s(40);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

bool Ui::button(const char* id, Rectangle r, const char* label, ButtonStyle style, bool active, const char* tooltip,
                float fontSize) {
    const unsigned hid = idOf(id);
    const bool hovered = hovering(r);
    const bool clicked = takeClick(hovered);
    if (hovered) hot_ = true;

    const float hover = animate(hid, hovered ? 1.0f : 0.0f, 14.0f);
    float& press = anims_[hid ^ 0x9e3779b9u];
    if (clicked) press = 1.0f;
    press = std::max(0.0f, press - dt_ * 5.0f);

    Color base = theme::kButton;
    Color accent = theme::kSky;
    switch (style) {
        case ButtonStyle::Normal: break;
        case ButtonStyle::Accent: base = Color{40, 52, 86, 245}; accent = theme::kGold; break;
        case ButtonStyle::Good: base = Color{22, 56, 42, 245}; accent = theme::kGood; break;
        case ButtonStyle::Danger: base = Color{70, 24, 26, 245}; accent = theme::kDanger; break;
    }
    if (active) base = mixColor(base, accent, 0.28f);

    // Hover lifts the button slightly; a click pulses it.
    const float lift = hover * s(1.5f);
    Rectangle d{r.x, r.y - lift, r.width, r.height};
    const float grow = press * s(3.0f);
    d.x -= grow * 0.5f;
    d.y -= grow * 0.5f;
    d.width += grow;
    d.height += grow;

    if (hover > 0.01f) roundedRect(Rectangle{d.x - 2, d.y - 2, d.width + 4, d.height + 4}, s(9), Fade(accent, 0.12f * hover));
    roundedRect(d, s(8), mixColor(base, WHITE, 0.08f * hover + 0.25f * press));
    roundedOutline(d, s(8), active ? 2.0f : 1.2f,
                   active ? accent : mixColor(Fade(theme::kPanelBorder, 0.9f), accent, hover));

    const float size = fontSize > 0.0f ? fontSize : s(14.0f);
    const Color tc = active ? accent : mixColor(theme::kText, WHITE, hover);
    textCentered(label, d, size, tc, active || style != ButtonStyle::Normal);

    hoverTooltip(hid, hovered, tooltip);
    return clicked;
}

bool Ui::chip(const char* id, Rectangle r, const char* label, bool on, Color accent, const char* tooltip) {
    const unsigned hid = idOf(id);
    const bool hovered = hovering(r);
    const bool clicked = takeClick(hovered);
    if (hovered) hot_ = true;
    const float hover = animate(hid, hovered ? 1.0f : 0.0f, 14.0f);
    const float onT = animate(hid ^ 0x51ed270bu, on ? 1.0f : 0.0f, 12.0f);

    const float radius = r.height * 0.5f;
    roundedRect(r, radius, mixColor(Color{24, 32, 50, 240}, Fade(accent, 0.35f), onT));
    roundedOutline(r, radius, 1.3f, mixColor(mixColor(theme::kPanelBorder, accent, onT), WHITE, hover * 0.4f));
    // indicator dot
    const float dotR = r.height * 0.17f;
    const Vector2 dot{r.x + radius, r.y + r.height * 0.5f};
    DrawCircleV(dot, dotR + 2.0f * onT, Fade(accent, 0.25f * onT));
    DrawCircleV(dot, dotR, mixColor(theme::kDim, accent, onT));
    const float size = std::min(s(13.5f), r.height * 0.5f);
    const float tw = measure(label, size, on);
    const float avail = r.width - radius * 1.6f;
    text(label, r.x + radius * 1.6f + std::max(0.0f, (avail - tw) * 0.5f) - s(2), r.y + (r.height - size) * 0.5f, size,
         mixColor(theme::kDim, theme::kText, std::max(onT, hover)), on);

    hoverTooltip(hid, hovered, tooltip);
    return clicked;
}

bool Ui::slider(const char* id, Rectangle r, double& value, double lo, double hi, bool logScale, Color accent,
                const char* tooltip) {
    const unsigned hid = idOf(id);
    const bool hovered = hovering(r);
    if (hovered) hot_ = true;
    if (takeClick(hovered)) activeId_ = hid;
    const bool dragging = activeId_ == hid;

    auto toT = [&](double v) {
        v = std::clamp(v, lo, hi);
        if (logScale) return static_cast<float>(std::log(v / lo) / std::log(hi / lo));
        return static_cast<float>((v - lo) / (hi - lo));
    };

    bool changed = false;
    if (dragging) {
        const float t = std::clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f);
        const double nv = logScale ? lo * std::pow(hi / lo, static_cast<double>(t)) : lo + (hi - lo) * t;
        if (nv != value) {
            value = nv;
            changed = true;
        }
    }

    const float hover = animate(hid, (hovered || dragging) ? 1.0f : 0.0f, 14.0f);
    const float t = toT(value);
    const float cy = r.y + r.height * 0.5f;
    const float trackH = s(6.0f);
    Rectangle track{r.x, cy - trackH * 0.5f, r.width, trackH};
    roundedRect(track, trackH * 0.5f, Color{30, 40, 62, 255});
    Rectangle fill{r.x, track.y, std::max(trackH, r.width * t), trackH};
    roundedRect(fill, trackH * 0.5f, mixColor(Fade(accent, 0.7f), accent, hover));
    const Vector2 knob{r.x + r.width * t, cy};
    const float kr = s(7.5f) + hover * s(2.0f);
    DrawCircleV(knob, kr + s(4.0f) * hover, Fade(accent, 0.2f * hover));
    DrawCircleV(knob, kr, theme::kText);
    DrawCircleV(knob, kr * 0.5f, accent);

    hoverTooltip(hid, hovered && !dragging, tooltip);
    return changed;
}

bool Ui::swatch(const char* id, Rectangle r, Color c, bool selected) {
    const unsigned hid = idOf(id);
    const bool hovered = hovering(r);
    const bool clicked = takeClick(hovered);
    if (hovered) hot_ = true;
    const float hover = animate(hid, hovered ? 1.0f : 0.0f, 16.0f);
    const float sel = animate(hid ^ 0x2545f491u, selected ? 1.0f : 0.0f, 12.0f);
    const float grow = s(3.0f) * std::max(hover, sel);
    Rectangle d{r.x - grow * 0.5f, r.y - grow * 0.5f, r.width + grow, r.height + grow};
    if (sel > 0.01f) roundedRect(Rectangle{d.x - 3, d.y - 3, d.width + 6, d.height + 6}, s(8), Fade(c, 0.35f * sel));
    roundedRect(d, s(6), c);
    roundedOutline(d, s(6), selected ? 2.0f : 1.0f, selected ? WHITE : Fade(BLACK, 0.4f));
    return clicked;
}
