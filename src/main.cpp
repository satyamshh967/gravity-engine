#include "GravityEngine.hpp"
#include "LaunchSettings.hpp"
#include "PhysicsTests.hpp"
#include "Renderer.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {

struct App {
    GravityEngine engine;
    Renderer renderer;
    LaunchSettings launch;
    SimStats stats;

    float timeScale{1.0f};
    double accumulator{0.0};
    bool pendingStep{false};
    float lagHold{0.0f};

    int selectedId{-1};
    int hoveredId{-1};

    bool dragging{false};
    Vec2 dragStart{};
    Vec2 dragCurrent{};
    bool rightPanning{false};
    TrajectoryPrediction prediction;

    double clearArmedUntil{-1.0};
    int cursor{MOUSE_CURSOR_DEFAULT};
};

bool isNoiseName(const std::string& n) { return n.rfind("Dust", 0) == 0 || n.rfind("Debris", 0) == 0; }

double launchVelocityFactor(const GravityEngine& e) { return e.roomBounds.enabled ? 2.0 : 0.5; }

void loadScene(App& app, SceneId id) {
    app.engine.loadScene(id);
    app.selectedId = -1;
    app.hoveredId = -1;
    app.dragging = false;
    app.accumulator = 0.0;
    app.renderer.followedBodyId = -1;
    app.renderer.view.followHeaviest = false;
    app.renderer.fitScene(app.engine.sceneView(), false);

    // Offer payloads that make sense for the scene.
    const bool room = app.engine.roomBounds.enabled;
    const LaunchPresetType p = app.launch.presetType;
    const bool labPreset = p == LaunchPresetType::HeavyAnvil || p == LaunchPresetType::LightApple || p == LaunchPresetType::RubberBall;
    if (room && !labPreset) {
        app.launch.applyPreset(LaunchPresetType::RubberBall);
        if (app.launch.spawnMode == LaunchSpawnMode::CircularOrbit) app.launch.spawnMode = LaunchSpawnMode::Stationary;
    } else if (!room && labPreset) {
        app.launch.applyPreset(LaunchPresetType::Terrestrial);
    }
    app.renderer.pushToast(std::string("Scene: ") + GravityEngine::sceneName(id), theme::kGold);
}

void cycleCollision(App& app) {
    GravityEngine& e = app.engine;
    switch (e.collisionMode) {
        case CollisionMode::InelasticMerge: e.collisionMode = CollisionMode::DestructiveShatter; break;
        case CollisionMode::DestructiveShatter: e.collisionMode = CollisionMode::Elastic; break;
        case CollisionMode::Elastic: e.collisionMode = CollisionMode::None; break;
        case CollisionMode::None: e.collisionMode = CollisionMode::InelasticMerge; break;
    }
    app.renderer.pushToast(std::string("Collisions: ") + e.getCollisionModeName(), theme::kSky);
}

void setIntegrator(App& app, IntegratorType t) {
    app.engine.integrator = t;
    app.engine.markEnergyBaseline();
    app.renderer.pushToast(std::string("Integrator: ") + app.engine.getIntegratorName(), theme::kSky);
}

void spawnBody(App& app, Vec2 pos, Vec2 vel) {
    GravityEngine& e = app.engine;
    const LaunchSettings& L = app.launch;
    if (e.getBodyCount() >= e.maxBodies) {
        app.renderer.pushToast("Body limit reached (" + std::to_string(e.maxBodies) + ")", theme::kDanger);
        return;
    }
    const int id = e.allocateId();
    Body b(id, L.typeName + " #" + std::to_string(id), L.mass, L.radius, pos, vel, L.color, L.isStatic);
    b.maxTrailPoints = L.trailLength;
    e.addBody(std::move(b));
    e.spawnShockwave(pos, static_cast<float>(L.radius * 3.0 + 12.0), L.color, 140.0f);
    app.selectedId = id;
}

