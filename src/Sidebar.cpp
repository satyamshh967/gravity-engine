// Right-hand sidebar: Inspector, Spawner and World tabs. Scrollable, slides
// in/out, and reports structural requests through UiActions.

#include "Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace {

const Color kSwatches[] = {
    Color{70, 160, 255, 255}, Color{255, 215, 30, 255}, Color{240, 80, 60, 255},  Color{60, 230, 120, 255},
    Color{235, 175, 110, 255}, Color{220, 60, 240, 255}, Color{120, 245, 255, 255}, Color{240, 240, 245, 255},
};

std::string pct(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.0f%%", v * 100.0);
    return b;
}

Rectangle cell(float x, float y, float w, float h, int i, int n, float gap) {
    const float cw = (w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
    return Rectangle{x + (cw + gap) * static_cast<float>(i), y, cw, h};
}

} // namespace

// ---------------------------------------------------------------------------

void Renderer::drawSidebar(GravityEngine& e, Body* selected, LaunchSettings& launch, int hoveredId, UiActions& a) {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    const float width = sidebarWidth();
    const float visible = sidebarVisibleWidth();

    if (visible < 1.0f) {
        Rectangle pill{W - ui.s(132), ui.s(14), ui.s(118), ui.s(38)};
        if (ui.button("sb_show", pill, "<< PANEL", ButtonStyle::Accent, false, "Show sidebar (H)")) sidebarVisible = true;
        ui.block(pill);
        return;
    }

    const float x0 = W - visible;
    const Rectangle bg{x0, 0.0f, width, H};
    DrawRectangleRec(bg, Color{9, 13, 23, 246});
    DrawRectangleGradientH(static_cast<int>(x0) - 18, 0, 18, static_cast<int>(H), Fade(BLACK, 0.0f), Fade(BLACK, 0.35f));
    DrawLineEx(Vector2{x0, 0.0f}, Vector2{x0, H}, 2.0f, Fade(theme::kGold, 0.6f));
    ui.block(bg);

    // --- Header: tabs + hide ---
    const float pad = ui.s(16);
    const float cx = x0 + pad;
    const float cw = width - pad * 2;
    float y = ui.s(14);
    const float tabH = ui.s(38);
    const Rectangle hideR{cx + cw - ui.s(44), y, ui.s(44), tabH};
    if (ui.button("sb_hide", hideR, ">>", ButtonStyle::Normal, false, "Hide sidebar (H)")) sidebarVisible = false;

    const char* tabNames[] = {"INSPECT", "SPAWN", "WORLD"};
    const char* tabTips[] = {"Selected body details (Tab)", "Launch settings for new bodies", "Scenes, physics constants, display"};
    const SidebarTab tabs[] = {SidebarTab::Inspector, SidebarTab::Spawner, SidebarTab::World};
    for (int i = 0; i < 3; ++i) {
        const Rectangle r = cell(cx, y, cw - ui.s(52), tabH, i, 3, ui.s(6));
        char id[16];
        std::snprintf(id, sizeof(id), "sb_tab%d", i);
        if (ui.button(id, r, tabNames[i], ButtonStyle::Normal, activeTab == tabs[i], tabTips[i])) {
            if (activeTab != tabs[i]) scrollTarget_ = 0.0f;
            activeTab = tabs[i];
        }
    }
    y += tabH + ui.s(12);
    DrawLineEx(Vector2{cx, y}, Vector2{cx + cw, y}, 1.0f, Fade(theme::kPanelBorder, 0.7f));
    y += ui.s(2);

    // --- Scrollable content ---
    const Rectangle area{x0, y, width, H - y};
    if (CheckCollisionPointRec(GetMousePosition(), area) && ui.inputEnabled) {
        scrollTarget_ -= GetMouseWheelMove() * ui.s(70);
    }
    const float maxScroll = std::max(0.0f, contentHeight_ - area.height);
    scrollTarget_ = std::clamp(scrollTarget_, 0.0f, maxScroll);
    scroll_ += (scrollTarget_ - scroll_) * std::min(1.0f, ui.dt() * 14.0f);

    ui.pushClip(area);
    const float startY = area.y + ui.s(14) - scroll_;
    float endY = startY;
    switch (activeTab) {
        case SidebarTab::Inspector: endY = drawInspectorTab(e, selected, hoveredId, cx, startY, cw, a); break;
        case SidebarTab::Spawner: endY = drawSpawnerTab(e, launch, cx, startY, cw); break;
        case SidebarTab::World: endY = drawWorldTab(e, cx, startY, cw, a); break;
    }
    ui.popClip();
    contentHeight_ = endY - startY + ui.s(30);

    // Scrollbar + fade hint
    if (contentHeight_ > area.height) {
        const float trackH = area.height - ui.s(16);
        const float thumbH = std::max(ui.s(30), trackH * area.height / contentHeight_);
        const float t = maxScroll > 0.0f ? scroll_ / maxScroll : 0.0f;
        const Rectangle thumb{x0 + width - ui.s(7), area.y + ui.s(8) + (trackH - thumbH) * t, ui.s(4), thumbH};
        ui.roundedRect(thumb, ui.s(2), Fade(theme::kGold, 0.45f));
        if (scroll_ < maxScroll - 2.0f) {
            DrawRectangleGradientV(static_cast<int>(x0), static_cast<int>(H - ui.s(40)), static_cast<int>(width),
                                   static_cast<int>(ui.s(40)), Fade(Color{9, 13, 23, 255}, 0.0f), Color{9, 13, 23, 255});
        }
    }
}

