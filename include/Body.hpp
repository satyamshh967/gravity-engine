#pragma once

#include "Rgba.hpp"
#include "Vec2.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <string>

// Single source of truth for size/mass limits (spawner, inspector and merges).
namespace limits {
inline constexpr double kMinMass = 0.01;
inline constexpr double kMaxMass = 1.0e6;
inline constexpr double kMinRadius = 1.0;
inline constexpr double kMaxRadius = 140.0;
inline constexpr double kMinDensity = 1.0e-4;
inline constexpr double kMaxDensity = 1.0e4;
} // namespace limits

struct Body {
    int id{0};
    std::string name{"Body"};
    double mass{1.0};
    double radius{5.0};
    Vec2 pos{0.0, 0.0};
    Vec2 vel{0.0, 0.0};
    Vec2 acc{0.0, 0.0};
    Rgba color{};
    bool isStatic{false};
    bool isDebris{false}; // fragments / disk particles: no name labels, smaller trails

    // Orbital trail history
    std::deque<Vec2> trail;
    std::size_t maxTrailPoints{350};
    double minTrailDistSq{4.0}; // avoids clumping of trail points

    Body() = default;

    Body(int id_, std::string name_, double mass_, double radius_,
         Vec2 pos_, Vec2 vel_, Rgba color_, bool isStatic_ = false)
        : id(id_),
          name(std::move(name_)),
          mass(mass_),
          radius(radius_),
          pos(pos_),
          vel(isStatic_ ? Vec2{} : vel_),
          color(color_),
          isStatic(isStatic_) {}

    void addTrailPoint(const Vec2& p) {
        if (maxTrailPoints == 0) return;
        if (!trail.empty() && trail.back().distanceSq(p) < minTrailDistSq) return;
        trail.push_back(p);
        while (trail.size() > maxTrailPoints) trail.pop_front();
    }

    void clearTrail() { trail.clear(); }

    // 2D "density": mass per disk area.
    [[nodiscard]] double density() const {
        return radius > 1e-9 ? mass / (std::numbers::pi * radius * radius) : 0.0;
    }

    void setRadius(double r) { radius = std::clamp(r, limits::kMinRadius, limits::kMaxRadius); }

    void setDensity(double d) {
        d = std::clamp(d, limits::kMinDensity, limits::kMaxDensity);
        setRadius(std::sqrt(mass / (std::numbers::pi * d)));
    }

    // Changes mass while keeping density (radius follows, within limits).
    void setMass(double m) {
        const double d = density();
        mass = std::clamp(m, limits::kMinMass, limits::kMaxMass);
        if (d > 0.0) setDensity(d);
    }

    void setStatic(bool s) {
        isStatic = s;
        if (s) vel = Vec2{};
    }
};