Vec2 clampToRoom(const GravityEngine& e, Vec2 p, double radius) {
    if (!e.roomBounds.enabled) return p;
    const RoomBounds& rb = e.roomBounds;
    const double r = std::min(radius, 0.45 * std::min(rb.xMax - rb.xMin, rb.yMax - rb.yMin));
    return Vec2{std::clamp(p.x, rb.xMin + r, rb.xMax - r), std::clamp(p.y, rb.yMin + r, rb.yMax - r)};
}

void deselectIfGone(App& app) {
    if (app.selectedId != -1 && !app.engine.getBodyById(app.selectedId)) app.selectedId = -1;
    if (app.renderer.followedBodyId != -1 && !app.engine.getBodyById(app.renderer.followedBodyId)) {
        app.renderer.followedBodyId = -1;
    }
}

void requestClearAll(App& app) {
    app.engine.clearBodies();
    app.selectedId = -1;
    app.renderer.followedBodyId = -1;
    app.renderer.pushToast("All bodies cleared", theme::kDanger);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void handleKeys(App& app, UiActions& a) {
    Renderer& r = app.renderer;

    if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_SLASH)) a.toggleHelp = true;

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (r.helpVisible) a.toggleHelp = true;
        else if (app.dragging) app.dragging = false;
        else if (app.selectedId != -1) a.deselect = true;
        else if (r.sidebarVisible) r.sidebarVisible = false;
    }
    if (r.helpVisible) return; // the help overlay is modal

    if (IsKeyPressed(KEY_SPACE)) a.togglePause = true;
    if (IsKeyPressed(KEY_N)) a.stepOnce = true;
    if (IsKeyPressed(KEY_H)) r.sidebarVisible = !r.sidebarVisible;
    if (IsKeyPressed(KEY_TAB)) {
        if (!r.sidebarVisible) {
            r.sidebarVisible = true;
        } else {
            r.activeTab = r.activeTab == SidebarTab::Inspector ? SidebarTab::Spawner
                        : r.activeTab == SidebarTab::Spawner   ? SidebarTab::World
                                                               : SidebarTab::Inspector;
        }
    }

    const struct {
        int key;
        SceneId id;
    } sceneKeys[] = {{KEY_ONE, SceneId::SolarSystem}, {KEY_TWO, SceneId::FigureEight}, {KEY_THREE, SceneId::BinaryDisk},
                     {KEY_FOUR, SceneId::EarthRoom},   {KEY_ZERO, SceneId::Empty}};
    for (const auto& sk : sceneKeys) {
        if (IsKeyPressed(sk.key)) {
            a.sceneRequested = true;
            a.scene = sk.id;
        }
    }

    if (IsKeyPressed(KEY_I)) a.cycleIntegrator = true;
    if (IsKeyPressed(KEY_C)) a.cycleCollision = true;
    if (IsKeyPressed(KEY_K) && app.selectedId != -1) a.explodeSelected = true;
    if (IsKeyPressed(KEY_DELETE) && app.selectedId != -1) a.deleteSelected = true;

    if (IsKeyPressed(KEY_T)) r.view.trails = !r.view.trails;
    if (IsKeyPressed(KEY_V)) r.view.vectors = !r.view.vectors;
    if (IsKeyPressed(KEY_G)) r.view.grid = !r.view.grid;
    if (IsKeyPressed(KEY_L)) r.view.labels = !r.view.labels;
    if (IsKeyPressed(KEY_F)) {
        r.view.followHeaviest = !r.view.followHeaviest;
        r.followedBodyId = -1;
    }
    if (IsKeyPressed(KEY_R)) a.fitScene = true;

    // X must be pressed twice within 2.5 s: clearing everything is destructive.
    if (IsKeyPressed(KEY_X)) {
        if (GetTime() < app.clearArmedUntil) {
            a.clearAll = true;
            app.clearArmedUntil = -1.0;
        } else {
            app.clearArmedUntil = GetTime() + 2.5;
            r.pushToast("Press X again to clear all bodies", theme::kWarn);
        }
    }

    if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_PERIOD)) app.timeScale = std::min(app.timeScale * 1.5f, 20.0f);
    if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_COMMA)) app.timeScale = std::max(app.timeScale / 1.5f, 0.1f);
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) app.launch.scaleMass(1.25);
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) app.launch.scaleMass(1.0 / 1.25);
}

