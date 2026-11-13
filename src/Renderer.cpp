#include "Renderer.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {
constexpr float kTwoPiF = 6.28318530718f;

float easeOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

std::string format(const char* f, double a) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), f, a);
    return buf;
}

std::string format2(const char* f, double a, double b) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), f, a, b);
    return buf;
}
} // namespace

// ---------------------------------------------------------------------------
// Units & formatting
// ---------------------------------------------------------------------------

std::string fmtNum(double v) {
    const double a = std::abs(v);
    if (a != 0.0 && (a >= 1e5 || a < 1e-2)) return format("%.2e", v);
    if (a >= 100.0) return format("%.0f", v);
    if (a >= 10.0) return format("%.1f", v);
    return format("%.2f", v);
}

std::string Units::mass(double m) const { return fmtNum(m) + (metric ? " kg" : " M"); }
std::string Units::dist(double px) const { return metric ? fmtNum(px / ppm) + " m" : fmtNum(px) + " px"; }
std::string Units::speed(double v) const { return metric ? fmtNum(v / ppm) + " m/s" : fmtNum(v) + " px/s"; }
std::string Units::accel(double a) const { return metric ? fmtNum(a / ppm) + " m/s^2" : fmtNum(a) + " px/s^2"; }
std::string Units::vec(Vec2 v, bool velocity) const {
    const double k = metric ? 1.0 / ppm : 1.0;
    const char* u = metric ? (velocity ? " m/s" : " m") : (velocity ? " px/s" : " px");
    return "(" + fmtNum(v.x * k) + ", " + fmtNum(v.y * k) + ")" + u;
}

Units unitsFor(const GravityEngine& e) {
    Units u;
    u.metric = e.roomBounds.enabled;
    u.ppm = e.roomBounds.pixelsPerMeter;
    return u;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Renderer::init() {
    ui.loadFonts();
    const Vector2 dpi = GetWindowScaleDPI();
    ui.scale = std::clamp(dpi.x, 1.0f, 2.0f);

    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    const Rectangle vp = viewport();
    camera.offset = Vector2{vp.x + vp.width * 0.5f, vp.y + vp.height * 0.5f};

    std::mt19937 rng(20240611u);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    stars_.clear();
    for (int i = 0; i < 260; ++i) {
        const float depth = u01(rng);
        stars_.push_back(Star{u01(rng), u01(rng), 0.6f + depth * 1.4f, u01(rng) * kTwoPiF, 0.6f + u01(rng) * 2.2f, depth});
    }
}

void Renderer::shutdown() { ui.unloadFonts(); }

void Renderer::beginFrame(float dt) {
    ui.beginFrame(dt);
    ui.inputEnabled = true;
    sidebarAnim_ += ((sidebarVisible ? 1.0f : 0.0f) - sidebarAnim_) * std::min(1.0f, dt * 10.0f);
    if (std::abs(sidebarAnim_ - (sidebarVisible ? 1.0f : 0.0f)) < 0.002f) sidebarAnim_ = sidebarVisible ? 1.0f : 0.0f;
    clearConfirm_ = std::max(0.0f, clearConfirm_ - dt);
    for (auto& t : toasts_) t.age += dt;
    std::erase_if(toasts_, [](const Toast& t) { return t.age > t.life; });
}

void Renderer::endFrame() { ui.endFrame(); }

void Renderer::pushToast(std::string text, Color color) {
    // Merge repeated messages instead of stacking duplicates.
    for (auto& t : toasts_) {
        if (t.text == text) {
            t.age = std::min(t.age, 0.3f);
            return;
        }
    }
    toasts_.push_back(Toast{std::move(text), color, 0.0f, 3.2f});
    while (toasts_.size() > 5) toasts_.erase(toasts_.begin());
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

float Renderer::sidebarWidth() const {
    const float w = static_cast<float>(GetScreenWidth());
    return std::clamp(ui.s(440.0f), 340.0f, w * 0.45f);
}

float Renderer::sidebarVisibleWidth() const { return sidebarWidth() * easeOutCubic(sidebarAnim_); }

Rectangle Renderer::viewport() const {
    const float w = static_cast<float>(GetScreenWidth());
    const float h = static_cast<float>(GetScreenHeight());
    return Rectangle{0.0f, 0.0f, std::max(100.0f, w - sidebarVisibleWidth()), h};
}

Vec2 Renderer::screenToWorld(Vector2 p) const { return toVec2(GetScreenToWorld2D(p, camera)); }
Vector2 Renderer::worldToScreen(Vec2 p) const { return GetWorldToScreen2D(toVector2(p), camera); }

Rectangle Renderer::visibleWorldRect() const {
    const Vec2 a = screenToWorld(Vector2{0.0f, 0.0f});
    const Vec2 b = screenToWorld(Vector2{static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
    return Rectangle{static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(b.x - a.x),
                     static_cast<float>(b.y - a.y)};
}

void Renderer::updateCamera(float dt, bool allowWheelZoom, bool allowDragPan, bool allowKeyPan) {
    // Keep the camera centred in the visible area (left of the sidebar). The
    // offset glides when the sidebar opens/closes or the window is resized.
    const Rectangle vp = viewport();
    const Vector2 center{vp.x + vp.width * 0.5f, vp.y + vp.height * 0.5f};
    camera.offset = Vector2Lerp(camera.offset, center, std::min(1.0f, dt * 12.0f));

    bool panned = false;
    if (allowDragPan && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        const Vector2 d = GetMouseDelta();
        if (d.x != 0.0f || d.y != 0.0f) {
            camera.target = Vector2Subtract(camera.target, Vector2Scale(d, 1.0f / camera.zoom));
            panned = true;
        }
    }

    if (allowKeyPan) {
        const float pan = 650.0f / camera.zoom * dt;
        Vector2 move{0.0f, 0.0f};
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) move.y -= pan;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) move.y += pan;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) move.x -= pan;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) move.x += pan;
        if (move.x != 0.0f || move.y != 0.0f) {
            camera.target = Vector2Add(camera.target, move);
            panned = true;
        }
    }

    const float wheel = allowWheelZoom ? GetMouseWheelMove() : 0.0f;
    if (wheel != 0.0f) {
        // Zoom towards the cursor without permanently moving the camera offset.
        const Vector2 mouse = GetMousePosition();
        const Vector2 before = GetScreenToWorld2D(mouse, camera);
        float factor = 1.0f + 0.15f * std::abs(wheel);
        if (wheel < 0.0f) factor = 1.0f / factor;
        camera.zoom = std::clamp(camera.zoom * factor, 0.02f, 30.0f);
        const Vector2 after = GetScreenToWorld2D(mouse, camera);
        camera.target = Vector2Add(camera.target, Vector2Subtract(before, after));
        camAnimating_ = false;
    }

    if (panned) {
        // Manual panning cancels scene-fit animation and camera lock; zooming keeps the lock.
        camAnimating_ = false;
        followedBodyId = -1;
        view.followHeaviest = false;
    }

    if (camAnimating_) {
        const float k = std::min(1.0f, dt * 6.0f);
        camera.target = Vector2Lerp(camera.target, goalTarget_, k);
        camera.zoom = std::exp(std::log(camera.zoom) + (std::log(goalZoom_) - std::log(camera.zoom)) * k);
        if (Vector2Distance(camera.target, goalTarget_) * camera.zoom < 0.5f && std::abs(camera.zoom / goalZoom_ - 1.0f) < 0.002f) {
            camAnimating_ = false;
        }
    }
}