// ---------------------------------------------------------------------------
// Shared: scene picker
// ---------------------------------------------------------------------------

float Renderer::drawSceneButtons(const GravityEngine& e, float x, float y, float w, UiActions& a) {
    struct SceneDef {
        SceneId id;
        const char* key;
        const char* title;
        const char* tip;
    };
    static const SceneDef scenes[] = {
        {SceneId::SolarSystem, "1", "Solar System", "Sun, 6 planets, 4 moons and a comet.\nEvery moon stays bound."},
        {SceneId::FigureEight, "2", "Figure-8", "Three equal stars chasing each other\non one figure-8 path."},
        {SceneId::BinaryDisk, "3", "Binary + Disk", "Two stars with a 220-particle\ncircumbinary dust disk."},
        {SceneId::EarthRoom, "4", "Earth Lab", "Uniform g = 9.81 m/s^2.\nDrop things - high drops shatter!"},
    };
    const float bh = ui.s(52);
    for (int i = 0; i < 4; ++i) {
        const Rectangle r = cell(x, y + static_cast<float>(i / 2) * (bh + ui.s(8)), w, bh, i % 2, 2, ui.s(8));
        const bool current = e.currentScene() == scenes[i].id;
        char id[16];
        std::snprintf(id, sizeof(id), "scene%d", i);
        if (ui.button(id, r, "", current ? ButtonStyle::Accent : ButtonStyle::Normal, current, scenes[i].tip)) {
            a.sceneRequested = true;
            a.scene = scenes[i].id;
        }
        Rectangle key{r.x + ui.s(10), r.y + (bh - ui.s(24)) * 0.5f, ui.s(24), ui.s(24)};
        ui.roundedRect(key, ui.s(5), Fade(current ? theme::kGold : theme::kSky, 0.2f));
        ui.textCentered(scenes[i].key, key, ui.s(14), current ? theme::kGold : theme::kSky, true);
        ui.text(scenes[i].title, r.x + ui.s(44), r.y + (bh - ui.s(15)) * 0.5f, ui.s(15), current ? theme::kGold : theme::kText, true);
    }
    return y + 2 * bh + ui.s(8);
}

// ---------------------------------------------------------------------------
// Inspector tab
// ---------------------------------------------------------------------------

