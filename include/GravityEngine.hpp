#pragma once

#include "Body.hpp"
#include "Effects.hpp"

#include <cstddef>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// Axis-aligned laboratory chamber with a uniform downward gravity field.
struct RoomBounds {
    bool enabled{false};
    double xMin{-460.0};
    double xMax{460.0};
    double yMin{-340.0}; // ceiling
    double yMax{290.0};  // floor
    double floorRestitution{0.82};
    double wallRestitution{0.85};
    double ceilingRestitution{0.85};
    double shatterVelocity{680.0}; // impact speed (px/s) at which bodies shatter
    double minShatterMass{0.1};    // lighter bodies just bounce
    // World scale for the room: chosen so that g = 720 px/s^2 is exactly 9.81 m/s^2.
    double pixelsPerMeter{720.0 / 9.81};

    [[nodiscard]] double criticalDropHeight(double g) const {
        return g > 1e-9 ? (shatterVelocity * shatterVelocity) / (2.0 * g) : 0.0;
    }
};

enum class IntegratorType { VelocityVerlet, SymplecticEuler };

enum class CollisionMode { None, Elastic, InelasticMerge, DestructiveShatter };

enum class SceneId { Empty, SolarSystem, FigureEight, BinaryDisk, EarthRoom, TwoBodyTest };

enum class SimEventType { Merge, Shatter, Explosion, FloorShatter };

// Notable things that happened during a step; the app turns them into toasts
// and uses survivor/absorbed ids to keep selection and camera lock on the
// right body after merges.
struct SimEvent {
    SimEventType type{SimEventType::Merge};
    std::string nameA;
    std::string nameB;
    int survivorId{-1};
    int absorbedId{-1};
    Vec2 pos{};
};

struct EnergyReport {
    double kinetic{0.0};
    double potential{0.0};
    double total{0.0};
    double driftPercent{0.0};
    bool driftMeaningful{true}; // false in the dissipative Earth room
};

struct OrbitInfo {
    bool valid{false};
    int attractorId{-1};
    std::string attractorName;
    bool bound{false};
    double eccentricity{0.0};
    double semiMajorAxis{0.0};
    double period{0.0};
};

struct TrajectoryPrediction {
    std::vector<Vec2> points;
    bool hitsBody{false};
    std::string hitName;
    bool wouldShatter{false};
    bool hasOrbitClass{false};
    bool bound{false};
    std::string attractorName;
    Vec2 endPos{};
};

// Region the camera should frame when a scene loads.
struct SceneView {
    Vec2 center{0.0, 0.0};
    Vec2 halfExtent{400.0, 300.0};
};

class GravityEngine {
public:
    // --- Tunable physics parameters ---
    double G{1.0};
    double epsilon{5.0}; // Plummer softening length
    IntegratorType integrator{IntegratorType::VelocityVerlet};
    CollisionMode collisionMode{CollisionMode::InelasticMerge};
    double fixedDt{0.002};
    double restitution{1.0};          // body-body restitution in Elastic mode
    double shatterEnergyFactor{1.0};  // impact energy / binding energy needed to shatter
    std::size_t maxBodies{900};

    // --- Earth room ---
    RoomBounds roomBounds;
    bool enableUniformGravity{false};
    Vec2 uniformGravity{0.0, 720.0};
    bool disableMutualGravityInRoom{true};

    std::vector<Body> bodies;
    Effects effects;
    std::vector<SimEvent> events; // drained by the app each frame

    GravityEngine();

    // --- Simulation ---
    void step(double dt);
    void updateEffects(double dt);
    void computeAccelerations();
    void seedRandom(unsigned seed) { rng_.seed(seed); }
    void spawnShockwave(Vec2 pos, float maxRadius, Rgba color, float speed = 180.0f);
    [[nodiscard]] double simulationTime() const { return simTime_; }

    // --- Scenes ---
    void loadScene(SceneId id);
    [[nodiscard]] SceneId currentScene() const { return scene_; }
    [[nodiscard]] SceneView sceneView() const { return sceneView_; }
    [[nodiscard]] static const char* sceneName(SceneId id);

    // --- Body management ---
    [[nodiscard]] int allocateId() { return nextId_++; }
    int addBody(Body b); // assigns an id when b.id == 0; returns the id
    void clearBodies();
    bool deleteBody(int id);
    bool explodeBody(int id); // shatter into fragments, conserving mass & momentum
    void circularizeOrbit(int bodyId);
    // Call after editing body properties (mass, radius, static flag) or G/epsilon.
    void notifyBodiesEdited();
    [[nodiscard]] std::size_t getBodyCount() const { return bodies.size(); }