void Renderer::followPoint(Vec2 p, float dt) {
    camAnimating_ = false;
    camera.target = Vector2Lerp(camera.target, toVector2(p), std::min(1.0f, dt * 14.0f));
}

void Renderer::fitScene(const SceneView& v, bool instant) {
    const float sideW = sidebarVisible ? sidebarWidth() : 0.0f;
    const float vw = std::max(200.0f, static_cast<float>(GetScreenWidth()) - sideW);
    const float vh = static_cast<float>(GetScreenHeight());
    const float zoom = std::clamp(std::min(vw / static_cast<float>(2.0 * v.halfExtent.x),
                                           vh / static_cast<float>(2.0 * v.halfExtent.y)) * 0.94f,
                                  0.02f, 30.0f);
    goalTarget_ = toVector2(v.center);
    goalZoom_ = zoom;
    followedBodyId = -1;
    if (instant) {
        camera.target = goalTarget_;
        camera.zoom = goalZoom_;
        camera.offset = Vector2{vw * 0.5f, vh * 0.5f};
        camAnimating_ = false;
    } else {
        camAnimating_ = true;
    }
}

// ---------------------------------------------------------------------------
// World-space drawing
// ---------------------------------------------------------------------------

void Renderer::ring(Vector2 c, float radius, float thickWorld, Color col) const {
    if (radius <= 0.0f || col.a == 0) return;
    const int seg = std::clamp(static_cast<int>(radius * camera.zoom * 0.6f), 24, 160);
    DrawRing(c, std::max(0.0f, radius - thickWorld * 0.5f), radius + thickWorld * 0.5f, 0.0f, 360.0f, seg, col);
}

void Renderer::drawBackground() {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    DrawRectangleGradientV(0, 0, static_cast<int>(W), static_cast<int>(H), Color{12, 14, 30, 255}, Color{4, 5, 11, 255});

    // Faint nebulae with very slow parallax
    const float nx = -camera.target.x * 0.02f;
    const float ny = -camera.target.y * 0.02f;
    DrawCircleGradient(static_cast<int>(W * 0.22f + nx), static_cast<int>(H * 0.3f + ny), W * 0.32f,
                       Color{90, 50, 160, 22}, Color{90, 50, 160, 0});
    DrawCircleGradient(static_cast<int>(W * 0.75f + nx), static_cast<int>(H * 0.75f + ny), W * 0.28f,
                       Color{30, 110, 170, 20}, Color{30, 110, 170, 0});

    // Twinkling parallax starfield (3 depths)
    const float t = ui.time();
    for (const Star& s : stars_) {
        const float par = 0.03f + 0.12f * s.depth;
        float x = std::fmod(s.x * W - camera.target.x * par, W);
        float y = std::fmod(s.y * H - camera.target.y * par, H);
        if (x < 0.0f) x += W;
        if (y < 0.0f) y += H;
        const float tw = 0.55f + 0.45f * std::sin(t * s.speed + s.phase);
        const float a = (0.25f + 0.6f * s.depth) * tw;
        DrawCircleV(Vector2{x, y}, s.size, Fade(Color{220, 230, 255, 255}, a));
    }
}

