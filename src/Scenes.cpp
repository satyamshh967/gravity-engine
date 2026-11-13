// Preset scene builders. Every scene starts from the same clean state
// (resetSceneState) so settings never leak from one scene to the next.

#include "GravityEngine.hpp"

#include <cmath>
#include <numbers>
#include <random>

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}

const char* GravityEngine::sceneName(SceneId id) {
    switch (id) {
        case SceneId::Empty: return "Empty Space";
        case SceneId::SolarSystem: return "Solar System";
        case SceneId::FigureEight: return "Figure-8 Three-Body";
        case SceneId::BinaryDisk: return "Binary Star + Disk";
        case SceneId::EarthRoom: return "Earth Gravity Lab";
        case SceneId::TwoBodyTest: return "Two-Body Test";
    }
    return "Unknown";
}

void GravityEngine::resetSceneState() {
    bodies.clear();
    effects.clear();
    events.clear();
    roomBounds = RoomBounds{};
    enableUniformGravity = false;
    uniformGravity = Vec2{0.0, 720.0};
    disableMutualGravityInRoom = true;
    G = 1.0;
    epsilon = 5.0;
    integrator = IntegratorType::VelocityVerlet;
    collisionMode = CollisionMode::InelasticMerge;
    restitution = 1.0;
    shatterEnergyFactor = 1.0;
    idIndexDirty_ = true;
}

void GravityEngine::finalizeScene(SceneId id, SceneView view) {
    scene_ = id;
    sceneView_ = view;
    idIndexDirty_ = true;
    computeAccelerations();
    markEnergyBaseline();
}

void GravityEngine::loadScene(SceneId id) {
    resetSceneState();
    switch (id) {
        case SceneId::Empty: finalizeScene(id, SceneView{{0, 0}, {500, 400}}); break;
        case SceneId::SolarSystem: buildSolarSystem(); break;
        case SceneId::FigureEight: buildFigureEight(); break;
        case SceneId::BinaryDisk: buildBinaryDisk(); break;
        case SceneId::EarthRoom: buildEarthRoom(); break;
        case SceneId::TwoBodyTest: buildTwoBodyTest(); break;
    }
}

void GravityEngine::zeroTotalMomentum() {
    // Put the barycentre at rest at the origin so the system does not drift.
    const Vec2 p = totalMomentum();
    const Vec2 com = centerOfMass();
    double m = 0.0;
    for (const auto& b : bodies) {
        if (!b.isStatic) m += b.mass;
    }
    if (m <= 0.0) return;
    const Vec2 drift = p / m;
    for (auto& b : bodies) {
        if (b.isStatic) continue;
        b.vel -= drift;
        b.pos -= com;
        for (auto& t : b.trail) t -= com;
    }
}