// ---------------------------------------------------------------------------
// Mouse in the world
// ---------------------------------------------------------------------------

void handleWorldMouse(App& app, bool overUI, Vec2 mouseWorld) {
    GravityEngine& e = app.engine;
    Renderer& r = app.renderer;

    if (!overUI && !app.dragging && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (app.hoveredId != -1) {
            app.selectedId = app.hoveredId;
            r.activeTab = SidebarTab::Inspector;
            r.sidebarVisible = true;
        } else {
            const Vec2 pos = clampToRoom(e, mouseWorld, app.launch.radius);
            switch (app.launch.spawnMode) {
                case LaunchSpawnMode::Slingshot:
                    app.dragging = true;
                    app.dragStart = pos;
                    app.dragCurrent = mouseWorld;
                    break;
                case LaunchSpawnMode::CircularOrbit:
                    spawnBody(app, pos, e.roomBounds.enabled ? Vec2{} : e.computeCircularOrbitVelocity(pos, app.launch.mass));
                    break;
                case LaunchSpawnMode::Stationary:
                    spawnBody(app, pos, Vec2{});
                    break;
            }
        }
    }

    if (!app.dragging) return;
    app.dragCurrent = mouseWorld;

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) app.launch.scaleMass(std::pow(1.2, static_cast<double>(wheel)));

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        app.dragging = false; // cancel
        r.pushToast("Launch cancelled", theme::kDim);
        return;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vec2 vel = (app.dragStart - app.dragCurrent) * launchVelocityFactor(e);
        if (vel.length() > 5000.0) vel = vel.normalized() * 5000.0;
        spawnBody(app, app.dragStart, vel);
        app.dragging = false;
    }
}

// ---------------------------------------------------------------------------
// Physics with a fixed timestep and a per-frame CPU budget
// ---------------------------------------------------------------------------

void runPhysics(App& app, float dt) {
    GravityEngine& e = app.engine;
    if (!app.stats.paused) {
        app.accumulator += static_cast<double>(dt) * app.timeScale;
    } else if (app.pendingStep) {
        app.accumulator += app.timeScale / 60.0;
    }
    app.pendingStep = false;

    const double budget = 0.012; // seconds of CPU per frame for physics
    const double start = GetTime();
    int steps = 0;
    double simulated = 0.0;
    bool lagging = false;
    while (app.accumulator >= e.fixedDt) {
        e.step(e.fixedDt);
        app.accumulator -= e.fixedDt;
        simulated += e.fixedDt;
        ++steps;
        if ((steps & 7) == 0 && GetTime() - start > budget) {
            // Can't keep up: drop the backlog instead of spiralling, and say so.
            lagging = app.accumulator >= e.fixedDt;
            app.accumulator = 0.0;
            break;
        }
    }
    if (simulated > 0.0) e.updateEffects(simulated);

    app.stats.stepsLastFrame = steps;
    if (lagging) app.lagHold = 0.75f;
    app.lagHold = std::max(0.0f, app.lagHold - dt);
    app.stats.lagging = app.lagHold > 0.0f;
    if (!app.stats.paused && dt > 0.0f) {
        const float actual = static_cast<float>(simulated / dt);
        app.stats.actualSpeed += (actual - app.stats.actualSpeed) * std::min(1.0f, dt * 4.0f);
    }
}