void Renderer::drawGrid() {
    if (!view.grid) return;
    const Rectangle wr = visibleWorldRect();
    const double target = 90.0 / camera.zoom;
    const double p = std::pow(10.0, std::floor(std::log10(target)));
    double step = p;
    if (target / p > 5.0) step = 5.0 * p;
    else if (target / p > 2.0) step = 2.0 * p;

    const float thick = 1.0f / camera.zoom;
    const Color minor = Fade(Color{90, 120, 170, 255}, 0.07f);
    const Color axis = Fade(Color{120, 150, 200, 255}, 0.3f);
    for (double x = std::floor(wr.x / step) * step; x <= wr.x + wr.width; x += step) {
        DrawLineEx(Vector2{static_cast<float>(x), wr.y}, Vector2{static_cast<float>(x), wr.y + wr.height}, thick,
                   std::abs(x) < step * 0.01 ? axis : minor);
    }
    for (double y = std::floor(wr.y / step) * step; y <= wr.y + wr.height; y += step) {
        DrawLineEx(Vector2{wr.x, static_cast<float>(y)}, Vector2{wr.x + wr.width, static_cast<float>(y)}, thick,
                   std::abs(y) < step * 0.01 ? axis : minor);
    }
}

void Renderer::drawRoom(const GravityEngine& e) const {
    const RoomBounds& rb = e.roomBounds;
    if (!rb.enabled) return;

    const float rx = static_cast<float>(rb.xMin);
    const float ry = static_cast<float>(rb.yMin);
    const float rw = static_cast<float>(rb.xMax - rb.xMin);
    const float rh = static_cast<float>(rb.yMax - rb.yMin);
    const float floorY = static_cast<float>(rb.yMax);
    const float ceilY = static_cast<float>(rb.yMin);
    const float xMax = static_cast<float>(rb.xMax);
    const double ppm = rb.pixelsPerMeter;
    const double g = std::abs(e.uniformGravity.y);

    DrawRectangleRec(Rectangle{rx, ry, rw, rh}, Color{12, 16, 27, 235});
    for (float x = rx + 50.0f; x < xMax; x += 50.0f) DrawLineV(Vector2{x, ry}, Vector2{x, floorY}, Color{30, 42, 65, 70});
    for (float y = ry + 50.0f; y < floorY; y += 50.0f) DrawLineV(Vector2{rx, y}, Vector2{xMax, y}, Color{30, 42, 65, 70});

    // Danger zone: drops starting above the critical height shatter on impact.
    const double hCrit = rb.criticalDropHeight(g);
    const float critY = static_cast<float>(rb.yMax - hCrit);
    if (critY > ry && critY < floorY) {
        DrawRectangleGradientV(static_cast<int>(rx), static_cast<int>(ry), static_cast<int>(rw), static_cast<int>(critY - ry),
                               Color{255, 60, 60, 26}, Color{255, 60, 60, 6});
        const float dash = 13.0f;
        const float phase = std::fmod(ui.time() * 30.0f, 22.0f);
        for (float x = rx + phase; x < xMax - dash; x += 22.0f) {
            DrawLineEx(Vector2{x, critY}, Vector2{x + dash, critY}, 2.0f, Color{255, 75, 75, 200});
        }
        const std::string label = "SHATTER ZONE - drops from above " + fmtNum(hCrit / ppm) + " m break apart (impact > " +
                                  fmtNum(rb.shatterVelocity / ppm) + " m/s)";
        ui.text(label.c_str(), rx + 22.0f, critY - 19.0f, 13.0f, Color{255, 110, 110, 235}, true);
        ui.text("SAFE ZONE - drops from below the line bounce", rx + 22.0f, critY + 6.0f, 13.0f, Color{90, 235, 140, 210}, true);
    }

    // Floor
    DrawRectangleRec(Rectangle{rx - 30.0f, floorY, rw + 60.0f, 36.0f}, Color{35, 42, 58, 255});
    DrawLineEx(Vector2{rx - 30.0f, floorY}, Vector2{xMax + 30.0f, floorY}, 3.5f, Color{0, 230, 255, 240});
    for (float hx = rx - 20.0f; hx < xMax + 20.0f; hx += 32.0f) {
        DrawLineEx(Vector2{hx, floorY + 4.0f}, Vector2{hx + 16.0f, floorY + 32.0f}, 3.5f, Color{255, 205, 30, 170});
    }
    const std::string floorLabel = "STEEL FLOOR   restitution " + format("%.0f%%", rb.floorRestitution * 100.0) +
                                   "   g = " + fmtNum(g / ppm) + " m/s^2";
    ui.text(floorLabel.c_str(), rx + 15.0f, floorY + 10.0f, 13.0f, Color{235, 245, 255, 240}, true);

    // Ceiling + lights
    DrawRectangleRec(Rectangle{rx - 30.0f, ceilY - 28.0f, rw + 60.0f, 28.0f}, Color{28, 34, 48, 255});
    DrawLineEx(Vector2{rx - 30.0f, ceilY}, Vector2{xMax + 30.0f, ceilY}, 2.5f, Color{110, 160, 215, 230});
    for (float lx = rx + 140.0f; lx < xMax - 80.0f; lx += 220.0f) {
        const float flicker = 0.85f + 0.15f * std::sin(ui.time() * 7.0f + lx);
        DrawCircleV(Vector2{lx, ceilY}, 6.0f, Fade(Color{255, 240, 200, 255}, flicker));
        DrawTriangle(Vector2{lx, ceilY}, Vector2{lx - 70.0f, ceilY + 150.0f}, Vector2{lx + 70.0f, ceilY + 150.0f},
                     Color{255, 230, 180, 14});
    }

    // Walls with a metric height ruler (1 m ticks)
    DrawRectangleRec(Rectangle{rx - 30.0f, ceilY, 30.0f, rh}, Color{26, 32, 46, 255});
    DrawLineEx(Vector2{rx, ceilY}, Vector2{rx, floorY}, 3.0f, Color{110, 150, 200, 230});
    DrawRectangleRec(Rectangle{xMax, ceilY, 30.0f, rh}, Color{26, 32, 46, 255});
    DrawLineEx(Vector2{xMax, ceilY}, Vector2{xMax, floorY}, 3.0f, Color{110, 150, 200, 230});
    int meter = 0;
    for (double my = rb.yMax; my >= rb.yMin - 1e-6; my -= ppm, ++meter) {
        const float y = static_cast<float>(my);
        DrawLineEx(Vector2{rx - 14.0f, y}, Vector2{rx, y}, 2.0f, Color{160, 190, 225, 220});
        ui.text((std::to_string(meter) + "m").c_str(), rx - 29.0f, y - 6.0f, 11.0f, Color{190, 210, 235, 220});
    }

    ui.text("EARTH GRAVITY LAB", rx + 18.0f, ry + 14.0f, 20.0f, theme::kGold, true);
    ui.text("Galileo: every mass falls with the same acceleration a = g", rx + 18.0f, ry + 40.0f, 13.0f, theme::kSky, true);
}

