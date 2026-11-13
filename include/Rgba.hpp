#pragma once

#include <algorithm>

// Graphics-library-independent color used by the physics core.
// The renderer converts it to raylib's Color.
struct Rgba {
    unsigned char r{255};
    unsigned char g{255};
    unsigned char b{255};
    unsigned char a{255};

    [[nodiscard]] constexpr bool sameRgb(const Rgba& o) const noexcept {
        return r == o.r && g == o.g && b == o.b;
    }
};

[[nodiscard]] inline Rgba lerpColor(Rgba c1, Rgba c2, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto mix = [t](unsigned char p, unsigned char q) {
        return static_cast<unsigned char>(std::clamp(p + (q - p) * t, 0.0, 255.0));
    };
    return Rgba{mix(c1.r, c2.r), mix(c1.g, c2.g), mix(c1.b, c2.b), mix(c1.a, c2.a)};
}

// Mass-weighted blend used when bodies merge.
[[nodiscard]] inline Rgba weightedColor(Rgba c1, double w1, Rgba c2, double w2) {
    const double total = w1 + w2;
    if (total <= 0.0) return c1;
    return lerpColor(c1, c2, w2 / total);
}