float Renderer::drawInspectorTab(GravityEngine& e, Body* sel, int hoveredId, float x, float y, float w, UiActions& a) {
    const float gap = ui.s(14);
    const float fs = ui.s(14);
    const float inner = w - ui.s(28);
    const float ix = x + ui.s(14);
    const Units u = unitsFor(e);

    if (!sel) {
        Rectangle c1{x, y, w, ui.s(196)};
        float cy = ui.card(c1, "NOTHING SELECTED", theme::kSky);
        const char* tips[] = {
            "Click any body to inspect and edit it.",
            "Drag in empty space to slingshot a new body.",
            "Hover a body to peek at its stats.",
            "Everything in the HUD and time bar is clickable.",
            "Press F1 for the full list of controls.",
        };
        for (const char* t : tips) {
            DrawCircleV(Vector2{ix + ui.s(4), cy + ui.s(8)}, ui.s(2.5f), theme::kGold);
            ui.text(t, ix + ui.s(14), cy, fs, theme::kText);
            cy += ui.s(26);
        }
        if (hoveredId != -1) {
            if (const Body* h = e.getBodyById(hoveredId)) {
                ui.text(("Hovering: " + h->name).c_str(), ix, cy + ui.s(2), fs, theme::kGold, true);
            }
        }
        y += c1.height + gap;

        Rectangle c2{x, y, w, ui.s(180)};
        const float sy = ui.card(c2, "SCENES", theme::kGold);
        drawSceneButtons(e, ix, sy, inner, a);
        y += c2.height + gap;

        if (ui.button("insp_open_spawn", Rectangle{x, y, w, ui.s(42)}, "OPEN SPAWNER  >", ButtonStyle::Accent)) {
            activeTab = SidebarTab::Spawner;
        }
        return y + ui.s(42);
    }

    Body& b = *sel;

    // --- Identity ---
    {
        Rectangle c{x, y, w, ui.s(132)};
        ui.roundedRect(c, ui.s(10), theme::kCard);
        ui.roundedOutline(c, ui.s(10), 1.2f, Fade(toColor(b.color), 0.6f));
        const Vector2 thumb{x + ui.s(42), y + ui.s(42)};
        const float pulse = 1.0f + 0.08f * std::sin(ui.time() * 3.0f);
        DrawCircleV(thumb, ui.s(26) * pulse, Fade(toColor(b.color), 0.22f));
        DrawCircleV(thumb, ui.s(18), toColor(b.color));
        DrawCircleV(Vector2{thumb.x - ui.s(5), thumb.y - ui.s(5)}, ui.s(6), Fade(WHITE, 0.45f));

        ui.text(b.name.c_str(), x + ui.s(80), y + ui.s(16), ui.s(19), theme::kText, true);
        const auto [motion, mc] = describeMotion(e, b);
        ui.text(motion.c_str(), x + ui.s(80), y + ui.s(42), ui.s(13.5f), mc, true);

        const float by = y + ui.s(84);
        const bool locked = followedBodyId == b.id;
        if (ui.button("insp_desel", cell(ix, by, inner, ui.s(34), 0, 3, ui.s(8)), "DESELECT", ButtonStyle::Normal, false, "Esc")) {
            a.deselect = true;
        }
        if (ui.button("insp_lock", cell(ix, by, inner, ui.s(34), 1, 3, ui.s(8)), locked ? "UNLOCK CAM" : "LOCK CAM",
                      ButtonStyle::Accent, locked, "Camera follows this body")) {
            followedBodyId = locked ? -1 : b.id;
            view.followHeaviest = false;
        }
        if (ui.button("insp_static", cell(ix, by, inner, ui.s(34), 2, 3, ui.s(8)), b.isStatic ? "UNANCHOR" : "ANCHOR",
                      ButtonStyle::Normal, b.isStatic, "Pin the body in place (static)")) {
            b.setStatic(!b.isStatic);
            a.bodyEdited = true;
        }
        y += c.height + gap;
    }

    // --- Mass & size ---
    {
        Rectangle c{x, y, w, ui.s(250)};
        float cy = ui.card(c, "MASS & SIZE", theme::kGold);
        ui.text("Mass", ix, cy, fs, theme::kDim);
        ui.textRight(u.mass(b.mass).c_str(), ix + inner, cy, ui.s(15), theme::kGold, true);
        cy += ui.s(22);
        double m = b.mass;
        if (ui.slider("insp_mass", Rectangle{ix, cy, inner, ui.s(22)}, m, limits::kMinMass, limits::kMaxMass, true, theme::kGold,
                      "Drag to change mass (log scale).\nDensity is kept, so the radius follows.")) {
            b.setMass(m);
            a.bodyEdited = true;
        }
        cy += ui.s(28);
        const char* q[] = {"/10", "/2", "x2", "x10"};
        const double f[] = {0.1, 0.5, 2.0, 10.0};
        for (int i = 0; i < 4; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "insp_mq%d", i);
            if (ui.button(id, cell(ix, cy, inner, ui.s(28), i, 4, ui.s(6)), q[i], ButtonStyle::Normal, false, nullptr, ui.s(13))) {
                b.setMass(b.mass * f[i]);
                a.bodyEdited = true;
            }
        }
        cy += ui.s(40);

        ui.text("Radius", ix, cy, fs, theme::kDim);
        ui.textRight(u.dist(b.radius).c_str(), ix + inner, cy, ui.s(15), theme::kText, true);
        cy += ui.s(22);
        double r = b.radius;
        if (ui.slider("insp_radius", Rectangle{ix, cy, inner, ui.s(22)}, r, limits::kMinRadius, limits::kMaxRadius, true,
                      theme::kSky, "Drag to change radius")) {
            b.setRadius(r);
            a.bodyEdited = true;
        }
        cy += ui.s(30);
        ui.text("Density", ix, cy, fs, theme::kDim);
        ui.textRight(fmtNum(b.density()).c_str(), ix + inner, cy, ui.s(15), theme::kGood, true);
        cy += ui.s(22);
        double d = b.density();
        if (ui.slider("insp_density", Rectangle{ix, cy, inner, ui.s(22)}, d, 1e-3, 1e3, true, theme::kGood,
                      "Drag to change density (mass kept, radius follows).\nVery dense + heavy = singularity look.")) {
            b.setDensity(d);
            a.bodyEdited = true;
        }
        y += c.height + gap;
    }

    // --- Live readouts ---
    {
        const OrbitInfo o = e.orbitInfo(b.id);
        const bool showOrbit = o.valid && o.bound;
        Rectangle c{x, y, w, ui.s(showOrbit ? 238.0f : 190.0f)};
        float cy = ui.card(c, "LIVE READOUTS", theme::kSky);
        auto row = [&](const char* label, const std::string& value, Color vc) {
            ui.text(label, ix, cy, fs, theme::kDim);
            ui.textRight(value.c_str(), ix + inner, cy, fs, vc, true);
            cy += ui.s(24);
        };
        row("Speed", u.speed(b.vel.length()), theme::kGood);
        row("Net acceleration", u.accel(b.acc.length()), ORANGE);
        row("Surface gravity", u.accel(e.surfaceGravity(b)), theme::kGold);
        row("Escape velocity", u.speed(e.escapeVelocity(b)), theme::kGold);
        row("Position", u.vec(b.pos, false), theme::kText);
        row("Velocity", u.vec(b.vel, true), theme::kText);
        if (showOrbit) {
            row("Orbital period", fmtNum(o.period) + " s", theme::kViolet);
            row("Eccentricity", fmtNum(o.eccentricity), theme::kViolet);
        }
        y += c.height + gap;
    }

    // --- Motion ---
    {
        Rectangle c{x, y, w, ui.s(132)};
        float cy = ui.card(c, "MOTION", theme::kGood);
        const float bh = ui.s(34);
        if (ui.button("insp_halt", cell(ix, cy, inner, bh, 0, 3, ui.s(6)), "HALT", ButtonStyle::Normal, false, "Set velocity to zero")) {
            b.vel = Vec2{};
            a.bodyEdited = true;
        }
        if (ui.button("insp_rev", cell(ix, cy, inner, bh, 1, 3, ui.s(6)), "REVERSE", ButtonStyle::Normal, false, "Flip the velocity")) {
            b.vel = -b.vel;
            a.bodyEdited = true;
        }
        if (ui.button("insp_boost", cell(ix, cy, inner, bh, 2, 3, ui.s(6)), "BOOST +20%", ButtonStyle::Good, false,
                      "Speed up by 20% along the current direction")) {
            b.vel *= 1.2;
            a.bodyEdited = true;
        }
        cy += bh + ui.s(8);
        if (!e.roomBounds.enabled &&
            ui.button("insp_circ", cell(ix, cy, inner, bh, 0, 2, ui.s(6)), "CIRCULARIZE", ButtonStyle::Accent, false,
                      "Put the body on a circular orbit around\nthe body whose Hill sphere it is in")) {
            a.circularizeSelected = true;
        }
        if (ui.button("insp_trail", cell(ix, cy, inner, bh, e.roomBounds.enabled ? 0 : 1, e.roomBounds.enabled ? 1 : 2, ui.s(6)),
                      "CLEAR TRAIL", ButtonStyle::Normal)) {
            b.clearTrail();
        }
        y += c.height + gap;
    }

    // --- Appearance & danger ---
    {
        Rectangle c{x, y, w, ui.s(142)};
        float cy = ui.card(c, "COLOR & DESTRUCTION", theme::kDanger);
        for (int i = 0; i < 8; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "insp_sw%d", i);
            if (ui.swatch(id, cell(ix, cy, inner, ui.s(26), i, 8, ui.s(6)), kSwatches[i], b.color.sameRgb(toRgba(kSwatches[i])))) {
                b.color = toRgba(kSwatches[i]);
            }
        }
        cy += ui.s(38);
        if (ui.button("insp_boom", cell(ix, cy, inner, ui.s(38), 0, 2, ui.s(8)), "EXPLODE  (K)", ButtonStyle::Danger, false,
                      "Shatter into fragments.\nMass and momentum are conserved.")) {
            a.explodeSelected = true;
        }
        if (ui.button("insp_del", cell(ix, cy, inner, ui.s(38), 1, 2, ui.s(8)), "DELETE  (Del)", ButtonStyle::Danger, false,
                      "Remove the body")) {
            a.deleteSelected = true;
        }
        y += c.height;
    }
    return y;
}