void Renderer::drawTrails(const GravityEngine& e) const {
    if (!view.trails) return;
    const float thick = 1.5f / camera.zoom;
    for (const auto& b : e.bodies) {
        const std::size_t n = b.trail.size();
        if (n < 2) continue;
        const float baseAlpha = b.isDebris ? 0.35f : 0.65f;
        const Color c = toColor(b.color);
        for (std::size_t i = 1; i < n; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(n);
            DrawLineEx(toVector2(b.trail[i - 1]), toVector2(b.trail[i]), thick * (0.4f + 0.6f * progress),
                       Fade(c, progress * baseAlpha));
        }
    }
}

void Renderer::drawBodies(const GravityEngine& e, int selectedId, int hoveredId) const {
    const Rectangle wr = visibleWorldRect();
    const float t = ui.time();
    const float px = 1.0f / camera.zoom; // one screen pixel in world units

    for (const auto& b : e.bodies) {
        const Vector2 pos = toVector2(b.pos);
        const float r = std::max(static_cast<float>(b.radius), 0.8f * px);
        const float margin = r * 3.0f;
        if (pos.x + margin < wr.x || pos.x - margin > wr.x + wr.width || pos.y + margin < wr.y ||
            pos.y - margin > wr.y + wr.height) {
            continue;
        }
        const Color c = toColor(b.color);
        const double density = b.density();
        const bool singularity = density >= 100.0 && b.mass >= 1000.0;
        const bool star = !singularity && b.mass >= 5000.0;

        if (singularity) {
            // Accretion ring + event horizon
            const float spin = t * 2.0f;
            for (int k = 0; k < 3; ++k) {
                const float rr = r * (1.7f + 0.35f * k + 0.08f * std::sin(spin + k));
                ring(pos, rr, r * 0.22f, Fade(c, 0.35f - 0.09f * k));
            }
            DrawCircleV(pos, r * 1.15f, Fade(c, 0.5f));
            DrawCircleV(pos, r, BLACK);
            ring(pos, r, 1.5f * px, Fade(WHITE, 0.6f));
        } else {
            if (star) {
                const float pulse = 1.0f + 0.06f * std::sin(t * 2.2f + static_cast<float>(b.id));
                DrawCircleV(pos, r * 2.6f * pulse, Fade(c, 0.06f));
                DrawCircleV(pos, r * 1.9f * pulse, Fade(c, 0.12f));
            }
            DrawCircleV(pos, r * 1.6f, Fade(c, b.isDebris ? 0.1f : 0.18f));
            DrawCircleV(pos, r * 1.25f, Fade(c, b.isDebris ? 0.25f : 0.4f));
            DrawCircleV(pos, r, c);
            if (r * camera.zoom > 3.0f) {
                DrawCircleV(Vector2{pos.x - r * 0.28f, pos.y - r * 0.28f}, r * 0.35f, Fade(WHITE, star ? 0.65f : 0.45f));
                ring(pos, r, 1.0f * px, Fade(BLACK, 0.35f));
            }
        }

        if (b.isStatic) {
            // Rotating dashed anchor ring
            const float rr = r + 4.0f * px;
            const float rot = t * 40.0f;
            for (int k = 0; k < 8; ++k) {
                const float a0 = rot + k * 45.0f;
                DrawRing(pos, rr - 1.0f * px, rr + 1.0f * px, a0, a0 + 25.0f, 6, Fade(theme::kGold, 0.75f));
            }
        }
        if (b.id == hoveredId && b.id != selectedId) {
            ring(pos, r + 5.0f * px + 1.5f * px * std::sin(t * 6.0f), 2.0f * px, Fade(WHITE, 0.55f));
        }
    }
}

void Renderer::drawEffects(const GravityEngine& e) const {
    const float px = 1.0f / camera.zoom;
    for (const auto& s : e.effects.shockwaves) {
        const float alpha = std::clamp(1.0f - s.radius / s.maxRadius, 0.0f, 1.0f);
        const Vector2 c = toVector2(s.center);
        ring(c, s.radius, std::max(2.5f * px, s.radius * 0.06f), Fade(toColor(s.color), alpha * 0.8f));
        ring(c, s.radius * 0.92f, 1.2f * px, Fade(WHITE, alpha * 0.45f));
    }
    for (const auto& p : e.effects.particles) {
        const float alpha = std::clamp(p.lifetime / p.maxLifetime, 0.0f, 1.0f);
        const Vector2 pos = toVector2(p.pos);
        const Color c = toColor(p.color);
        DrawCircleV(pos, p.size * 2.2f, Fade(c, alpha * 0.3f));
        DrawCircleV(pos, p.size, Fade(c, alpha));
        if (p.vel.lengthSq() > 25.0) {
            const Vector2 tail = toVector2(p.pos - p.vel * 0.035);
            DrawLineEx(pos, tail, std::max(1.2f * px, p.size * 0.7f), Fade(c, alpha * 0.75f));
        }
    }
}