// Planet masses and spacings are chosen so that
//  * neighbouring planets are > 3.5 mutual Hill radii apart (long-term stable), and
//  * every moon orbits inside ~0.45 of its planet's Hill radius (stays bound).
// Orbital speeds use the softened circular speed, so orbits start truly circular.
void GravityEngine::buildSolarSystem() {
    epsilon = 1.0;
    const double sunMass = 200000.0;

    Body sun(allocateId(), "Sun", sunMass, 24.0, Vec2{}, Vec2{}, Rgba{255, 210, 30, 255}, false);
    sun.maxTrailPoints = 60;
    bodies.push_back(sun);

    auto addPlanet = [&](const char* name, double dist, double mass, double radius, Rgba color,
                         std::size_t trail) -> Body& {
        const double v = circularSpeed(sunMass + mass, dist);
        Body b(allocateId(), name, mass, radius, Vec2{dist, 0.0}, Vec2{0.0, v}, color, false);
        b.maxTrailPoints = trail;
        bodies.push_back(b);
        return bodies.back();
    };
    auto addMoon = [&](const Body& planet, const char* name, double dist, double mass, double radius,
                       Rgba color, double side) {
        const double v = circularSpeed(planet.mass + mass, dist);
        Body m(allocateId(), name, mass, radius, planet.pos + Vec2{side * dist, 0.0},
               planet.vel + Vec2{0.0, side * v}, color, false);
        m.maxTrailPoints = 160;
        bodies.push_back(m);
    };

    addPlanet("Mercury", 70.0, 1.2, 3.0, Rgba{190, 180, 170, 255}, 220);
    addPlanet("Venus", 110.0, 5.0, 4.5, Rgba{235, 195, 140, 255}, 280);

    const Body earth = addPlanet("Earth", 175.0, 1000.0, 5.0, Rgba{70, 150, 255, 255}, 340);
    addMoon(earth, "Moon", 8.5, 0.3, 1.5, Rgba{210, 215, 220, 255}, 1.0);

    // Mars mass 150 gives a Hill radius of ~17 px, so Phobos at 6 px sits at ~0.35 R_H
    // (at 60 / 0.44 R_H solar tides pumped its eccentricity until it hit Mars).
    const Body mars = addPlanet("Mars", 275.0, 150.0, 3.5, Rgba{240, 100, 60, 255}, 360);
    addMoon(mars, "Phobos", 6.0, 0.02, 1.0, Rgba{180, 160, 150, 255}, 1.0);

    const Body jupiter = addPlanet("Jupiter", 455.0, 1500.0, 11.0, Rgba{225, 175, 120, 255}, 420);
    addMoon(jupiter, "Io", 20.0, 0.3, 2.0, Rgba{255, 235, 90, 255}, 1.0);
    addMoon(jupiter, "Europa", 27.0, 0.25, 1.8, Rgba{200, 230, 255, 255}, -1.0);

    addPlanet("Saturn", 780.0, 500.0, 10.0, Rgba{240, 220, 160, 255}, 480);

    // Halley-style comet on a highly eccentric orbit (vis-viva at perihelion)
    const double perihelion = 55.0;
    const double aphelion = 650.0;
    const double a = 0.5 * (perihelion + aphelion);
    const double vPeri = std::sqrt(G * sunMass * (2.0 / perihelion - 1.0 / a));
    Body comet(allocateId(), "Comet Halley", 0.1, 2.0, Vec2{-perihelion, 0.0}, Vec2{0.0, -vPeri},
               Rgba{130, 245, 255, 255}, false);
    comet.maxTrailPoints = 500;
    bodies.push_back(comet);

    zeroTotalMomentum();
    finalizeScene(SceneId::SolarSystem, SceneView{{0, 0}, {820, 820}});
}

void GravityEngine::buildFigureEight() {
    epsilon = 0.01;
    // Chenciner & Montgomery choreography (G = m = 1), scaled by length L and mass M.
    // Period = 6.326 * L / sqrt(G M / L): with L = 220 and M = 6e5 one loop takes
    // ~27 s (the old M = 500 took ~20 minutes per loop).
    const double L = 220.0;
    const double mass = 600000.0;
    const double V = std::sqrt(G * mass / L);
    const double x1 = -0.97000436, y1 = 0.24308753;
    const double vx3 = -0.93240737, vy3 = -0.86473146;

    auto make = [&](const char* name, Vec2 p, Vec2 v, Rgba c) {
        Body b(allocateId(), name, mass, 8.5, p * L, v * V, c, false);
        b.maxTrailPoints = 600;
        bodies.push_back(b);
    };
    make("Alpha", Vec2{x1, y1}, Vec2{-vx3 / 2.0, -vy3 / 2.0}, Rgba{0, 245, 255, 255});
    make("Beta", Vec2{0.0, 0.0}, Vec2{vx3, vy3}, Rgba{255, 40, 160, 255});
    make("Gamma", Vec2{-x1, -y1}, Vec2{-vx3 / 2.0, -vy3 / 2.0}, Rgba{255, 240, 30, 255});

    finalizeScene(SceneId::FigureEight, SceneView{{0, 0}, {280, 280}});
}

