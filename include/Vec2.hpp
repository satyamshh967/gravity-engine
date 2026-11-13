#pragma once

#include <cmath>

// Lightweight 2D double-precision vector. Deliberately free of any graphics
// dependency so the physics core can be built and tested without raylib.
struct Vec2 {
    double x{0.0};
    double y{0.0};

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }
    constexpr Vec2 operator-(const Vec2& rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }
    constexpr Vec2 operator*(double s) const noexcept { return {x * s, y * s}; }
    friend constexpr Vec2 operator*(double s, const Vec2& v) noexcept { return {v.x * s, v.y * s}; }
    // Plain IEEE division: dividing by zero yields inf/nan instead of silently
    // returning a zero vector and hiding the bug. Use normalized() when a safe
    // direction is needed.
    constexpr Vec2 operator/(double s) const noexcept { return {x / s, y / s}; }
    constexpr Vec2 operator-() const noexcept { return {-x, -y}; }

    constexpr Vec2& operator+=(const Vec2& rhs) noexcept { x += rhs.x; y += rhs.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& rhs) noexcept { x -= rhs.x; y -= rhs.y; return *this; }
    constexpr Vec2& operator*=(double s) noexcept { x *= s; y *= s; return *this; }
    constexpr Vec2& operator/=(double s) noexcept { x /= s; y /= s; return *this; }

    constexpr bool operator==(const Vec2&) const = default;

    [[nodiscard]] constexpr double dot(const Vec2& rhs) const noexcept { return x * rhs.x + y * rhs.y; }
    // z-component of the 3D cross product (signed parallelogram area).
    [[nodiscard]] constexpr double cross(const Vec2& rhs) const noexcept { return x * rhs.y - y * rhs.x; }
    [[nodiscard]] constexpr double lengthSq() const noexcept { return x * x + y * y; }
    [[nodiscard]] double length() const noexcept { return std::sqrt(lengthSq()); }
    [[nodiscard]] constexpr Vec2 perp() const noexcept { return {-y, x}; }

    // Unit vector, or (0,0) for a (near) zero-length vector.
    [[nodiscard]] Vec2 normalized() const noexcept {
        const double len = length();
        return (len > 1e-14) ? Vec2{x / len, y / len} : Vec2{0.0, 0.0};
    }

    [[nodiscard]] constexpr double distanceSq(const Vec2& rhs) const noexcept {
        const double dx = rhs.x - x;
        const double dy = rhs.y - y;
        return dx * dx + dy * dy;
    }
    [[nodiscard]] double distance(const Vec2& rhs) const noexcept { return std::sqrt(distanceSq(rhs)); }
};