void Renderer::drawReticle(const Body& b) const {
    const float px = 1.0f / camera.zoom;
    const Vector2 pos = toVector2(b.pos);
    const float r = static_cast<float>(b.radius);
    const float t = ui.time();
    const float box = r + 10.0f * px + 2.0f * px * std::sin(t * 4.0f);
    const float len = std::max(7.0f * px, box * 0.35f);
    const float th = 2.0f * px;
    const Color col = Color{255, 220, 40, static_cast<unsigned char>(200 + 55 * std::sin(t * 6.0f))};

    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const Vector2 corner{pos.x + sx * box, pos.y + sy * box};
            DrawLineEx(corner, Vector2{corner.x - sx * len, corner.y}, th, col);
            DrawLineEx(corner, Vector2{corner.x, corner.y - sy * len}, th, col);
        }
    }
    // Rotating orbit-marker dots
    for (int k = 0; k < 3; ++k) {
        const float a = t * 1.6f + k * kTwoPiF / 3.0f;
        DrawCircleV(Vector2{pos.x + std::cos(a) * box * 1.15f, pos.y + std::sin(a) * box * 1.15f}, 2.2f * px, Fade(col, 0.8f));
    }

    // Net acceleration direction (fixed on-screen length)
    if (b.acc.lengthSq() > 1e-9) {
        const Vec2 dir = b.acc.normalized();
        const Vector2 end{pos.x + static_cast<float>(dir.x) * (r + 34.0f * px), pos.y + static_cast<float>(dir.y) * (r + 34.0f * px)};
        const Vector2 start{pos.x + static_cast<float>(dir.x) * r, pos.y + static_cast<float>(dir.y) * r};
        DrawLineEx(start, end, 1.8f * px, Fade(ORANGE, 0.9f));
        DrawCircleV(end, 2.8f * px, ORANGE);
    }
}

void Renderer::drawVectors(const GravityEngine& e) const {
    if (!view.vectors) return;
    const float px = 1.0f / camera.zoom;
    for (const auto& b : e.bodies) {
        const double speed = b.vel.length();
        if (speed < 0.1) continue;
        // Log-scaled on-screen length so slow and fast scenes both stay readable.
        const float lenPx = std::min(120.0f, 18.0f * std::log2(1.0f + static_cast<float>(speed) / 5.0f));
        const Vec2 dir = b.vel / speed;
        const Vector2 start = toVector2(b.pos + dir * b.radius);
        const Vector2 end = toVector2(b.pos + dir * (b.radius + lenPx * px));
        DrawLineEx(start, end, 1.6f * px, Fade(GREEN, 0.8f));
        const Vector2 d = toVector2(dir);
        const Vector2 perp{-d.y, d.x};
        const float a = 5.0f * px;
        const Vector2 back = Vector2Subtract(end, Vector2Scale(d, a));
        DrawTriangle(end, Vector2Subtract(back, Vector2Scale(perp, a * 0.5f)), Vector2Add(back, Vector2Scale(perp, a * 0.5f)),
                     Fade(GREEN, 0.9f));
        DrawTriangle(end, Vector2Add(back, Vector2Scale(perp, a * 0.5f)), Vector2Subtract(back, Vector2Scale(perp, a * 0.5f)),
                     Fade(GREEN, 0.9f));
    }
}

void Renderer::drawSlingshot(const GravityEngine& e, const SlingshotView& s) const {
    if (!s.active) return;
    const float px = 1.0f / camera.zoom;
    const Vector2 start = toVector2(s.start);
    const Vector2 cur = toVector2(s.current);
    const Color bodyCol = toColor(s.color);
    const float t = ui.time();

    // Elastic band
    const float stretch = Vector2Distance(start, cur) * camera.zoom;
    const Color band = mixColor(theme::kGood, theme::kDanger, std::clamp(stretch / 400.0f, 0.0f, 1.0f));
    DrawLineEx(start, cur, 2.5f * px, Fade(band, 0.8f));
    DrawCircleV(cur, 5.0f * px, band);

    // Trajectory preview, colour-coded
    Color pathCol = theme::kSky;
    if (s.prediction) {
        const TrajectoryPrediction& p = *s.prediction;
        if (p.hitsBody || p.wouldShatter) pathCol = theme::kDanger;
        else if (p.hasOrbitClass) pathCol = p.bound ? theme::kGood : theme::kWarn;
        const std::size_t n = p.points.size();
        for (std::size_t i = 1; i < n; ++i) {
            // Animated dashes flowing along the path
            const float phase = std::fmod(static_cast<float>(i) * 0.25f - t * 4.0f, 2.0f);
            if (phase < 0.0f ? phase > -1.0f : phase < 1.0f) {
                const float alpha = 1.0f - static_cast<float>(i) / static_cast<float>(n) * 0.75f;
                DrawLineEx(toVector2(p.points[i - 1]), toVector2(p.points[i]), 2.2f * px, Fade(pathCol, alpha));
            }
        }
        if (n > 0 && (p.hitsBody || p.wouldShatter)) {
            const Vector2 end = toVector2(p.endPos);
            const float k = 7.0f * px * (1.0f + 0.2f * std::sin(t * 10.0f));
            DrawLineEx(Vector2{end.x - k, end.y - k}, Vector2{end.x + k, end.y + k}, 2.5f * px, theme::kDanger);
            DrawLineEx(Vector2{end.x - k, end.y + k}, Vector2{end.x + k, end.y - k}, 2.5f * px, theme::kDanger);
            ring(end, k * 1.6f, 1.5f * px, Fade(theme::kDanger, 0.6f));
        }
    }

    // Ghost of the body about to launch
    const float r = static_cast<float>(s.radius);
    DrawCircleV(start, r * 1.5f, Fade(bodyCol, 0.25f));
    DrawCircleV(start, r, Fade(bodyCol, 0.8f));
    ring(start, r + 2.0f * px * (1.0f + std::sin(t * 8.0f)), 1.5f * px, Fade(WHITE, 0.8f));
    (void)e;
}

