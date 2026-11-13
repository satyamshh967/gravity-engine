#include "PhysicsTests.hpp"

#include "GravityEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
}

bool near(double a, double b, double relTol, double absTol = 1e-9) {
    return std::abs(a - b) <= std::max(absTol, relTol * std::max(std::abs(a), std::abs(b)));
}

bool nearVec(Vec2 a, Vec2 b, double relTol, double absTol = 1e-9) {
    return near(a.x, b.x, relTol, absTol) && near(a.y, b.y, relTol, absTol);
}

std::string fmt(const char* f, double v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

GravityEngine emptyEngine() {
    GravityEngine e;
    e.seedRandom(1234);
    e.loadScene(SceneId::Empty);
    return e;
}

void testTwoBodyFullOrbit() {
    std::puts("Two-body circular orbit, one full period (Velocity Verlet vs Symplectic Euler)");
    for (IntegratorType integ : {IntegratorType::VelocityVerlet, IntegratorType::SymplecticEuler}) {
        GravityEngine e;
        e.loadScene(SceneId::TwoBodyTest);
        e.integrator = integ;
        const Body* planet = &e.bodies[1];
        const double r0 = planet->pos.length();
        const double period = 2.0 * std::numbers::pi * r0 / planet->vel.length();
        const int steps = static_cast<int>(period / e.fixedDt);

        double minR = r0, maxR = r0;
        for (int s = 0; s < steps; ++s) {
            e.step(e.fixedDt);
            const double r = e.bodies[1].pos.length();
            minR = std::min(minR, r);
            maxR = std::max(maxR, r);
        }
        const double drift = e.energyReport().driftPercent;
        const bool verlet = integ == IntegratorType::VelocityVerlet;
        const char* name = verlet ? "Verlet" : "Euler";
        if (verlet) {
            check(drift < 0.01, std::string(name) + fmt(" energy drift over a full orbit < 0.01%% (got %.2e%%)", drift));
            check((maxR - minR) / r0 < 1e-3, std::string(name) + fmt(" orbit radius stays within 0.1%% (spread %.2e)", (maxR - minR) / r0));
        } else {
            check(drift < 1.0, std::string(name) + fmt(" stays bounded (drift %.3f%%)", drift));
        }
        check(nearVec(e.bodies[1].pos, Vec2{r0, 0.0}, 0.0, r0 * 0.01),
              std::string(name) + " returns to its starting point after one period");
    }
}

void testMergeConservation() {
    std::puts("Inelastic merge conserves mass, momentum and centre of mass");
    GravityEngine e = emptyEngine();
    e.G = 0.0; // isolate the collision
    e.collisionMode = CollisionMode::InelasticMerge;
    e.addBody(Body(0, "A", 15.0, 5.0, Vec2{-10.0, 0.0}, Vec2{20.0, 5.0}, Rgba{}));
    e.addBody(Body(0, "B", 25.0, 8.0, Vec2{10.0, 0.0}, Vec2{-10.0, 2.0}, Rgba{}));
    const Vec2 p0 = e.totalMomentum();
    const double m0 = e.totalMass();
    for (int s = 0; s < 500 && e.getBodyCount() > 1; ++s) e.step(e.fixedDt);
    check(e.getBodyCount() == 1, "bodies merged into one");
    check(near(e.totalMass(), m0, 1e-12), "mass conserved");
    check(nearVec(e.totalMomentum(), p0, 1e-12), "momentum conserved");
    check(e.bodies[0].name == "B", "heavier body keeps its identity");
    bool sawMerge = false;
    for (const auto& ev : e.events) sawMerge |= ev.type == SimEventType::Merge;
    check(sawMerge, "merge event emitted");
}

void testShatterCollision() {
    std::puts("Shatter mode: violent impacts fragment and conserve mass/momentum/COM");
    GravityEngine e = emptyEngine();
    e.collisionMode = CollisionMode::DestructiveShatter;
    e.addBody(Body(0, "Planet A", 80.0, 6.5, Vec2{-6.4, 0.0}, Vec2{60.0, 0.0}, Rgba{200, 50, 50, 255}));
    e.addBody(Body(0, "Planet B", 80.0, 6.5, Vec2{6.4, 0.0}, Vec2{-60.0, 0.0}, Rgba{50, 50, 200, 255}));
    const Vec2 p0 = e.totalMomentum();
    const double m0 = e.totalMass();
    e.step(e.fixedDt);
    check(e.getBodyCount() > 2, "head-on impact produced fragments (" + std::to_string(e.getBodyCount()) + " bodies)");
    check(near(e.totalMass(), m0, 1e-12), "mass conserved");
    check(nearVec(e.totalMomentum(), p0, 1e-9, 1e-6), "momentum conserved");
    bool sawShatter = false;
    for (const auto& ev : e.events) sawShatter |= ev.type == SimEventType::Shatter;
    check(sawShatter, "shatter event emitted");

    // A gentle touch in shatter mode should merge, not shatter.
    GravityEngine g = emptyEngine();
    g.collisionMode = CollisionMode::DestructiveShatter;
    g.addBody(Body(0, "Rock", 0.5, 2.5, Vec2{-9.0, 0.0}, Vec2{2.0, 0.0}, Rgba{}));
    g.addBody(Body(0, "Planet", 80.0, 6.5, Vec2{0.0, 0.0}, Vec2{}, Rgba{}));
    for (int s = 0; s < 200 && g.getBodyCount() > 1; ++s) g.step(g.fixedDt);
    check(g.getBodyCount() == 1, "low-energy impact merges instead of shattering");
}

void testExplosionConservation() {
    std::puts("Manual explosion conserves mass, momentum and centre of mass");
    GravityEngine e = emptyEngine();
    const int id = e.addBody(Body(0, "Bomb", 120.0, 8.0, Vec2{30.0, -20.0}, Vec2{4.0, -3.0}, Rgba{}));
    e.addBody(Body(0, "Bystander", 5.0, 3.0, Vec2{500.0, 0.0}, Vec2{0.0, 1.0}, Rgba{}));
    const Vec2 p0 = e.totalMomentum();
    const Vec2 c0 = e.centerOfMass();
    const double m0 = e.totalMass();
    check(e.explodeBody(id), "explodeBody succeeded");
    check(e.getBodyById(id) == nullptr, "original body removed");
    check(e.getBodyCount() >= 4, "fragments created");
    check(near(e.totalMass(), m0, 1e-12), "mass conserved");
    check(nearVec(e.totalMomentum(), p0, 1e-9, 1e-9), "momentum conserved");
    check(nearVec(e.centerOfMass(), c0, 1e-9, 1e-9), "centre of mass conserved");
}

void testRoomBounce() {
    std::puts("Earth room: walls only reflect bodies moving into them");
    GravityEngine e;
    e.loadScene(SceneId::EarthRoom);
    e.bodies.clear();
    const double floorY = e.roomBounds.yMax;
    // Overlapping the floor but already moving up: must keep moving up.
    const int id = e.addBody(Body(0, "Leaving", 1.0, 10.0, Vec2{0.0, floorY - 9.0}, Vec2{0.0, -100.0}, Rgba{}));
    e.step(e.fixedDt);
    const Body* b = e.getBodyById(id);
    check(b && b->vel.y < -95.0, fmt("upward velocity preserved (vy = %.2f)", b ? b->vel.y : 0.0));

    // Moving into the floor slowly: bounces with restitution.
    GravityEngine f;
    f.loadScene(SceneId::EarthRoom);
    f.bodies.clear();
    const int id2 = f.addBody(Body(0, "Ball", 1.0, 10.0, Vec2{0.0, floorY - 10.5}, Vec2{0.0, 300.0}, Rgba{}));
    for (int s = 0; s < 5; ++s) f.step(f.fixedDt);
    const Body* ball = f.getBodyById(id2);
    check(ball && ball->vel.y < -200.0, fmt("downward ball bounced (vy = %.1f)", ball ? ball->vel.y : 0.0));
}

void testGalileoAndFloorShatter() {
    std::puts("Earth room: Galileo free fall and critical-height shatter");
    GravityEngine e;
    e.seedRandom(7);
    e.loadScene(SceneId::EarthRoom);
    const int anvil = e.bodies[0].id;
    const int apple = e.bodies[1].id;
    for (int s = 0; s < 150; ++s) e.step(e.fixedDt); // 0.3 s
    const Body* a = e.getBodyById(anvil);
    const Body* p = e.getBodyById(apple);
    check(a && p && near(a->pos.y, p->pos.y, 1e-12) && near(a->vel.y, p->vel.y, 1e-12),
          "500 kg anvil and 0.5 kg apple fall identically");

    for (int s = 0; s < 1500; ++s) e.step(e.fixedDt); // +3 s
    check(e.getBodyById(anvil) == nullptr && e.getBodyById(apple) == nullptr,
          "both shattered when hitting the floor from above the critical height");
    bool inside = true;
    for (const auto& b : e.bodies) {
        inside &= b.pos.x - b.radius >= e.roomBounds.xMin - 1e-6 && b.pos.x + b.radius <= e.roomBounds.xMax + 1e-6 &&
                  b.pos.y - b.radius >= e.roomBounds.yMin - 1e-6 && b.pos.y + b.radius <= e.roomBounds.yMax + 1e-6;
    }
    check(inside, "all debris stays inside the room");
    bool ballSurvived = false;
    for (const auto& b : e.bodies) ballSurvived |= b.name.rfind("Rubber Ball", 0) == 0;
    check(ballSurvived, "low drop (rubber ball) bounced without shattering");
}

void testEnergyBaseline() {
    std::puts("Energy drift baseline resets on discontinuous changes");
    GravityEngine e;
    e.loadScene(SceneId::SolarSystem);
    for (int s = 0; s < 200; ++s) e.step(e.fixedDt);
    e.addBody(Body(0, "Rogue", 5000.0, 9.0, Vec2{300.0, 300.0}, Vec2{}, Rgba{}));
    check(e.energyReport().driftPercent < 1e-9, "drift is 0 right after spawning a body");
    e.clearBodies();
    e.addBody(Body(0, "A", 10.0, 3.0, Vec2{0.0, 0.0}, Vec2{}, Rgba{}));
    e.addBody(Body(0, "B", 1.0, 2.0, Vec2{100.0, 0.0}, Vec2{0.0, 0.3}, Rgba{}));
    for (int s = 0; s < 200; ++s) e.step(e.fixedDt);
    const double drift = e.energyReport().driftPercent;
    check(std::isfinite(drift) && drift < 0.01, fmt("drift tracked after clear + respawn (%.2e%%)", drift));
}

void testSolarSystemStability() {
    std::puts("Solar system: moons stay bound, planets survive 40 s");
    GravityEngine e;
    e.loadScene(SceneId::SolarSystem);
    const std::size_t n0 = e.getBodyCount();
    const Vec2 p0 = e.totalMomentum();
    for (int s = 0; s < 20000; ++s) e.step(e.fixedDt);
    check(e.getBodyCount() == n0, "no collisions or merges (" + std::to_string(e.getBodyCount()) + "/" + std::to_string(n0) + " bodies)");

    auto boundTo = [&](const char* moon, const char* planet) {
        const Body* m = nullptr;
        for (const auto& b : e.bodies) {
            if (b.name == moon) m = &b;
        }
        if (!m) return false;
        const OrbitInfo info = e.orbitInfo(m->id);
        return info.valid && info.bound && info.attractorName == planet;
    };
    check(boundTo("Moon", "Earth"), "Moon still bound to Earth");
    check(boundTo("Phobos", "Mars"), "Phobos still bound to Mars");
    check(boundTo("Io", "Jupiter"), "Io still bound to Jupiter");
    check(boundTo("Europa", "Jupiter"), "Europa still bound to Jupiter");
    check(nearVec(e.totalMomentum(), p0, 1e-9, 1e-4), "total momentum conserved (Sun is dynamic now)");
    const double drift = e.energyReport().driftPercent;
    check(drift < 0.05, fmt("energy drift < 0.05%% (got %.2e%%)", drift));
}

void testFigureEight() {
    std::puts("Figure-8 choreography stays intact");
    GravityEngine e;
    e.loadScene(SceneId::FigureEight);
    for (int s = 0; s < 5000; ++s) e.step(e.fixedDt);
    check(e.getBodyCount() == 3, "no merges after 10 s");
    check(e.totalMomentum().length() < 1e-3, "zero net momentum");
    const double drift = e.energyReport().driftPercent;
    check(drift < 0.1, fmt("energy drift < 0.1%% (got %.2e%%)", drift));
    // Choreography: the middle body (Beta) returns close to the centre every half period.
    double minCentre = 1e9;
    for (int s = 0; s < 4000; ++s) {
        e.step(e.fixedDt);
        minCentre = std::min(minCentre, e.bodies[1].pos.length());
    }
    check(minCentre < 15.0, fmt("Beta passes through the centre (closest %.2f px)", minCentre));
}

void testTrajectoryPrediction() {
    std::puts("Trajectory preview matches the real simulation");
    GravityEngine e;
    e.loadScene(SceneId::TwoBodyTest);
    const Vec2 start{0.0, -150.0};
    const Vec2 vel{8.0, 0.0};
    const auto pred = e.predictTrajectory(start, vel, 1.0, 200, 0.01);
    e.addBody(Body(0, "Probe", 1e-6, 1.0, start, vel, Rgba{}));
    const int probeId = e.bodies.back().id;
    for (int s = 0; s < 1000; ++s) e.step(e.fixedDt); // 2 s
    const Body* p = e.getBodyById(probeId);
    const double err = p ? p->pos.distance(pred.endPos) : 1e9;
    check(!pred.hitsBody && err < 2.0, fmt("predicted position within 2 px after 2 s (error %.3f px)", err));
}

void testIdsUnique() {
    std::puts("Body ids are never reused across scenes");
    GravityEngine e;
    e.loadScene(SceneId::FigureEight);
    const int oldId = e.bodies[0].id;
    e.loadScene(SceneId::SolarSystem);
    check(e.getBodyById(oldId) == nullptr, "a stale selection id cannot alias a new body");
}

} // namespace

int runPhysicsTests() {
    g_failures = 0;
    g_checks = 0;
    std::puts("==========================================================");
    std::puts("  GRAVITY ENGINE - HEADLESS PHYSICS TEST SUITE");
    std::puts("==========================================================");
    testTwoBodyFullOrbit();
    testMergeConservation();
    testShatterCollision();
    testExplosionConservation();
    testRoomBounce();
    testGalileoAndFloorShatter();
    testEnergyBaseline();
    testSolarSystemStability();
    testFigureEight();
    testTrajectoryPrediction();
    testIdsUnique();
    std::puts("----------------------------------------------------------");
    std::printf("  %d/%d checks passed%s\n", g_checks - g_failures, g_checks, g_failures ? "  <-- FAILURES" : "");
    std::puts("==========================================================");
    return g_failures;
}
