#pragma once

#include "GravityEngine.hpp"
#include "LaunchSettings.hpp"
#include "Ui.hpp"

#include <raylib.h>

#include <string>
#include <utility>
#include <vector>

enum class SidebarTab { Inspector, Spawner, World };

struct ViewOptions {
    bool trails{true};
    bool vectors{false};
    bool grid{true};
    bool labels{true};
    bool followHeaviest{false};
};

struct SlingshotView {
    bool active{false};
    Vec2 start{};
    Vec2 current{};
    Vec2 launchVel{};
    double radius{5.0};
    Rgba color{};
    const TrajectoryPrediction* prediction{nullptr};
};

struct SimStats {
    bool paused{false};
    int stepsLastFrame{0};
    float actualSpeed{1.0f};
    bool lagging{false};
    int merges{0};
    int shatters{0};
    int explosions{0};
};

// Everything the UI asks the app to do this frame. The renderer never changes
// the simulation's structure itself; the app applies these after drawing.
struct UiActions {
    bool sceneRequested{false};
    SceneId scene{SceneId::Empty};
    bool deselect{false};
    bool deleteSelected{false};
    bool explodeSelected{false};
    bool circularizeSelected{false};
    bool bodyEdited{false};    // selected body's properties changed
    bool physicsEdited{false}; // G, softening, room parameters changed
    bool clearAll{false};
    bool togglePause{false};
    bool stepOnce{false};
    bool toggleHelp{false};
    bool cycleIntegrator{false};
    bool cycleCollision{false};
    bool setCollisionMode{false};
    CollisionMode collisionMode{CollisionMode::InelasticMerge};
    bool setIntegrator{false};
    IntegratorType integrator{IntegratorType::VelocityVerlet};
    bool fitScene{false};
};

// Display units: the Earth room is metric (pixelsPerMeter), space uses
// simulation units (mass M, distance px, time s).
struct Units {
    bool metric{false};
    double ppm{1.0};
    [[nodiscard]] std::string mass(double m) const;
    [[nodiscard]] std::string dist(double px) const;
    [[nodiscard]] std::string speed(double pxPerS) const;
    [[nodiscard]] std::string accel(double pxPerS2) const;
    [[nodiscard]] std::string vec(Vec2 v, bool velocity) const;
};
[[nodiscard]] Units unitsFor(const GravityEngine& e);
[[nodiscard]] std::string fmtNum(double v);

struct Toast {
    std::string text;
    Color color{WHITE};
    float age{0.0f};
    float life{3.2f};
};

class Renderer {
public:
    Ui ui;
    Camera2D camera{};
    ViewOptions view;
    SidebarTab activeTab{SidebarTab::Inspector};
    bool sidebarVisible{true};
    bool helpVisible{false};
    int followedBodyId{-1};

    void init();     // after InitWindow
    void shutdown(); // before CloseWindow (GPU resources need a live context)

    void beginFrame(float dt);
    void endFrame();

    // --- Camera ---
    [[nodiscard]] Rectangle viewport() const; // on-screen area not covered by the sidebar
    void updateCamera(float dt, bool allowWheelZoom, bool allowDragPan, bool allowKeyPan);
    void followPoint(Vec2 p, float dt);
    void fitScene(const SceneView& v, bool instant);
    [[nodiscard]] Vec2 screenToWorld(Vector2 p) const;
    [[nodiscard]] Vector2 worldToScreen(Vec2 p) const;
    [[nodiscard]] double pickPaddingWorld() const { return ui.s(10.0f) / camera.zoom; }

    // --- Drawing ---
    void drawBackground();
    void drawWorld(const GravityEngine& e, int selectedId, int hoveredId, const SlingshotView& sling);
    void drawWorldOverlay(const GravityEngine& e, int selectedId, int hoveredId, const SlingshotView& sling);
    void drawHud(const GravityEngine& e, const SimStats& stats, float timeScale, UiActions& a);
    void drawTimeBar(const SimStats& stats, float& timeScale, UiActions& a);
    void drawSidebar(GravityEngine& e, Body* selected, LaunchSettings& launch, int hoveredId, UiActions& a);
    void drawToasts();
    void drawHelp(UiActions& a);
    void drawFlash(float flash);

    void pushToast(std::string text, Color color);

    // Short human-readable description of how a body is moving.
    [[nodiscard]] std::pair<std::string, Color> describeMotion(const GravityEngine& e, const Body& b) const;

private:
    struct Star {
        float x, y, size, phase, speed, depth;
    };

    [[nodiscard]] float sidebarWidth() const;
    [[nodiscard]] float sidebarVisibleWidth() const;
    [[nodiscard]] Rectangle visibleWorldRect() const;

    void ring(Vector2 c, float radius, float thickWorld, Color col) const;
    void drawGrid();
    void drawRoom(const GravityEngine& e) const;
    void drawTrails(const GravityEngine& e) const;
    void drawBodies(const GravityEngine& e, int selectedId, int hoveredId) const;
    void drawEffects(const GravityEngine& e) const;
    void drawReticle(const Body& b) const;
    void drawVectors(const GravityEngine& e) const;
    void drawSlingshot(const GravityEngine& e, const SlingshotView& s) const;

    // Sidebar tabs (Sidebar.cpp); each returns the y below its content.
    float drawInspectorTab(GravityEngine& e, Body* sel, int hoveredId, float x, float y, float w, UiActions& a);
    float drawSpawnerTab(const GravityEngine& e, LaunchSettings& launch, float x, float y, float w);
    float drawWorldTab(GravityEngine& e, float x, float y, float w, UiActions& a);
    float drawSceneButtons(const GravityEngine& e, float x, float y, float w, UiActions& a);

    float sidebarAnim_{1.0f};
    float scrollTarget_{0.0f};
    float scroll_{0.0f};
    float contentHeight_{0.0f};
    float clearConfirm_{0.0f};

    bool camAnimating_{false};
    Vector2 goalTarget_{};
    float goalZoom_{1.0f};

    std::vector<Toast> toasts_;
    std::vector<Star> stars_;
};
