#pragma once

#include "Rgba.hpp"
#include "Vec2.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

// Purely visual effects (sparks, shockwaves, camera shake, screen flash).
// They never feed back into the physics.
struct Particle {
    Vec2 pos{};
    Vec2 vel{};
    Rgba color{255, 200, 50, 255};
    float size{3.0f};
    float lifetime{1.0f};
    float maxLifetime{1.0f};
};

struct Shockwave {
    Vec2 center{};
    float radius{4.0f};
    float maxRadius{90.0f};
    float speed{180.0f};
    Rgba color{255, 220, 100, 255};
};

class Effects {
public:
    static constexpr std::size_t kMaxParticles = 1500;
    static constexpr std::size_t kMaxShockwaves = 48;

    std::vector<Particle> particles;
    std::vector<Shockwave> shockwaves;
    float cameraShake{0.0f};
    float flash{0.0f};

    Effects() {
        particles.reserve(kMaxParticles);
        shockwaves.reserve(kMaxShockwaves);
    }

    // O(1): when full, overwrite a slot in round-robin order instead of
    // erasing from the front of the vector.
    void addParticle(const Particle& p) {
        if (particles.size() < kMaxParticles) {
            particles.push_back(p);
        } else {
            particles[particleCursor_++ % kMaxParticles] = p;
        }
    }

    void addShockwave(const Shockwave& s) {
        if (shockwaves.size() < kMaxShockwaves) {
            shockwaves.push_back(s);
        } else {
            shockwaves[shockCursor_++ % kMaxShockwaves] = s;
        }
    }

    void shake(float amount) { cameraShake = std::max(cameraShake, amount); }
    void addFlash(float amount) { flash = std::min(1.0f, std::max(flash, amount)); }

    void clear() {
        particles.clear();
        shockwaves.clear();
        cameraShake = 0.0f;
        flash = 0.0f;
        particleCursor_ = 0;
        shockCursor_ = 0;
    }

private:
    std::size_t particleCursor_{0};
    std::size_t shockCursor_{0};
};