void drainEvents(App& app) {
    GravityEngine& e = app.engine;
    Renderer& r = app.renderer;
    for (const SimEvent& ev : e.events) {
        // Dust/debris events are counted but don't get a toast (the disk scene has hundreds).
        const bool noisy = isNoiseName(ev.nameA) || isNoiseName(ev.nameB);
        switch (ev.type) {
            case SimEventType::Merge:
                ++app.stats.merges;
                if (!noisy) r.pushToast(ev.nameA + " + " + ev.nameB + " merged", theme::kViolet);
                break;
            case SimEventType::Shatter:
                ++app.stats.shatters;
                if (!noisy) r.pushToast(ev.nameA + " and " + ev.nameB + " SHATTERED!", theme::kDanger);
                break;
            case SimEventType::Explosion:
                ++app.stats.explosions;
                r.pushToast(ev.nameA + " exploded!", theme::kWarn);
                break;
            case SimEventType::FloorShatter:
                ++app.stats.shatters;
                if (!noisy) r.pushToast(ev.nameA + " shattered on impact!", theme::kDanger);
                break;
        }
        // Keep selection and camera lock on the merged body.
        const int replacement = ev.type == SimEventType::Merge ? ev.survivorId : -1;
        if (app.selectedId == ev.absorbedId) app.selectedId = replacement;
        if (r.followedBodyId == ev.absorbedId) r.followedBodyId = replacement;
        if (ev.type == SimEventType::Shatter && app.selectedId == ev.survivorId) app.selectedId = -1;
    }
    e.events.clear();
    deselectIfGone(app);
}

void applyActions(App& app, const UiActions& a) {
    GravityEngine& e = app.engine;
    Renderer& r = app.renderer;

    if (a.toggleHelp) r.helpVisible = !r.helpVisible;
    if (a.sceneRequested) loadScene(app, a.scene);
    if (a.togglePause) app.stats.paused = !app.stats.paused;
    if (a.stepOnce) {
        app.stats.paused = true;
        app.pendingStep = true;
    }
    if (a.cycleIntegrator) {
        setIntegrator(app, e.integrator == IntegratorType::VelocityVerlet ? IntegratorType::SymplecticEuler
                                                                          : IntegratorType::VelocityVerlet);
    }
    if (a.setIntegrator && a.integrator != e.integrator) setIntegrator(app, a.integrator);
    if (a.cycleCollision) cycleCollision(app);
    if (a.setCollisionMode && a.collisionMode != e.collisionMode) {
        e.collisionMode = a.collisionMode;
        r.pushToast(std::string("Collisions: ") + e.getCollisionModeName(), theme::kSky);
    }
    if (a.bodyEdited || a.physicsEdited) e.notifyBodiesEdited();
    if (a.circularizeSelected && app.selectedId != -1) e.circularizeOrbit(app.selectedId);
    if (a.explodeSelected && app.selectedId != -1) {
        if (e.explodeBody(app.selectedId)) app.selectedId = -1;
        else r.pushToast("Can't explode: body limit reached", theme::kDanger);
    }
    if (a.deleteSelected && app.selectedId != -1) {
        e.deleteBody(app.selectedId);
        app.selectedId = -1;
    }
    if (a.deselect) app.selectedId = -1;
    if (a.clearAll) requestClearAll(app);
    if (a.fitScene) r.fitScene(e.sceneView(), false);
    deselectIfGone(app);
}

// ---------------------------------------------------------------------------
// One frame
// ---------------------------------------------------------------------------