void GravityEngine::buildBinaryDisk() {
    epsilon = 4.0;
    const double starMass = 40000.0;
    const double sep = 120.0;

    // Each star orbits the barycentre at radius sep/2 under the softened mutual force.
    const double d2 = sep * sep + epsilon * epsilon;
    const double starSpeed = std::sqrt(0.5 * sep * G * starMass * sep / (d2 * std::sqrt(d2)));

    Body a(allocateId(), "Star A", starMass, 14.0, Vec2{-sep / 2, 0.0}, Vec2{0.0, -starSpeed},
           Rgba{255, 110, 40, 255}, false);
    a.maxTrailPoints = 300;
    Body b(allocateId(), "Star B", starMass, 12.0, Vec2{sep / 2, 0.0}, Vec2{0.0, starSpeed},
           Rgba{80, 190, 255, 255}, false);
    b.maxTrailPoints = 300;
    bodies.push_back(a);
    bodies.push_back(b);

    // Circumbinary orbits are only stable beyond ~2.5-3x the binary separation,
    // so the disk starts at 3x.
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> distU(0.0, 1.0);
    std::uniform_real_distribution<double> distAngle(0.0, kTwoPi);
    std::uniform_real_distribution<double> distMass(0.05, 0.25);

    const double rMin = 3.0 * sep;
    const double rMax = 760.0;
    for (int i = 0; i < 220; ++i) {
        const double r = std::sqrt(distU(rng) * (rMax * rMax - rMin * rMin) + rMin * rMin);
        const double theta = distAngle(rng);
        const Vec2 pos{r * std::cos(theta), r * std::sin(theta)};
        const double speed = circularSpeed(2.0 * starMass, r);
        const Vec2 vel{-speed * std::sin(theta), speed * std::cos(theta)};

        const double t = (r - rMin) / (rMax - rMin);
        const Rgba color{static_cast<unsigned char>(255 * (1.0 - 0.6 * t)), static_cast<unsigned char>(200 + 40 * t),
                         static_cast<unsigned char>(120 + 130 * t), 230};
        const int id = allocateId();
        Body p(id, "Dust #" + std::to_string(id), distMass(rng), 2.0, pos, vel, color, false);
        p.isDebris = true;
        p.maxTrailPoints = 35;
        bodies.push_back(p);
    }

    zeroTotalMomentum();
    finalizeScene(SceneId::BinaryDisk, SceneView{{0, 0}, {800, 800}});
}

void GravityEngine::buildEarthRoom() {
    collisionMode = CollisionMode::Elastic;
    roomBounds.enabled = true;
    enableUniformGravity = true;
    uniformGravity = Vec2{0.0, 720.0}; // = 9.81 m/s^2 at roomBounds.pixelsPerMeter
    disableMutualGravityInRoom = true;

    auto add = [&](const char* name, double mass, double radius, Vec2 pos, Vec2 vel, Rgba c) {
        Body b(allocateId(), name, mass, radius, pos, vel, c, false);
        b.maxTrailPoints = 300;
        bodies.push_back(b);
    };

    // Galileo pair dropped from the same height: they fall together regardless
    // of mass, and the drop is above the critical shatter height, so both shatter.
    add("Heavy Anvil (500 kg)", 500.0, 18.0, Vec2{-180.0, -220.0}, Vec2{}, Rgba{240, 175, 45, 255});
    add("Light Apple (0.5 kg)", 0.5, 8.0, Vec2{-100.0, -220.0}, Vec2{}, Rgba{255, 60, 60, 255});
    // Below the critical height: these bounce.
    add("Rubber Ball (2 kg)", 2.0, 11.0, Vec2{90.0, 130.0}, Vec2{}, Rgba{0, 235, 255, 255});
    add("Tennis Ball (1 kg)", 1.0, 9.0, Vec2{0.0, 90.0}, Vec2{140.0, -60.0}, Rgba{180, 255, 40, 255});
    add("Bowling Ball (40 kg)", 40.0, 15.0, Vec2{220.0, 110.0}, Vec2{}, Rgba{130, 150, 190, 255});

    finalizeScene(SceneId::EarthRoom, SceneView{{0, -25}, {500, 395}});
}

void GravityEngine::buildTwoBodyTest() {
    epsilon = 0.0;
    Body star(allocateId(), "Central", 10000.0, 12.0, Vec2{}, Vec2{}, Rgba{255, 220, 0, 255}, true);
    const double r = 200.0;
    const double v = std::sqrt(G * star.mass / r);
    Body planet(allocateId(), "TestPlanet", 1.0, 4.0, Vec2{r, 0.0}, Vec2{0.0, v}, Rgba{0, 200, 255, 255}, false);
    bodies.push_back(star);
    bodies.push_back(planet);
    finalizeScene(SceneId::TwoBodyTest, SceneView{{0, 0}, {260, 260}});
}