void Renderer::drawWorld(const GravityEngine& e, int selectedId, int hoveredId, const SlingshotView& sling) {
    drawGrid();
    drawRoom(e);
    drawTrails(e);
    drawBodies(e, selectedId, hoveredId);
    drawEffects(e);
    if (const Body* sel = e.getBodyById(selectedId)) drawReticle(*sel);
    drawVectors(e);
    drawSlingshot(e, sling);
}

// ---------------------------------------------------------------------------
// Screen-space overlay: labels, hover card, slingshot readout
// ---------------------------------------------------------------------------

std::pair<std::string, Color> Renderer::describeMotion(const GravityEngine& e, const Body& b) const {
    if (b.isStatic) return {"Anchored in place", theme::kGold};
    if (e.roomBounds.enabled) {
        const bool resting = std::abs(b.vel.y) < 1e-6 && b.pos.y + b.radius >= e.roomBounds.yMax - 0.5;
        if (resting) return {b.vel.lengthSq() < 1e-6 ? "Resting on the floor" : "Rolling on the floor", theme::kDim};
        return {b.vel.y < 0.0 ? "Flying upward" : "Falling", theme::kSky};
    }
    const OrbitInfo o = e.orbitInfo(b.id);
    if (!o.valid) return {"Drifting freely", theme::kDim};
    if (o.bound) {
        if (o.eccentricity < 0.1) return {"Circular orbit around " + o.attractorName, theme::kGood};
        return {"Elliptical orbit around " + o.attractorName, theme::kGood};
    }
    return {"Escaping " + o.attractorName, theme::kWarn};
}