void frame(App& app) {
    GravityEngine& e = app.engine;
    Renderer& r = app.renderer;
    const float dt = std::min(GetFrameTime(), 0.1f);

    r.beginFrame(dt);
    UiActions actions;

    const Vector2 mouse = GetMousePosition();
    const bool overUI = r.helpVisible || r.ui.mouseOverUI(mouse);

    handleKeys(app, actions);

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !overUI && !app.dragging) app.rightPanning = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) app.rightPanning = false;
    r.updateCamera(dt, !overUI && !app.dragging, app.rightPanning, !r.helpVisible);

    // Camera follow: locked body first, else the heaviest body.
    if (r.followedBodyId != -1) {
        if (const Body* b = e.getBodyById(r.followedBodyId)) r.followPoint(b->pos, dt);
        else r.followedBodyId = -1;
    } else if (r.view.followHeaviest && !e.bodies.empty()) {
        const auto heaviest = std::max_element(e.bodies.begin(), e.bodies.end(),
                                               [](const Body& x, const Body& y) { return x.mass < y.mass; });
        r.followPoint(heaviest->pos, dt);
    }

    const Vec2 mouseWorld = r.screenToWorld(mouse);
    app.hoveredId = (!overUI && !app.dragging) ? e.findNearestBody(mouseWorld, r.pickPaddingWorld()) : -1;
    if (!r.helpVisible) handleWorldMouse(app, overUI, mouseWorld);

    runPhysics(app, dt);
    drainEvents(app);

    SlingshotView sling;
    if (app.dragging) {
        sling.active = true;
        sling.start = app.dragStart;
        sling.current = app.dragCurrent;
        sling.launchVel = (app.dragStart - app.dragCurrent) * launchVelocityFactor(e);
        sling.radius = app.launch.radius;
        sling.color = app.launch.color;
        app.prediction = e.predictTrajectory(app.dragStart, sling.launchVel, app.launch.radius, app.launch.predictionSteps, 0.01);
        sling.prediction = &app.prediction;
    }

    // --- Draw ---
    BeginDrawing();
    ClearBackground(theme::kBg);
    r.drawBackground();

    const Vector2 savedTarget = r.camera.target;
    if (!app.stats.paused && e.effects.cameraShake > 0.1f) {
        const float ang = static_cast<float>(GetRandomValue(0, 360)) * DEG2RAD;
        const float dist = static_cast<float>(GetRandomValue(5, 100)) * 0.01f * e.effects.cameraShake / r.camera.zoom;
        r.camera.target.x += std::cos(ang) * dist;
        r.camera.target.y += std::sin(ang) * dist;
    }
    BeginMode2D(r.camera);
    r.drawWorld(e, app.selectedId, app.hoveredId, sling);
    EndMode2D();
    r.camera.target = savedTarget;

    r.ui.inputEnabled = !r.helpVisible;
    r.drawWorldOverlay(e, app.selectedId, app.hoveredId, sling);
    r.drawHud(e, app.stats, app.timeScale, actions);
    r.drawTimeBar(app.stats, app.timeScale, actions);
    r.drawSidebar(e, e.getBodyById(app.selectedId), app.launch, app.hoveredId, actions);
    r.drawToasts();
    r.drawFlash(e.effects.flash);
    if (r.helpVisible) {
        r.ui.inputEnabled = true;
        r.drawHelp(actions);
    }
    r.endFrame();
    EndDrawing();

    const int cursor = (app.hoveredId != -1 || r.ui.wantsHandCursor()) ? MOUSE_CURSOR_POINTING_HAND
                     : app.dragging                                   ? MOUSE_CURSOR_CROSSHAIR
                                                                      : MOUSE_CURSOR_DEFAULT;
    if (cursor != app.cursor) {
        SetMouseCursor(cursor);
        app.cursor = cursor;
    }

    applyActions(app, actions);
}

void sizeWindowToMonitor() {
    const int mon = GetCurrentMonitor();
    const int mw = GetMonitorWidth(mon);
    const int mh = GetMonitorHeight(mon);
    if (mw <= 0 || mh <= 0) return;
    const int w = std::clamp(static_cast<int>(mw * 0.85f), 960, 2600);
    const int h = std::clamp(static_cast<int>(mh * 0.85f), 620, 1600);
    SetWindowSize(std::min(w, mw), std::min(h, mh));
    const Vector2 mp = GetMonitorPosition(mon);
    SetWindowPosition(static_cast<int>(mp.x) + (mw - std::min(w, mw)) / 2, static_cast<int>(mp.y) + (mh - std::min(h, mh)) / 2);
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0 || std::strcmp(argv[i], "-t") == 0) {
            return runPhysicsTests() == 0 ? 0 : 1;
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Gravity Engine - 2D N-Body Sandbox");
    SetExitKey(KEY_NULL); // ESC is handled by the app (close / cancel / deselect), not "quit"
    SetWindowMinSize(900, 600);
    sizeWindowToMonitor();
    SetTargetFPS(60);

    {
        App app;
        app.renderer.init();
        app.renderer.fitScene(app.engine.sceneView(), true);
        app.renderer.pushToast("Welcome! Drag in space to launch a planet - F1 for controls", theme::kGold);

        while (!WindowShouldClose()) frame(app);

        app.renderer.shutdown(); // free fonts while the GL context still exists
    }
    CloseWindow();
    return 0;
}