// ---------------------------------------------------------------------------
// Spawner tab
// ---------------------------------------------------------------------------

float Renderer::drawSpawnerTab(const GravityEngine& e, LaunchSettings& L, float x, float y, float w) {
    const float gap = ui.s(14);
    const float fs = ui.s(14);
    const float inner = w - ui.s(28);
    const float ix = x + ui.s(14);
    const bool room = e.roomBounds.enabled;
    const Units u = unitsFor(e);

    // --- Payload with live preview ---
    {
        Rectangle c{x, y, w, ui.s(232)};
        float cy = ui.card(c, room ? "LAB PAYLOAD" : "PAYLOAD", theme::kGold);
        Rectangle prev{ix, cy, inner, ui.s(64)};
        ui.roundedRect(prev, ui.s(8), Color{8, 11, 20, 255});
        const float pr = std::clamp(static_cast<float>(std::sqrt(L.radius)) * ui.s(6.5f), ui.s(5), ui.s(27));
        const Vector2 pc{prev.x + ui.s(42), prev.y + prev.height * 0.5f};
        const float bob = std::sin(ui.time() * 2.5f) * ui.s(2);
        DrawCircleV(Vector2{pc.x, pc.y + bob}, pr * 1.6f, Fade(toColor(L.color), 0.2f));
        DrawCircleV(Vector2{pc.x, pc.y + bob}, pr, toColor(L.color));
        DrawCircleV(Vector2{pc.x - pr * 0.3f, pc.y + bob - pr * 0.3f}, pr * 0.35f, Fade(WHITE, 0.45f));
        ui.text(L.presetType == LaunchPresetType::Custom ? (L.typeName + " (custom)").c_str() : L.typeName.c_str(),
                prev.x + ui.s(84), prev.y + ui.s(12), ui.s(16), theme::kText, true);
        ui.text((u.mass(L.mass) + "   r " + u.dist(L.radius)).c_str(), prev.x + ui.s(84), prev.y + ui.s(36), ui.s(13),
                theme::kDim);
        cy += prev.height + ui.s(12);

        static const LaunchPresetType spacePresets[] = {LaunchPresetType::Asteroid, LaunchPresetType::Moon, LaunchPresetType::Terrestrial,
                                                        LaunchPresetType::GasGiant, LaunchPresetType::Star, LaunchPresetType::BlackHole};
        static const LaunchPresetType labPresets[] = {LaunchPresetType::HeavyAnvil, LaunchPresetType::LightApple, LaunchPresetType::RubberBall,
                                                      LaunchPresetType::Asteroid, LaunchPresetType::Terrestrial, LaunchPresetType::Star};
        const LaunchPresetType* list = room ? labPresets : spacePresets;
        const float bh = ui.s(40);
        for (int i = 0; i < 6; ++i) {
            const LaunchPreset* p = findLaunchPreset(list[i]);
            if (!p) continue;
            const Rectangle r = cell(ix, cy + static_cast<float>(i / 3) * (bh + ui.s(8)), inner, bh, i % 3, 3, ui.s(8));
            char id[16];
            std::snprintf(id, sizeof(id), "sp_pre%d", i);
            const bool on = L.presetType == p->type;
            if (ui.button(id, r, "", ButtonStyle::Normal, on)) L.applyPreset(p->type);
            DrawCircleV(Vector2{r.x + ui.s(15), r.y + bh * 0.5f}, ui.s(6), toColor(p->color));
            ui.text(p->name, r.x + ui.s(27), r.y + (bh - ui.s(13)) * 0.5f, ui.s(13), on ? theme::kGold : theme::kText, on);
        }
        y += c.height + gap;
    }

    // --- Spawn mode ---
    {
        Rectangle c{x, y, w, ui.s(96)};
        const float cy = ui.card(c, "HOW TO SPAWN", theme::kSky);
        struct ModeDef {
            LaunchSpawnMode mode;
            const char* label;
            const char* tip;
        };
        const ModeDef spaceModes[] = {
            {LaunchSpawnMode::Slingshot, "Slingshot", "Drag back and release.\nThe preview shows the predicted path."},
            {LaunchSpawnMode::CircularOrbit, "Auto orbit", "Click: placed on a circular orbit around\nthe body whose Hill sphere you clicked in."},
            {LaunchSpawnMode::Stationary, "Stationary", "Click: placed at rest."},
        };
        const ModeDef labModes[] = {
            {LaunchSpawnMode::Slingshot, "Throw", "Drag back and release to throw."},
            {LaunchSpawnMode::Stationary, "Drop", "Click: released at rest and falls."},
        };
        const ModeDef* modes = room ? labModes : spaceModes;
        const int n = room ? 2 : 3;
        if (room && L.spawnMode == LaunchSpawnMode::CircularOrbit) L.spawnMode = LaunchSpawnMode::Stationary;
        for (int i = 0; i < n; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "sp_mode%d", i);
            if (ui.chip(id, cell(ix, cy, inner, ui.s(34), i, n, ui.s(8)), modes[i].label, L.spawnMode == modes[i].mode,
                        theme::kSky, modes[i].tip)) {
                L.spawnMode = modes[i].mode;
            }
        }
        y += c.height + gap;
    }

    // --- Properties ---
    {
        Rectangle c{x, y, w, ui.s(240)};
        float cy = ui.card(c, "PROPERTIES", theme::kGood);
        auto labelled = [&](const char* label, const std::string& value, Color col) {
            ui.text(label, ix, cy, fs, theme::kDim);
            ui.textRight(value.c_str(), ix + inner, cy, ui.s(15), col, true);
            cy += ui.s(22);
        };
        labelled("Mass", u.mass(L.mass), theme::kGold);
        double m = L.mass;
        if (ui.slider("sp_mass", Rectangle{ix, cy, inner, ui.s(22)}, m, limits::kMinMass, limits::kMaxMass, true, theme::kGold,
                      "Launch mass (also + / - keys,\nor mouse wheel while dragging)")) {
            L.setMass(m);
        }
        cy += ui.s(30);
        labelled("Radius", u.dist(L.radius), theme::kText);
        double r = L.radius;
        if (ui.slider("sp_radius", Rectangle{ix, cy, inner, ui.s(22)}, r, limits::kMinRadius, limits::kMaxRadius, true, theme::kSky)) {
            L.setRadius(r);
        }
        cy += ui.s(30);
        labelled("Density", fmtNum(L.density), theme::kGood);
        double d = L.density;
        if (ui.slider("sp_density", Rectangle{ix, cy, inner, ui.s(22)}, d, 1e-3, 1e3, true, theme::kGood)) {
            L.setDensity(d);
        }
        cy += ui.s(32);
        if (ui.chip("sp_static", Rectangle{ix, cy, inner, ui.s(32)}, L.isStatic ? "Anchored spawn (static)" : "Free-moving spawn",
                    L.isStatic, theme::kGold, "Anchored bodies never move but still attract")) {
            L.isStatic = !L.isStatic;
        }
        y += c.height + gap;
    }

    // --- Color & preview length ---
    {
        Rectangle c{x, y, w, ui.s(146)};
        float cy = ui.card(c, "COLOR & PREVIEW", theme::kViolet);
        for (int i = 0; i < 8; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "sp_sw%d", i);
            if (ui.swatch(id, cell(ix, cy, inner, ui.s(26), i, 8, ui.s(6)), kSwatches[i], L.color.sameRgb(toRgba(kSwatches[i])))) {
                L.color = toRgba(kSwatches[i]);
            }
        }
        cy += ui.s(38);
        const int steps[] = {150, 300, 600};
        const char* names[] = {"1.5 s path", "3 s path", "6 s path"};
        for (int i = 0; i < 3; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "sp_pred%d", i);
            if (ui.chip(id, cell(ix, cy, inner, ui.s(32), i, 3, ui.s(8)), names[i], L.predictionSteps == steps[i], theme::kViolet,
                        "How far ahead the slingshot preview looks")) {
                L.predictionSteps = steps[i];
            }
        }
        y += c.height;
    }
    return y;
}