void Renderer::drawWorldOverlay(const GravityEngine& e, int selectedId, int hoveredId, const SlingshotView& sling) {
    const Rectangle vp = viewport();
    const float fs = ui.s(13.0f);

    if (view.labels) {
        int drawn = 0;
        for (const auto& b : e.bodies) {
            const bool important = b.id == selectedId || b.id == hoveredId;
            if (!important && (b.isDebris || camera.zoom < 0.08f)) continue;
            if (!important && drawn >= 40) continue;
            const Vector2 sp = worldToScreen(b.pos);
            const float rpx = static_cast<float>(b.radius) * camera.zoom;
            if (sp.x < vp.x - 50 || sp.x > vp.x + vp.width + 50 || sp.y < -20 || sp.y > vp.height + 20) continue;
            ui.text(b.name.c_str(), sp.x + rpx + ui.s(6), sp.y - fs * 0.6f, fs,
                    Fade(important ? theme::kGold : theme::kText, important ? 1.0f : 0.8f), important);
            ++drawn;
        }
    }

    const Units u = unitsFor(e);
    if (hoveredId != -1 && hoveredId != selectedId && !sling.active) {
        if (const Body* b = e.getBodyById(hoveredId)) {
            const auto [motion, col] = describeMotion(e, *b);
            std::string tip = b->name + "\nmass " + u.mass(b->mass) + "   speed " + u.speed(b->vel.length()) + "\n" + motion +
                              "\nclick to inspect";
            ui.showTooltip(tip.c_str());
            (void)col;
        }
    }

    if (sling.active && sling.prediction) {
        const TrajectoryPrediction& p = *sling.prediction;
        std::string line1 = "Launch speed " + u.speed(sling.launchVel.length());
        std::string line2;
        Color c2 = theme::kSky;
        if (p.wouldShatter) { line2 = "Will SHATTER on impact!"; c2 = theme::kDanger; }
        else if (p.hitsBody) { line2 = "Impact course: " + p.hitName; c2 = theme::kDanger; }
        else if (p.hasOrbitClass) {
            line2 = p.bound ? "Bound orbit around " + p.attractorName : "Escape trajectory from " + p.attractorName;
            c2 = p.bound ? theme::kGood : theme::kWarn;
        } else if (e.roomBounds.enabled) { line2 = "Safe throw - will bounce"; c2 = theme::kGood; }

        const Vector2 m = GetMousePosition();
        const float w = std::max(ui.measure(line1.c_str(), fs, true), ui.measure(line2.c_str(), fs, true)) + ui.s(20);
        Rectangle r{m.x + ui.s(18), m.y + ui.s(14), w, line2.empty() ? ui.s(30) : ui.s(50)};
        ui.roundedRect(r, ui.s(7), Color{6, 10, 20, 230});
        ui.roundedOutline(r, ui.s(7), 1.5f, Fade(c2, 0.8f));
        ui.text(line1.c_str(), r.x + ui.s(10), r.y + ui.s(8), fs, theme::kText, true);
        if (!line2.empty()) ui.text(line2.c_str(), r.x + ui.s(10), r.y + ui.s(28), fs, c2, true);
    }
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

void Renderer::drawHud(const GravityEngine& e, const SimStats& stats, float timeScale, UiActions& a) {
    const float x = ui.s(14);
    const float y0 = ui.s(14);
    const float w = ui.s(300);
    const float lh = ui.s(23);
    const float fs = ui.s(14);
    const float h = ui.s(286);
    ui.panel(Rectangle{x, y0, w, h});

    float y = y0 + ui.s(12);
    const bool room = e.roomBounds.enabled;
    ui.text(GravityEngine::sceneName(e.currentScene()), x + ui.s(14), y, ui.s(18), room ? theme::kGood : theme::kGold, true);
    y += ui.s(26);
    ui.text((std::to_string(e.getBodyCount()) + " bodies   " + std::to_string(GetFPS()) + " FPS   t = " +
             fmtNum(e.simulationTime()) + " s").c_str(),
            x + ui.s(14), y, ui.s(12.5f), theme::kDim);
    y += ui.s(24);

    const float labelX = x + ui.s(14);
    const float valX = x + ui.s(118);
    const float valW = w - ui.s(132);

    ui.text("Integrator", labelX, y + ui.s(4), fs, theme::kDim);
    if (ui.button("hud_integ", Rectangle{valX, y, valW, ui.s(26)}, e.getIntegratorName(), ButtonStyle::Normal, false,
                  "Click or press I to switch.\nVerlet: 2nd order, energy-stable.\nEuler: 1st order, for comparison.",
                  ui.s(13))) {
        a.cycleIntegrator = true;
    }
    y += lh + ui.s(8);

    ui.text("Collisions", labelX, y + ui.s(4), fs, theme::kDim);
    const ButtonStyle cs = e.collisionMode == CollisionMode::DestructiveShatter ? ButtonStyle::Danger
                         : e.collisionMode == CollisionMode::InelasticMerge   ? ButtonStyle::Accent
                         : e.collisionMode == CollisionMode::Elastic          ? ButtonStyle::Good
                                                                              : ButtonStyle::Normal;
    if (ui.button("hud_coll", Rectangle{valX, y, valW, ui.s(26)}, e.getCollisionModeName(), cs, false,
                  "Click or press C to cycle:\nMerge -> Shatter -> Bounce -> Ghost", ui.s(13))) {
        a.cycleCollision = true;
    }
    y += lh + ui.s(10);

    const EnergyReport er = e.energyReport();
    ui.text("Energy", labelX, y, fs, theme::kDim);
    ui.text(format("%+.4e", er.total).c_str(), valX, y, fs, theme::kText);
    y += lh;
    ui.text("Drift", labelX, y, fs, theme::kDim);
    if (er.driftMeaningful) {
        const Color dc = er.driftPercent < 0.01 ? theme::kGood : (er.driftPercent < 0.1 ? theme::kWarn : theme::kDanger);
        ui.text(format("%.5f %%", er.driftPercent).c_str(), valX, y, fs, dc, true);
    } else {
        ui.text("n/a (lab is dissipative)", valX, y, ui.s(12.5f), theme::kDim);
    }
    y += lh;

    ui.text("Speed", labelX, y, fs, theme::kDim);
    if (stats.paused) {
        ui.text("PAUSED", valX, y, fs, theme::kDanger, true);
    } else if (stats.lagging) {
        ui.text(format2("%.2fx (real %.2fx)", timeScale, stats.actualSpeed).c_str(), valX, y, fs, theme::kWarn, true);
    } else {
        ui.text(format("%.2fx", timeScale).c_str(), valX, y, fs, theme::kText);
    }
    y += lh;

    ui.text("Events", labelX, y, fs, theme::kDim);
    ui.text((std::to_string(stats.merges) + " merged   " + std::to_string(stats.shatters) + " shattered   " +
             std::to_string(stats.explosions) + " boom").c_str(),
            valX, y, ui.s(12.5f), theme::kText);
    y += lh + ui.s(4);

    ui.text("F1 or ? for all controls", labelX, y, ui.s(12), Fade(theme::kSky, 0.8f));
}

void Renderer::drawTimeBar(const SimStats& stats, float& timeScale, UiActions& a) {
    const Rectangle vp = viewport();
    const float h = ui.s(56);
    const float w = std::min(ui.s(760), vp.width - ui.s(28));
    Rectangle bar{vp.x + (vp.width - w) * 0.5f, vp.height - h - ui.s(14), w, h};
    ui.panel(bar);

    const float pad = ui.s(10);
    const float bh = h - pad * 2;
    float x = bar.x + pad;
    const float y = bar.y + pad;

    if (ui.button("tb_play", Rectangle{x, y, ui.s(62), bh}, stats.paused ? "PLAY" : "PAUSE",
                  stats.paused ? ButtonStyle::Good : ButtonStyle::Normal, false, "Space")) {
        a.togglePause = true;
    }
    x += ui.s(68);
    if (ui.button("tb_step", Rectangle{x, y, ui.s(56), bh}, "STEP", ButtonStyle::Normal, false,
                  "Advance one frame (N)\nPauses the simulation")) {
        a.stepOnce = true;
    }
    x += ui.s(66);

    double ts = timeScale;
    const float sliderW = std::max(ui.s(80), std::min(ui.s(170), w * 0.24f));
    if (ui.slider("tb_speed", Rectangle{x, y, sliderW, bh}, ts, 0.1, 20.0, true, theme::kGold,
                  "Simulation speed  ( [ / ] or , / . )")) {
        timeScale = static_cast<float>(ts);
    }
    x += sliderW + ui.s(8);
    ui.text(format("%.2fx", timeScale).c_str(), x, y + (bh - ui.s(15)) * 0.5f, ui.s(15), theme::kGold, true);
    x += ui.s(58);

    struct ChipDef {
        const char* id;
        const char* label;
        bool* flag;
        Color accent;
        const char* tip;
    };
    ChipDef chips[] = {
        {"tb_trails", "Trails", &view.trails, theme::kSky, "Orbital trails (T)"},
        {"tb_vec", "Vectors", &view.vectors, theme::kGood, "Velocity arrows (V)"},
        {"tb_grid", "Grid", &view.grid, theme::kDim, "Adaptive coordinate grid (G)"},
        {"tb_labels", "Labels", &view.labels, theme::kText, "Body names (L)"},
        {"tb_follow", "Follow", &view.followHeaviest, theme::kGold, "Camera follows the heaviest body (F)"},
    };
    const float remaining = bar.x + bar.width - pad - x;
    const float chipW = std::max(ui.s(40), (remaining - ui.s(6) * 4) / 5.0f);
    for (auto& c : chips) {
        if (ui.chip(c.id, Rectangle{x, y + ui.s(3), chipW, bh - ui.s(6)}, c.label, *c.flag, c.accent, c.tip)) {
            *c.flag = !*c.flag;
        }
        x += chipW + ui.s(6);
    }
}

void Renderer::drawToasts() {
    const Rectangle vp = viewport();
    float y = ui.s(16);
    for (const Toast& t : toasts_) {
        const float in = easeOutCubic(t.age / 0.25f);
        const float out = std::clamp((t.life - t.age) / 0.4f, 0.0f, 1.0f);
        const float a = std::min(in, out);
        const float fs = ui.s(15);
        const float w = ui.measure(t.text.c_str(), fs, true) + ui.s(36);
        const float h = ui.s(36);
        Rectangle r{vp.x + (vp.width - w) * 0.5f, y - (1.0f - in) * ui.s(20), w, h};
        ui.roundedRect(r, h * 0.5f, Fade(Color{8, 12, 24, 255}, 0.92f * a));
        ui.roundedOutline(r, h * 0.5f, 1.5f, Fade(t.color, a));
        DrawCircleV(Vector2{r.x + ui.s(16), r.y + h * 0.5f}, ui.s(4), Fade(t.color, a));
        ui.text(t.text.c_str(), r.x + ui.s(26), r.y + (h - fs) * 0.5f, fs, Fade(theme::kText, a), true);
        y += (h + ui.s(8)) * a;
    }
}

void Renderer::drawFlash(float flash) {
    if (flash <= 0.01f) return;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(Color{255, 240, 220, 255}, flash * 0.35f));
}

