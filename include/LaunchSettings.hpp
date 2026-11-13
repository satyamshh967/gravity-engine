#pragma once

#include "Body.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

enum class LaunchPresetType {
    Asteroid,
    Moon,
    Terrestrial,
    GasGiant,
    Star,
    BlackHole,
    HeavyAnvil,
    LightApple,
    RubberBall,
    Custom
};

enum class LaunchSpawnMode {
    Slingshot,     // drag to pull back the velocity vector
    CircularOrbit, // click: circular orbit around the local dominant body (in the room: drop)
    Stationary     // click: zero velocity
};

struct LaunchPreset {
    LaunchPresetType type;
    const char* name;
    double mass;
    double radius;
    Rgba color;
    std::size_t trail;
};

inline constexpr LaunchPreset kLaunchPresets[] = {
    {LaunchPresetType::Asteroid, "Asteroid", 0.5, 2.5, Rgba{180, 180, 180, 255}, 200},
    {LaunchPresetType::Moon, "Moon", 6.0, 3.8, Rgba{210, 215, 220, 255}, 250},
    {LaunchPresetType::Terrestrial, "Planet", 80.0, 6.5, Rgba{70, 160, 255, 255}, 350},
    {LaunchPresetType::GasGiant, "Gas Giant", 650.0, 13.0, Rgba{235, 175, 110, 255}, 400},
    {LaunchPresetType::Star, "Star", 20000.0, 22.0, Rgba{255, 215, 30, 255}, 450},
    {LaunchPresetType::BlackHole, "Singularity", 80000.0, 8.0, Rgba{220, 50, 255, 255}, 500},
    {LaunchPresetType::HeavyAnvil, "Heavy Anvil", 500.0, 18.0, Rgba{240, 175, 45, 255}, 300},
    {LaunchPresetType::LightApple, "Light Apple", 0.5, 8.0, Rgba{255, 60, 60, 255}, 300},
    {LaunchPresetType::RubberBall, "Rubber Ball", 2.0, 11.0, Rgba{0, 235, 255, 255}, 300},
};

[[nodiscard]] inline const LaunchPreset* findLaunchPreset(LaunchPresetType t) {
    for (const auto& p : kLaunchPresets) {
        if (p.type == t) return &p;
    }
    return nullptr;
}

struct LaunchSettings {
    LaunchPresetType presetType{LaunchPresetType::Terrestrial};
    LaunchSpawnMode spawnMode{LaunchSpawnMode::Slingshot};
    double mass{80.0};
    double radius{6.5};
    double density{0.60};
    Rgba color{70, 160, 255, 255};
    bool isStatic{false};
    int predictionSteps{250}; // at 0.01 s per step
    std::size_t trailLength{350};
    std::string typeName{"Planet"};

    LaunchSettings() { applyPreset(LaunchPresetType::Terrestrial); }

    void applyPreset(LaunchPresetType type) {
        const LaunchPreset* p = findLaunchPreset(type);
        if (!p) return;
        presetType = type;
        mass = p->mass;
        radius = p->radius;
        color = p->color;
        trailLength = p->trail;
        typeName = p->name;
        isStatic = false;
        recalculateDensityFromRadius();
    }

    // All edits clamp to the shared limits and mark the payload as Custom.
    void setMass(double m) {
        mass = std::clamp(m, limits::kMinMass, limits::kMaxMass);
        recalculateRadiusFromDensity();
        presetType = LaunchPresetType::Custom;
    }
    void scaleMass(double f) { setMass(mass * f); }

    void setRadius(double r) {
        radius = std::clamp(r, limits::kMinRadius, limits::kMaxRadius);
        recalculateDensityFromRadius();
        presetType = LaunchPresetType::Custom;
    }

    void setDensity(double d) {
        density = std::clamp(d, limits::kMinDensity, limits::kMaxDensity);
        recalculateRadiusFromDensity();
        presetType = LaunchPresetType::Custom;
    }

    void recalculateRadiusFromDensity() {
        if (density > 0.0) {
            radius = std::clamp(std::sqrt(mass / (std::numbers::pi * density)), limits::kMinRadius, limits::kMaxRadius);
        }
        density = mass / (std::numbers::pi * radius * radius); // stay consistent after clamping
    }

    void recalculateDensityFromRadius() {
        density = mass / (std::numbers::pi * radius * radius);
    }
};