    [[nodiscard]] Body* getBodyById(int id);
    [[nodiscard]] const Body* getBodyById(int id) const;
    [[nodiscard]] int findNearestBody(Vec2 worldPos, double pickPadding) const;

    // --- Queries ---
    [[nodiscard]] bool mutualGravityActive() const { return !enableUniformGravity || !disableMutualGravityInRoom; }
    [[nodiscard]] double circularSpeed(double centralMass, double r) const;
    [[nodiscard]] double surfaceGravity(const Body& b) const;
    [[nodiscard]] double escapeVelocity(const Body& b) const;
    [[nodiscard]] Vec2 computeCircularOrbitVelocity(Vec2 spawnPos, double spawnMass = 0.0, int excludeId = -1) const;
    [[nodiscard]] Vec2 computeNetAccelerationOnPoint(Vec2 pos) const;
    [[nodiscard]] OrbitInfo orbitInfo(int bodyId) const;
    [[nodiscard]] TrajectoryPrediction predictTrajectory(Vec2 startPos, Vec2 startVel, double probeRadius,
                                                         int steps, double dt) const;

    // --- Energy & conservation diagnostics ---
    [[nodiscard]] double computeKineticEnergy() const;
    [[nodiscard]] double computePotentialEnergy() const;
    [[nodiscard]] double computeTotalEnergy() const { return computeKineticEnergy() + computePotentialEnergy(); }
    [[nodiscard]] EnergyReport energyReport() const;
    // Energy drift is measured from this baseline. It is reset automatically on
    // every discontinuous change (spawn, delete, merge, shatter, edits, scene load).
    void markEnergyBaseline();
    [[nodiscard]] Vec2 totalMomentum() const;
    [[nodiscard]] double totalMass() const;
    [[nodiscard]] Vec2 centerOfMass() const;

    [[nodiscard]] const char* getIntegratorName() const;
    [[nodiscard]] const char* getCollisionModeName() const;

private:
    // Integrators
    void stepVelocityVerlet(double dt);
    void stepSymplecticEuler(double dt);

    // Collisions
    void buildSweepOrder();
    void handleCollisions();
    void resolveElasticPair(std::size_t i, std::size_t j);
    void mergePair(std::size_t i, std::size_t j);
    bool tryShatterPair(std::size_t i, std::size_t j);
    void handleRoomCollisions(double dt);

    // Fragmentation shared by collisions, explosions and floor impacts.
    // Equal-mass fragments on a symmetric ring with mean-free ejection velocities,
    // so total mass, momentum and centre of mass are conserved exactly.
    void spawnFragments(double totalMass, Vec2 center, Vec2 baseVel, double lumpRadius, Rgba color,
                        double ejectSpeed, int count, std::vector<Body>& out);
    [[nodiscard]] static int fragmentCountFor(double mass);
    [[nodiscard]] int dominantAttractorIndex(Vec2 pos, int excludeId, double probeMass) const;

    // Visual effects
    void spawnSparks(Vec2 pos, Vec2 dir, double energy, Rgba c1, Rgba c2, int count, float spread = 1.2f);
    void spawnEmbers(Vec2 pos, Vec2 baseVel, int count);

    // Scenes
    void resetSceneState();
    void finalizeScene(SceneId id, SceneView view);
    void buildSolarSystem();
    void buildFigureEight();
    void buildBinaryDisk();
    void buildEarthRoom();
    void buildTwoBodyTest();
    void zeroTotalMomentum();

    void markStructureChanged() { structureChanged_ = true; idIndexDirty_ = true; }
    void rebuildIdIndex() const;
    [[nodiscard]] std::ptrdiff_t indexOf(int id) const;

    std::mt19937 rng_;
    int nextId_{1};
    double simTime_{0.0};
    SceneId scene_{SceneId::Empty};
    SceneView sceneView_{};

    double baselineEnergy_{0.0};
    bool baselineValid_{false};

    bool structureChanged_{false};
    bool positionsCorrected_{false};

    // Scratch buffers reused every step (no per-step allocations)
    std::vector<Vec2> scratchAcc_;
    std::vector<std::size_t> sweepOrder_;
    std::vector<char> removed_;
    std::vector<Body> pendingBodies_;
    std::vector<std::size_t> attractors_;

    mutable std::unordered_map<int, std::size_t> idIndex_;
    mutable bool idIndexDirty_{true};
};