void Renderer::drawHelp(UiActions& a) {
    const float W = static_cast<float>(GetScreenWidth());
    const float H = static_cast<float>(GetScreenHeight());
    DrawRectangle(0, 0, static_cast<int>(W), static_cast<int>(H), Fade(BLACK, 0.6f));

    struct Row {
        const char* key;
        const char* what;
    };
    static const Row left[] = {
        {"Left click body", "Select & inspect"},   {"Left drag (space)", "Slingshot launch"},
        {"Wheel while dragging", "Change launch mass"}, {"Right drag", "Pan camera"},
        {"Mouse wheel", "Zoom to cursor"},          {"W A S D / arrows", "Pan camera"},
        {"Space", "Pause / resume"},               {"N", "Step one frame"},
        {"[ ]  or  , .", "Slower / faster"},       {"+ / -", "Launch mass"},
        {"Esc", "Close / cancel / deselect"},      {"F1 or ?", "This help"},
    };
    static const Row right[] = {
        {"1 2 3 4", "Solar / Figure-8 / Binary / Lab"}, {"0", "Empty space"},
        {"I", "Toggle integrator"},                  {"C", "Cycle collision mode"},
        {"K", "Explode selected body"},              {"Delete", "Delete selected body"},
        {"T V G L", "Trails / vectors / grid / labels"}, {"F", "Follow heaviest body"},
        {"R", "Re-frame the scene"},                 {"H", "Show / hide sidebar"},
        {"Tab", "Next sidebar tab"},                 {"X  (twice)", "Clear all bodies"},
    };

    const float pw = std::min(ui.s(860), W - ui.s(40));
    const float rowH = ui.s(27);
    const float ph = ui.s(110) + rowH * 12;
    Rectangle p{(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
    ui.panel(p, Color{10, 14, 26, 250}, Fade(theme::kGold, 0.7f));
    ui.text("CONTROLS", p.x + ui.s(24), p.y + ui.s(20), ui.s(24), theme::kGold, true);
    ui.text("Tip: hover any button for a hint. Everything in the HUD and time bar is clickable.", p.x + ui.s(24),
            p.y + ui.s(52), ui.s(13.5f), theme::kDim);

    const float colW = (pw - ui.s(48)) * 0.5f;
    auto drawCol = [&](const Row* rows, int n, float cx) {
        float y = p.y + ui.s(86);
        for (int i = 0; i < n; ++i) {
            Rectangle key{cx, y, ui.s(150), rowH - ui.s(6)};
            ui.roundedRect(key, ui.s(5), Color{30, 40, 64, 255});
            ui.textCentered(rows[i].key, key, ui.s(12.5f), theme::kSky, true);
            ui.text(rows[i].what, cx + ui.s(162), y + ui.s(3), ui.s(14), theme::kText);
            y += rowH;
        }
    };
    drawCol(left, 12, p.x + ui.s(24));
    drawCol(right, 12, p.x + ui.s(24) + colW);

    if (ui.button("help_close", Rectangle{p.x + pw - ui.s(110), p.y + ui.s(18), ui.s(90), ui.s(32)}, "CLOSE",
                  ButtonStyle::Accent)) {
        a.toggleHelp = true;
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), p)) {
        a.toggleHelp = true;
    }
    ui.block(Rectangle{0, 0, W, H});
}