// ---------------------------------------------------------------------------
// World tab
// ---------------------------------------------------------------------------

float Renderer::drawWorldTab(GravityEngine& e, float x, float y, float w, UiActions& a) {
    const float gap = ui.s(14);
    const float fs = ui.s(14);
    const float inner = w - ui.s(28);
    const float ix = x + ui.s(14);
    const bool room = e.roomBounds.enabled;

    {
        Rectangle c{x, y, w, ui.s(180)};
        const float cy = ui.card(c, "SCENES", theme::kGold);
        drawSceneButtons(e, ix, cy, inner, a);
        y += c.height + gap;
    }

    // --- Simulation modes ---
    {
        Rectangle c{x, y, w, ui.s(164)};
        float cy = ui.card(c, "SIMULATION", theme::kSky);
        ui.text("Integrator", ix, cy, fs, theme::kDim);
        cy += ui.s(22);
        if (ui.chip("w_verlet", cell(ix, cy, inner, ui.s(32), 0, 2, ui.s(8)), "Velocity Verlet",
                    e.integrator == IntegratorType::VelocityVerlet, theme::kGold, "2nd order, time-reversible, energy-stable")) {
            a.setIntegrator = true;
            a.integrator = IntegratorType::VelocityVerlet;
        }
        if (ui.chip("w_euler", cell(ix, cy, inner, ui.s(32), 1, 2, ui.s(8)), "Symplectic Euler",
                    e.integrator == IntegratorType::SymplecticEuler, theme::kWarn, "1st order - compare the energy drift!")) {
            a.setIntegrator = true;
            a.integrator = IntegratorType::SymplecticEuler;
        }
        cy += ui.s(44);
        ui.text("Collisions", ix, cy, fs, theme::kDim);
        cy += ui.s(22);
        struct M {
            CollisionMode m;
            const char* label;
            Color c;
            const char* tip;
        };
        const M modes[] = {
            {CollisionMode::InelasticMerge, "Merge", theme::kViolet, "Bodies stick together (momentum conserved)"},
            {CollisionMode::DestructiveShatter, "Shatter", theme::kDanger, "Violent impacts break bodies into fragments;\ngentle ones merge"},
            {CollisionMode::Elastic, "Bounce", theme::kGood, "Perfectly elastic bounces"},
            {CollisionMode::None, "Ghost", theme::kDim, "Bodies pass through each other"},
        };
        for (int i = 0; i < 4; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "w_col%d", i);
            if (ui.chip(id, cell(ix, cy, inner, ui.s(32), i, 4, ui.s(6)), modes[i].label, e.collisionMode == modes[i].m, modes[i].c,
                        modes[i].tip)) {
                a.setCollisionMode = true;
                a.collisionMode = modes[i].m;
            }
        }
        y += c.height + gap;
    }

    // --- Physics constants ---
    {
        const int rows = room ? 4 : 4;
        Rectangle c{x, y, w, ui.s(46) + rows * ui.s(52)};
        float cy = ui.card(c, room ? "LAB PHYSICS" : "PHYSICS CONSTANTS", theme::kGood);
        auto sliderRow = [&](const char* id, const char* label, const std::string& value, double& v, double lo, double hi,
                             bool logScale, Color col, const char* tip) {
            ui.text(label, ix, cy, fs, theme::kDim);
            ui.textRight(value.c_str(), ix + inner, cy, fs, col, true);
            cy += ui.s(22);
            const bool changed = ui.slider(id, Rectangle{ix, cy, inner, ui.s(22)}, v, lo, hi, logScale, col, tip);
            cy += ui.s(30);
            return changed;
        };

        if (room) {
            RoomBounds& rb = e.roomBounds;
            double g = e.uniformGravity.y / rb.pixelsPerMeter;
            if (sliderRow("w_g", "Gravity g", fmtNum(g) + " m/s^2", g, 1.0, 30.0, false, theme::kGold,
                          "Earth 9.81  |  Moon 1.62  |  Mars 3.71  |  Jupiter 24.8")) {
                e.uniformGravity.y = g * rb.pixelsPerMeter;
                a.physicsEdited = true;
            }
            double fr = rb.floorRestitution;
            if (sliderRow("w_floor", "Floor bounciness", pct(fr), fr, 0.0, 1.0, false, theme::kSky, "Fraction of speed kept on each bounce")) {
                rb.floorRestitution = fr;
                rb.wallRestitution = rb.ceilingRestitution = std::min(1.0, fr + 0.03);
            }
            double sv = rb.shatterVelocity / rb.pixelsPerMeter;
            if (sliderRow("w_shatter", "Shatter speed", fmtNum(sv) + " m/s", sv, 2.0, 30.0, false, theme::kDanger,
                          "Impacts faster than this break bodies apart")) {
                rb.shatterVelocity = sv * rb.pixelsPerMeter;
            }
            double rest = e.restitution;
            if (sliderRow("w_rest", "Body-body bounce", pct(rest), rest, 0.0, 1.0, false, theme::kGood,
                          "Restitution between colliding bodies")) {
                e.restitution = rest;
            }
        } else {
            double G = e.G;
            if (sliderRow("w_G", "Gravitational constant G", fmtNum(G), G, 0.1, 10.0, true, theme::kGold,
                          "Scales every gravitational force")) {
                e.G = G;
                a.physicsEdited = true;
            }
            double eps = e.epsilon;
            if (sliderRow("w_eps", "Softening length", fmtNum(eps) + " px", eps, 0.0, 20.0, false, theme::kSky,
                          "Smooths gravity at very short range\nso close encounters stay stable")) {
                e.epsilon = eps;
                a.physicsEdited = true;
            }
            double rest = e.restitution;
            if (sliderRow("w_rest", "Bounce restitution", pct(rest), rest, 0.0, 1.0, false, theme::kGood,
                          "Used in Bounce collision mode")) {
                e.restitution = rest;
            }
            double sf = e.shatterEnergyFactor;
            if (sliderRow("w_sf", "Shatter threshold", fmtNum(sf) + "x binding", sf, 0.1, 20.0, true, theme::kDanger,
                          "Impact energy needed to shatter, relative to\nthe gravitational binding energy (Shatter mode)")) {
                e.shatterEnergyFactor = sf;
            }
        }
        y += c.height + gap;
    }

    // --- Display ---
    {
        Rectangle c{x, y, w, ui.s(176)};
        float cy = ui.card(c, "DISPLAY", theme::kViolet);
        struct T {
            const char* id;
            const char* label;
            bool* flag;
        };
        T toggles[] = {
            {"w_tr", "Trails", &view.trails}, {"w_ve", "Vectors", &view.vectors}, {"w_gr", "Grid", &view.grid},
            {"w_la", "Labels", &view.labels}, {"w_fo", "Follow heaviest", &view.followHeaviest},
        };
        for (int i = 0; i < 5; ++i) {
            const Rectangle r = i < 3 ? cell(ix, cy, inner, ui.s(30), i, 3, ui.s(6))
                                      : cell(ix, cy + ui.s(38), inner, ui.s(30), i - 3, 2, ui.s(6));
            if (ui.chip(toggles[i].id, r, toggles[i].label, *toggles[i].flag, theme::kViolet)) *toggles[i].flag = !*toggles[i].flag;
        }
        cy += ui.s(80);
        ui.text("UI scale", ix, cy + ui.s(6), fs, theme::kDim);
        const float scales[] = {0.9f, 1.0f, 1.25f, 1.5f};
        const char* names[] = {"90%", "100%", "125%", "150%"};
        for (int i = 0; i < 4; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "w_sc%d", i);
            if (ui.chip(id, cell(ix + ui.s(80), cy, inner - ui.s(80), ui.s(30), i, 4, ui.s(6)), names[i],
                        std::abs(ui.scale - scales[i]) < 0.01f, theme::kSky)) {
                ui.scale = scales[i];
            }
        }
        y += c.height + gap;
    }

    // --- Danger zone ---
    {
        Rectangle c{x, y, w, ui.s(96)};
        const float cy = ui.card(c, "RESET", theme::kDanger);
        if (ui.button("w_reset", cell(ix, cy, inner, ui.s(38), 0, 2, ui.s(8)), "RELOAD SCENE", ButtonStyle::Accent, false,
                      "Restart the current scene")) {
            a.sceneRequested = true;
            a.scene = e.currentScene();
        }
        const bool armed = clearConfirm_ > 0.0f;
        if (ui.button("w_clear", cell(ix, cy, inner, ui.s(38), 1, 2, ui.s(8)), armed ? "CLICK TO CONFIRM" : "CLEAR ALL",
                      ButtonStyle::Danger, armed, "Remove every body (X twice)")) {
            if (armed) {
                a.clearAll = true;
                clearConfirm_ = 0.0f;
            } else {
                clearConfirm_ = 3.0f;
            }
        }
        y += c.height;
    }
    return y;
}
