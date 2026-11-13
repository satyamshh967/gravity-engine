#include "GravityEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kMinFragmentableMass = 0.2;
constexpr double kElasticSparkSpeed = 20.0; // no sparks for gentle resting contacts
constexpr int kMaxEffectAttractors = 12;

const Rgba kEmberWhite{255, 250, 200, 255};
const Rgba kEmberOrange{255, 160, 40, 255};
const Rgba kEmberRed{255, 60, 40, 255};
const Rgba kGold{255, 220, 110, 255};
const Rgba kFire{255, 120, 50, 255};
const Rgba kPaleWhite{245, 245, 245, 255};

Rgba shade(Rgba c, double factor) {
    auto ch = [factor](unsigned char v) {
        return static_cast<unsigned char>(std::clamp(v * factor, 0.0, 255.0));
    };
    return Rgba{ch(c.r), ch(c.g), ch(c.b), c.a};
}
} // namespace

GravityEngine::GravityEngine() : rng_(std::random_device{}()) {
    loadScene(SceneId::SolarSystem);
}

// ---------------------------------------------------------------------------
// Gravity & integration
// ---------------------------------------------------------------------------

void GravityEngine::computeAccelerations() {
    const std::size_t n = bodies.size();
    for (auto& b : bodies) {
        b.acc = (enableUniformGravity && !b.isStatic) ? uniformGravity : Vec2{};
    }
    if (!mutualGravityActive()) return;

    const double epsSq = epsilon * epsilon;
    for (std::size_t i = 0; i < n; ++i) {
        Body& bi = bodies[i];
        for (std::size_t j = i + 1; j < n; ++j) {
            Body& bj = bodies[j];
            const Vec2 r = bj.pos - bi.pos;
            const double d2 = r.lengthSq() + epsSq;
            if (d2 < 1e-12) continue;
            const double f = G / (d2 * std::sqrt(d2));
            if (!bi.isStatic) bi.acc += r * (f * bj.mass);
            if (!bj.isStatic) bj.acc -= r * (f * bi.mass);
        }
    }
}

void GravityEngine::stepVelocityVerlet(double dt) {
    const std::size_t n = bodies.size();
    const double halfDtSq = 0.5 * dt * dt;
    const double halfDt = 0.5 * dt;

    scratchAcc_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        Body& b = bodies[i];
        scratchAcc_[i] = b.acc;
        if (!b.isStatic) b.pos += b.vel * dt + b.acc * halfDtSq;
    }

    computeAccelerations();

    for (std::size_t i = 0; i < n; ++i) {
        Body& b = bodies[i];
        if (!b.isStatic) b.vel += (scratchAcc_[i] + b.acc) * halfDt;
    }
}

void GravityEngine::stepSymplecticEuler(double dt) {
    for (auto& b : bodies) {
        if (b.isStatic) continue;
        b.vel += b.acc * dt;
        b.pos += b.vel * dt;
    }
    computeAccelerations();
}

void GravityEngine::step(double dt) {
    simTime_ += dt;
    if (bodies.empty()) return;

    if (integrator == IntegratorType::VelocityVerlet) {
        stepVelocityVerlet(dt);
    } else {
        stepSymplecticEuler(dt);
    }

    for (auto& b : bodies) b.addTrailPoint(b.pos);

    structureChanged_ = false;
    positionsCorrected_ = false;

    handleCollisions();
    if (roomBounds.enabled) handleRoomCollisions(dt);

    if (structureChanged_) {
        idIndexDirty_ = true;
        computeAccelerations();
        markEnergyBaseline();
    } else if (positionsCorrected_) {
        // Overlap correction moved bodies: refresh a(t) so the next Verlet
        // step does not use accelerations from the pre-correction positions.
        computeAccelerations();
    }
}

// ---------------------------------------------------------------------------
// Collisions
// ---------------------------------------------------------------------------

void GravityEngine::buildSweepOrder() {
    const std::size_t n = bodies.size();
    sweepOrder_.resize(n);
    for (std::size_t i = 0; i < n; ++i) sweepOrder_[i] = i;
    std::sort(sweepOrder_.begin(), sweepOrder_.end(), [this](std::size_t a, std::size_t b) {
        return (bodies[a].pos.x - bodies[a].radius) < (bodies[b].pos.x - bodies[b].radius);
    });
}

void GravityEngine::handleCollisions() {
    const std::size_t n = bodies.size();
    if (collisionMode == CollisionMode::None || n < 2) return;

    // Sweep-and-prune on the x axis: only pairs whose x-extents overlap are tested.
    buildSweepOrder();

    if (collisionMode == CollisionMode::Elastic) {
        for (std::size_t a = 0; a < n; ++a) {
            const std::size_t i = sweepOrder_[a];
            for (std::size_t b = a + 1; b < n; ++b) {
                const std::size_t j = sweepOrder_[b];
                if (bodies[j].pos.x - bodies[j].radius > bodies[i].pos.x + bodies[i].radius) break;
                resolveElasticPair(i, j);
            }
        }
        return;
    }

    // Merge / shatter modes
    removed_.assign(n, 0);
    pendingBodies_.clear();
    bool anyRemoved = false;

    for (std::size_t a = 0; a < n; ++a) {
        const std::size_t i = sweepOrder_[a];
        if (removed_[i]) continue;
        for (std::size_t b = a + 1; b < n; ++b) {
            const std::size_t j = sweepOrder_[b];
            if (bodies[j].pos.x - bodies[j].radius > bodies[i].pos.x + bodies[i].radius) break;
            if (removed_[j]) continue;

            const double minDist = bodies[i].radius + bodies[j].radius;
            if (bodies[i].pos.distanceSq(bodies[j].pos) >= minDist * minDist) continue;

            anyRemoved = true;
            if (collisionMode == CollisionMode::DestructiveShatter && tryShatterPair(i, j)) {
                removed_[i] = 1;
                removed_[j] = 1;
                break; // body i no longer exists
            }
            mergePair(i, j);
            removed_[j] = 1;
        }
    }

    if (!anyRemoved) return;

    std::vector<Body> remaining;
    remaining.reserve(n + pendingBodies_.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (!removed_[i]) remaining.push_back(std::move(bodies[i]));
    }
    for (auto& f : pendingBodies_) remaining.push_back(std::move(f));
    pendingBodies_.clear();
    bodies = std::move(remaining);
    markStructureChanged();
}

void GravityEngine::resolveElasticPair(std::size_t i, std::size_t j) {
    Body& bi = bodies[i];
    Body& bj = bodies[j];
    const Vec2 d = bj.pos - bi.pos;
    const double distSq = d.lengthSq();
    const double minDist = bi.radius + bj.radius;
    if (distSq >= minDist * minDist || distSq < 1e-18) return;

    const double dist = std::sqrt(distSq);
    const Vec2 normal = d / dist;
    const double invM1 = bi.isStatic ? 0.0 : 1.0 / bi.mass;
    const double invM2 = bj.isStatic ? 0.0 : 1.0 / bj.mass;
    const double invMSum = invM1 + invM2;
    if (invMSum <= 0.0) return;

    const double vNorm = (bj.vel - bi.vel).dot(normal);
    if (vNorm < 0.0) {
        const double impulse = -(1.0 + restitution) * vNorm / invMSum;
        bi.vel -= normal * (impulse * invM1);
        bj.vel += normal * (impulse * invM2);

        if (-vNorm > kElasticSparkSpeed) {
            const double reduced = 1.0 / invMSum;
            const double energy = 0.5 * reduced * vNorm * vNorm;
            spawnSparks(bi.pos + normal * bi.radius, normal.perp(), energy, bi.color, bj.color,
                        std::clamp(static_cast<int>(-vNorm * 0.08), 6, 24), 1.6f);
        }
    }

    const double overlap = minDist - dist;
    bi.pos -= normal * (overlap * (invM1 / invMSum));
    bj.pos += normal * (overlap * (invM2 / invMSum));
    positionsCorrected_ = true;
}

void GravityEngine::mergePair(std::size_t i, std::size_t j) {
    Body& s = bodies[i];
    Body& o = bodies[j];

    const Vec2 relVel = o.vel - s.vel;
    const double vRel = relVel.length();
    const double m1 = s.mass;
    const double m2 = o.mass;
    const double totalMass = m1 + m2;
    const double impactEnergy = 0.5 * (m1 * m2 / totalMass) * vRel * vRel;

    const Vec2 normal = (o.pos - s.pos).normalized();
    const Vec2 contact = s.pos + normal * s.radius;
    const double minDist = s.radius + o.radius;
    spawnShockwave(contact, static_cast<float>(minDist * 2.5), kGold, 160.0f);
    spawnSparks(contact, relVel.normalized(), impactEnergy, s.color, o.color, 30);
    if (vRel > 40.0) effects.shake(static_cast<float>(std::min(8.0, vRel * 0.05)));

    SimEvent ev;
    ev.type = SimEventType::Merge;
    ev.nameA = s.name;
    ev.nameB = o.name;
    ev.pos = contact;

    // The heavier body (or the static anchor) keeps its identity.
    const bool takeOther = (o.isStatic && !s.isStatic) || (s.isStatic == o.isStatic && m2 > m1);

    if (s.isStatic) {
        // stays anchored where it is
    } else if (o.isStatic) {
        s.isStatic = true;
        s.pos = o.pos;
        s.vel = Vec2{};
    } else {
        // Exact linear momentum and centre-of-mass conservation
        s.vel = (s.vel * m1 + o.vel * m2) / totalMass;
        s.pos = (s.pos * m1 + o.pos * m2) / totalMass;
    }

    // 2D disk area conservation
    s.radius = std::min(std::sqrt(s.radius * s.radius + o.radius * o.radius), limits::kMaxRadius);
    s.mass = totalMass;
    s.color = weightedColor(s.color, m1, o.color, m2);
    s.isDebris = s.isDebris && o.isDebris;

    if (takeOther) {
        ev.absorbedId = s.id;
        s.id = o.id;
        s.name = std::move(o.name);
        s.trail = std::move(o.trail);
        s.maxTrailPoints = o.maxTrailPoints;
    } else {
        ev.absorbedId = o.id;
    }
    ev.survivorId = s.id;
    events.push_back(std::move(ev));
}

bool GravityEngine::tryShatterPair(std::size_t i, std::size_t j) {
    const Body& a = bodies[i];
    const Body& b = bodies[j];
    if (a.isStatic || b.isStatic) return false;

    const double totalMass = a.mass + b.mass;
    if (totalMass < kMinFragmentableMass) return false;

    const Vec2 relVel = b.vel - a.vel;
    const double reduced = a.mass * b.mass / totalMass;
    const double impactEnergy = 0.5 * reduced * relVel.lengthSq();
    const double lumpRadius = std::sqrt(a.radius * a.radius + b.radius * b.radius);
    // Gravitational binding energy scale of the combined lump.
    const double binding = G * totalMass * totalMass / lumpRadius;
    if (impactEnergy <= shatterEnergyFactor * binding) return false;

    const int count = fragmentCountFor(totalMass);
    if (bodies.size() + pendingBodies_.size() + static_cast<std::size_t>(count) > maxBodies) return false;

    const Vec2 com = (a.pos * a.mass + b.pos * b.mass) / totalMass;
    const Vec2 comVel = (a.vel * a.mass + b.vel * b.mass) / totalMass;
    // Half of the impact energy goes into the debris' kinetic energy (COM frame).
    const double ejectSpeed = std::sqrt(impactEnergy / totalMass);
    const Rgba color = weightedColor(a.color, a.mass, b.color, b.mass);

    spawnFragments(totalMass, com, comVel, lumpRadius, color, ejectSpeed, count, pendingBodies_);

    spawnShockwave(com, static_cast<float>(lumpRadius * 6.0), kGold, 240.0f);
    spawnShockwave(com, static_cast<float>(lumpRadius * 3.5), kFire, 170.0f);
    spawnSparks(com, relVel.normalized(), impactEnergy, a.color, b.color, 70, 3.1f);
    spawnEmbers(com, comVel, 60);
    effects.shake(static_cast<float>(std::clamp(relVel.length() * 0.15, 4.0, 18.0)));
    effects.addFlash(0.25f);

    SimEvent ev;
    ev.type = SimEventType::Shatter;
    ev.nameA = a.name;
    ev.nameB = b.name;
    ev.absorbedId = a.id;
    ev.survivorId = b.id;
    ev.pos = com;
    events.push_back(std::move(ev));
    return true;
}

int GravityEngine::fragmentCountFor(double mass) {
    return std::clamp(3 + static_cast<int>(std::log2(1.0 + mass / 10.0)), 3, 8);
}

void GravityEngine::spawnFragments(double totalMass, Vec2 center, Vec2 baseVel, double lumpRadius, Rgba color,
                                   double ejectSpeed, int count, std::vector<Body>& out) {
    count = std::max(count, 2);
    const double fragMass = totalMass / count;
    const double fragRadius = std::clamp(lumpRadius / std::sqrt(static_cast<double>(count)),
                                         limits::kMinRadius, limits::kMaxRadius);
    // Ring radius large enough that neighbouring fragments never overlap.
    const double ringRadius = std::max(lumpRadius * 0.75, fragRadius * 1.1 / std::sin(kPi / count));

    std::uniform_real_distribution<double> rot(0.0, kTwoPi);
    std::uniform_real_distribution<double> jitter(0.8, 1.2);
    std::uniform_real_distribution<double> spin(-0.25, 0.25);
    std::uniform_real_distribution<double> tint(0.8, 1.15);

    const double offset = rot(rng_);
    const std::size_t first = out.size();
    Vec2 ejectSum{};

    for (int k = 0; k < count; ++k) {
        const double ang = offset + k * kTwoPi / count;
        const Vec2 dir{std::cos(ang), std::sin(ang)};
        const Vec2 eject = dir * (ejectSpeed * jitter(rng_)) + dir.perp() * (ejectSpeed * spin(rng_));
        ejectSum += eject;

        const int id = allocateId();
        Body frag(id, "Debris #" + std::to_string(id), fragMass, fragRadius,
                  center + dir * ringRadius, baseVel + eject, shade(color, tint(rng_)), false);
        frag.isDebris = true;
        frag.maxTrailPoints = 120;
        out.push_back(std::move(frag));
    }

    // Remove the mean ejection velocity so the fragments' total momentum is
    // exactly totalMass * baseVel (equal masses => subtract the average).
    const Vec2 mean = ejectSum / static_cast<double>(count);
    for (std::size_t k = first; k < out.size(); ++k) out[k].vel -= mean;
}

void GravityEngine::handleRoomCollisions(double dt) {
    struct Impact {
        std::size_t index;
        double speed;
        Vec2 contact;
        Vec2 normal; // points into the room
    };
    std::vector<Impact> shatters;
    const RoomBounds& rb = roomBounds;

    auto canShatter = [&](const Body& b, double speed) {
        return speed >= rb.shatterVelocity && b.mass >= rb.minShatterMass &&
               bodies.size() + 8 * (shatters.size() + 1) <= maxBodies;
    };
    auto impactSparks = [&](Vec2 contact, Vec2 normal, const Body& b, double speed, int maxCount) {
        if (speed <= 25.0) return;
        spawnSparks(contact, normal, 0.5 * b.mass * speed * speed, b.color, Rgba{255, 230, 140, 255},
                    std::clamp(static_cast<int>(speed * 0.05), 6, maxCount));
        if (speed > 320.0) spawnShockwave(contact, static_cast<float>(b.radius * 2.5), Rgba{255, 210, 100, 220}, 150.0f);
    };

    for (std::size_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (b.isStatic) continue;

        // Floor. Velocity is only reflected when the body moves INTO the wall,
        // so a body leaving an overlap is never pushed back into it.
        if (b.pos.y + b.radius > rb.yMax) {
            b.pos.y = rb.yMax - b.radius;
            positionsCorrected_ = true;
            if (b.vel.y > 0.0) {
                const double speed = b.vel.y;
                const Vec2 contact{b.pos.x, rb.yMax};
                if (canShatter(b, speed)) {
                    shatters.push_back({i, speed, contact, Vec2{0.0, -1.0}});
                    continue;
                }
                b.vel.y = -speed * rb.floorRestitution;
                if (std::abs(b.vel.y) < 18.0) b.vel.y = 0.0; // settle
                else b.vel.x *= 0.96;                       // friction during bounce
                impactSparks(contact, Vec2{0.0, -1.0}, b, speed, 35);
            }
            if (b.vel.y == 0.0) {
                // Rolling friction while resting on the floor
                b.vel.x *= (1.0 - 1.5 * dt);
                if (std::abs(b.vel.x) < 2.0) b.vel.x = 0.0;
            }
        }

        // Ceiling
        if (b.pos.y - b.radius < rb.yMin) {
            b.pos.y = rb.yMin + b.radius;
            positionsCorrected_ = true;
            if (b.vel.y < 0.0) {
                const double speed = -b.vel.y;
                const Vec2 contact{b.pos.x, rb.yMin};
                if (canShatter(b, speed)) {
                    shatters.push_back({i, speed, contact, Vec2{0.0, 1.0}});
                    continue;
                }
                b.vel.y = speed * rb.ceilingRestitution;
                b.vel.x *= 0.96;
                impactSparks(contact, Vec2{0.0, 1.0}, b, speed, 15);
            }
        }

        // Left wall
        if (b.pos.x - b.radius < rb.xMin) {
            b.pos.x = rb.xMin + b.radius;
            positionsCorrected_ = true;
            if (b.vel.x < 0.0) {
                const double speed = -b.vel.x;
                const Vec2 contact{rb.xMin, b.pos.y};
                if (canShatter(b, speed)) {
                    shatters.push_back({i, speed, contact, Vec2{1.0, 0.0}});
                    continue;
                }
                b.vel.x = speed * rb.wallRestitution;
                b.vel.y *= 0.98;
                impactSparks(contact, Vec2{1.0, 0.0}, b, speed, 15);
            }
        }

        // Right wall
        if (b.pos.x + b.radius > rb.xMax) {
            b.pos.x = rb.xMax - b.radius;
            positionsCorrected_ = true;
            if (b.vel.x > 0.0) {
                const double speed = b.vel.x;
                const Vec2 contact{rb.xMax, b.pos.y};
                if (canShatter(b, speed)) {
                    shatters.push_back({i, speed, contact, Vec2{-1.0, 0.0}});
                    continue;
                }
                b.vel.x = -speed * rb.wallRestitution;
                b.vel.y *= 0.98;
                impactSparks(contact, Vec2{-1.0, 0.0}, b, speed, 15);
            }
        }
    }

    if (shatters.empty()) return;

    // Process from the highest index down so erasing keeps earlier indices valid.
    std::sort(shatters.begin(), shatters.end(),
              [](const Impact& x, const Impact& y) { return x.index > y.index; });

    std::vector<Body> fragments;
    for (const Impact& imp : shatters) {
        Body b = std::move(bodies[imp.index]);
        bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(imp.index));

        // Debris rebounds off the surface with a fraction of the impact speed.
        const Vec2 vNormal = imp.normal * imp.speed;
        const Vec2 vTangent = b.vel - imp.normal * b.vel.dot(imp.normal);
        const Vec2 baseVel = vTangent * 0.5 + vNormal * 0.3;
        const int count = std::clamp(2 + static_cast<int>(std::log2(1.0 + b.mass)), 3, 6);

        const std::size_t first = fragments.size();
        spawnFragments(b.mass, b.pos, baseVel, b.radius, b.color, imp.speed * 0.22, count, fragments);
        for (std::size_t k = first; k < fragments.size(); ++k) {
            Body& f = fragments[k];
            f.pos.x = std::clamp(f.pos.x, rb.xMin + f.radius, rb.xMax - f.radius);
            f.pos.y = std::clamp(f.pos.y, rb.yMin + f.radius, rb.yMax - f.radius);
            if (f.vel.dot(imp.normal) < 0.0) f.vel -= imp.normal * (1.5 * f.vel.dot(imp.normal)); // never into the wall
        }

        spawnShockwave(imp.contact, static_cast<float>(b.radius * 7.0), kGold, 260.0f);
        spawnShockwave(imp.contact, static_cast<float>(b.radius * 4.0), kFire, 180.0f);
        spawnSparks(imp.contact, imp.normal, 0.5 * b.mass * imp.speed * imp.speed, b.color, kEmberOrange, 60);
        spawnEmbers(imp.contact, Vec2{}, 80);
        effects.shake(static_cast<float>(std::clamp(std::log10(b.mass + 10.0) * 4.5, 4.0, 20.0)));
        effects.addFlash(0.2f);

        SimEvent ev;
        ev.type = SimEventType::FloorShatter;
        ev.nameA = b.name;
        ev.absorbedId = b.id;
        ev.pos = imp.contact;
        events.push_back(std::move(ev));
    }
    for (auto& f : fragments) bodies.push_back(std::move(f));
    markStructureChanged();
}

bool GravityEngine::explodeBody(int id) {
    const std::ptrdiff_t idx = indexOf(id);
    if (idx < 0) return false;

    int count = fragmentCountFor(bodies[static_cast<std::size_t>(idx)].mass);
    const std::size_t room = maxBodies > bodies.size() ? maxBodies - bodies.size() + 1 : 0;
    count = std::min<int>(count, static_cast<int>(room));
    if (count < 3) return false;

    Body b = std::move(bodies[static_cast<std::size_t>(idx)]);
    bodies.erase(bodies.begin() + idx);

    // Eject faster than the body's own escape velocity so the debris disperses.
    const double vEsc = escapeVelocity(b);
    const double ejectSpeed = 1.3 * vEsc + 15.0;

    std::vector<Body> fragments;
    spawnFragments(b.mass, b.pos, b.vel, b.radius, b.color, ejectSpeed, count, fragments);
    if (roomBounds.enabled) {
        for (auto& f : fragments) {
            f.pos.x = std::clamp(f.pos.x, roomBounds.xMin + f.radius, roomBounds.xMax - f.radius);
            f.pos.y = std::clamp(f.pos.y, roomBounds.yMin + f.radius, roomBounds.yMax - f.radius);
        }
    }
    for (auto& f : fragments) bodies.push_back(std::move(f));

    spawnShockwave(b.pos, static_cast<float>(b.radius * 7.0), kGold, 260.0f);
    spawnShockwave(b.pos, static_cast<float>(b.radius * 4.0), kFire, 180.0f);
    spawnEmbers(b.pos, b.vel, 160);
    effects.shake(static_cast<float>(std::clamp(std::log10(b.mass + 10.0) * 4.5, 4.0, 25.0)));
    effects.addFlash(0.35f);

    SimEvent ev;
    ev.type = SimEventType::Explosion;
    ev.nameA = b.name;
    ev.absorbedId = b.id;
    ev.pos = b.pos;
    events.push_back(std::move(ev));

    idIndexDirty_ = true;
    computeAccelerations();
    markEnergyBaseline();
    return true;
}

// ---------------------------------------------------------------------------
// Visual effects
// ---------------------------------------------------------------------------

void GravityEngine::spawnShockwave(Vec2 pos, float maxRadius, Rgba color, float speed) {
    Shockwave s;
    s.center = pos;
    s.radius = 4.0f;
    s.maxRadius = std::max(maxRadius, 12.0f);
    s.speed = speed;
    s.color = color;
    effects.addShockwave(s);
}

void GravityEngine::spawnSparks(Vec2 pos, Vec2 dir, double energy, Rgba c1, Rgba c2, int count, float spread) {
    const float maxSpeed = static_cast<float>(std::min(260.0, 25.0 + std::sqrt(std::max(energy, 0.0)) * 6.0));
    std::uniform_real_distribution<float> angleSpread(-spread, spread);
    std::uniform_real_distribution<float> speedDist(15.0f, std::max(maxSpeed, 16.0f));
    std::uniform_real_distribution<float> lifeDist(0.4f, 1.3f);
    std::uniform_real_distribution<float> sizeDist(1.5f, 3.8f);
    std::uniform_real_distribution<double> mix(0.0, 1.0);

    const float base = (dir.lengthSq() > 1e-12) ? static_cast<float>(std::atan2(dir.y, dir.x)) : 0.0f;
    for (int k = 0; k < count; ++k) {
        const float a = base + angleSpread(rng_);
        const float spd = speedDist(rng_);
        Particle p;
        p.pos = pos;
        p.vel = Vec2{std::cos(a) * spd, std::sin(a) * spd};
        p.color = lerpColor(c1, c2, mix(rng_));
        p.color.a = 255;
        p.size = sizeDist(rng_);
        p.lifetime = p.maxLifetime = lifeDist(rng_);
        effects.addParticle(p);
    }
}

void GravityEngine::spawnEmbers(Vec2 pos, Vec2 baseVel, int count) {
    std::uniform_real_distribution<float> angleDist(0.0f, static_cast<float>(kTwoPi));
    std::uniform_real_distribution<float> speedDist(30.0f, 230.0f);
    std::uniform_real_distribution<float> lifeDist(0.8f, 2.6f);
    std::uniform_real_distribution<float> sizeDist(2.0f, 5.0f);
    for (int k = 0; k < count; ++k) {
        const float a = angleDist(rng_);
        const float spd = speedDist(rng_);
        Particle p;
        p.pos = pos;
        p.vel = baseVel + Vec2{std::cos(a) * spd, std::sin(a) * spd};
        p.color = (k % 3 == 0) ? kEmberWhite : (k % 3 == 1) ? kEmberOrange : kEmberRed;
        p.size = sizeDist(rng_);
        p.lifetime = p.maxLifetime = lifeDist(rng_);
        effects.addParticle(p);
    }
}

void GravityEngine::updateEffects(double dt) {
    if (dt <= 0.0) return;

    // Particles feel gravity only from the few heaviest bodies: O(P * 12)
    // instead of O(P * N) per update.
    attractors_.clear();
    if (mutualGravityActive() && !bodies.empty()) {
        for (std::size_t i = 0; i < bodies.size(); ++i) attractors_.push_back(i);
        const std::size_t keep = std::min<std::size_t>(kMaxEffectAttractors, attractors_.size());
        std::partial_sort(attractors_.begin(), attractors_.begin() + static_cast<std::ptrdiff_t>(keep),
                          attractors_.end(),
                          [this](std::size_t a, std::size_t b) { return bodies[a].mass > bodies[b].mass; });
        attractors_.resize(keep);
    }

    const int subSteps = std::max(1, static_cast<int>(std::ceil(dt / 0.02)));
    const double h = dt / subSteps;
    const double epsSq = epsilon * epsilon;
    const RoomBounds& rb = roomBounds;

    for (int s = 0; s < subSteps; ++s) {
        auto& ps = effects.particles;
        for (std::size_t k = 0; k < ps.size();) {
            Particle& p = ps[k];
            p.lifetime -= static_cast<float>(h);
            if (p.lifetime <= 0.0f) {
                p = ps.back(); // swap-and-pop: O(1) removal
                ps.pop_back();
                continue;
            }

            Vec2 acc = enableUniformGravity ? uniformGravity : Vec2{};
            for (std::size_t ai : attractors_) {
                const Body& b = bodies[ai];
                const Vec2 r = b.pos - p.pos;
                const double d2 = r.lengthSq() + epsSq;
                acc += r * (G * b.mass / (d2 * std::sqrt(d2)));
            }
            p.vel += acc * h;
            p.vel *= (1.0 - 0.15 * h);
            p.pos += p.vel * h;

            if (rb.enabled) {
                if (p.pos.y > rb.yMax) { p.pos.y = rb.yMax; p.vel.y = -std::abs(p.vel.y) * 0.35; p.vel.x *= 0.85; }
                if (p.pos.y < rb.yMin) { p.pos.y = rb.yMin; p.vel.y = std::abs(p.vel.y) * 0.35; }
                if (p.pos.x < rb.xMin) { p.pos.x = rb.xMin; p.vel.x = std::abs(p.vel.x) * 0.35; }
                if (p.pos.x > rb.xMax) { p.pos.x = rb.xMax; p.vel.x = -std::abs(p.vel.x) * 0.35; }
            }
            ++k;
        }
    }

    auto& sw = effects.shockwaves;
    for (std::size_t k = 0; k < sw.size();) {
        sw[k].radius += sw[k].speed * static_cast<float>(dt);
        if (sw[k].radius >= sw[k].maxRadius) {
            sw[k] = sw.back();
            sw.pop_back();
        } else {
            ++k;
        }
    }

    effects.cameraShake = std::max(0.0f, effects.cameraShake - 9.0f * static_cast<float>(dt));
    effects.flash = std::max(0.0f, effects.flash - 2.5f * static_cast<float>(dt));
}

// ---------------------------------------------------------------------------
// Energy & conservation diagnostics
// ---------------------------------------------------------------------------

double GravityEngine::computeKineticEnergy() const {
    double k = 0.0;
    for (const auto& b : bodies) {
        if (!b.isStatic) k += 0.5 * b.mass * b.vel.lengthSq();
    }
    return k;
}

double GravityEngine::computePotentialEnergy() const {
    double u = 0.0;
    if (enableUniformGravity) {
        // U = m g h, with h measured upwards from the floor
        for (const auto& b : bodies) {
            if (!b.isStatic) u += b.mass * uniformGravity.y * (roomBounds.yMax - b.pos.y);
        }
    }
    if (mutualGravityActive()) {
        const double epsSq = epsilon * epsilon;
        const std::size_t n = bodies.size();
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                const double d = std::sqrt(bodies[i].pos.distanceSq(bodies[j].pos) + epsSq);
                if (d > 1e-12) u -= G * bodies[i].mass * bodies[j].mass / d;
            }
        }
    }
    return u;
}

EnergyReport GravityEngine::energyReport() const {
    EnergyReport r;
    r.kinetic = computeKineticEnergy();
    r.potential = computePotentialEnergy();
    r.total = r.kinetic + r.potential;
    if (baselineValid_ && std::abs(baselineEnergy_) > 1e-12) {
        r.driftPercent = std::abs(r.total - baselineEnergy_) / std::abs(baselineEnergy_) * 100.0;
    }
    // Floor friction and restitution make the room intentionally dissipative.
    r.driftMeaningful = !roomBounds.enabled;
    return r;
}

void GravityEngine::markEnergyBaseline() {
    baselineEnergy_ = computeTotalEnergy();
    baselineValid_ = true;
}

Vec2 GravityEngine::totalMomentum() const {
    Vec2 p{};
    for (const auto& b : bodies) {
        if (!b.isStatic) p += b.vel * b.mass;
    }
    return p;
}

double GravityEngine::totalMass() const {
    double m = 0.0;
    for (const auto& b : bodies) m += b.mass;
    return m;
}

Vec2 GravityEngine::centerOfMass() const {
    Vec2 c{};
    double m = 0.0;
    for (const auto& b : bodies) {
        c += b.pos * b.mass;
        m += b.mass;
    }
    return m > 0.0 ? c / m : Vec2{};
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

double GravityEngine::circularSpeed(double centralMass, double r) const {
    if (r <= 1e-9) return 0.0;
    // Softened force: a = G M r / (r^2 + eps^2)^1.5  =>  v^2 = a r
    const double d2 = r * r + epsilon * epsilon;
    return std::sqrt(G * centralMass * r * r / (d2 * std::sqrt(d2)));
}

double GravityEngine::surfaceGravity(const Body& b) const {
    const double d2 = b.radius * b.radius + epsilon * epsilon;
    return d2 > 1e-12 ? G * b.mass * b.radius / (d2 * std::sqrt(d2)) : 0.0;
}

double GravityEngine::escapeVelocity(const Body& b) const {
    // Softened potential: phi = -G M / sqrt(r^2 + eps^2)
    const double d = std::sqrt(b.radius * b.radius + epsilon * epsilon);
    return d > 1e-9 ? std::sqrt(2.0 * G * b.mass / d) : 0.0;
}

// The body a point "belongs to": the smallest Hill sphere (relative to the
// heaviest body) that contains the point, otherwise the heaviest body itself.
// Raw pull is the wrong criterion: the Sun pulls harder on Phobos than Mars does,
// yet Phobos orbits Mars.
int GravityEngine::dominantAttractorIndex(Vec2 pos, int excludeId, double probeMass) const {
    int primary = -1;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].id == excludeId) continue;
        if (primary < 0 || bodies[i].mass > bodies[static_cast<std::size_t>(primary)].mass) {
            primary = static_cast<int>(i);
        }
    }
    if (primary < 0) return -1;
    const Body& P = bodies[static_cast<std::size_t>(primary)];
    if (P.mass <= probeMass) return -1;

    int best = primary;
    double bestHill = 1e300;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const Body& A = bodies[i];
        if (static_cast<int>(i) == primary || A.id == excludeId || A.mass <= probeMass) continue;
        const double hill = A.pos.distance(P.pos) * std::cbrt(A.mass / (3.0 * P.mass));
        if (hill < bestHill && pos.distanceSq(A.pos) < hill * hill) {
            bestHill = hill;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Vec2 GravityEngine::computeCircularOrbitVelocity(Vec2 spawnPos, double spawnMass, int excludeId) const {
    const int k = dominantAttractorIndex(spawnPos, excludeId, spawnMass);
    if (k < 0) return Vec2{};
    const Body& d = bodies[static_cast<std::size_t>(k)];
    const Vec2 delta = spawnPos - d.pos;
    const double r = delta.length();
    if (r < 1e-3) return Vec2{};
    const double mass = d.isStatic ? d.mass : d.mass + spawnMass;
    return d.vel + (delta / r).perp() * circularSpeed(mass, r);
}

void GravityEngine::circularizeOrbit(int bodyId) {
    Body* body = getBodyById(bodyId);
    if (!body || body->isStatic) return;
    body->vel = computeCircularOrbitVelocity(body->pos, body->mass, bodyId);
    markEnergyBaseline();
}

Vec2 GravityEngine::computeNetAccelerationOnPoint(Vec2 pos) const {
    Vec2 a = enableUniformGravity ? uniformGravity : Vec2{};
    if (!mutualGravityActive()) return a;
    const double epsSq = epsilon * epsilon;
    for (const auto& b : bodies) {
        const Vec2 r = b.pos - pos;
        const double d2 = r.lengthSq() + epsSq;
        if (d2 > 1e-12) a += r * (G * b.mass / (d2 * std::sqrt(d2)));
    }
    return a;
}

OrbitInfo GravityEngine::orbitInfo(int bodyId) const {
    OrbitInfo info;
    const Body* b = getBodyById(bodyId);
    if (!b || b->isStatic || !mutualGravityActive()) return info;
    const int k = dominantAttractorIndex(b->pos, bodyId, b->mass);
    if (k < 0) return info;
    const Body& a = bodies[static_cast<std::size_t>(k)];

    const double mu = G * (a.mass + b->mass);
    const Vec2 r = b->pos - a.pos;
    const Vec2 v = b->vel - a.vel;
    const double dist = std::sqrt(r.lengthSq() + epsilon * epsilon);
    const double energy = 0.5 * v.lengthSq() - mu / dist;
    const double h = r.cross(v);

    info.valid = true;
    info.attractorId = a.id;
    info.attractorName = a.name;
    info.bound = energy < 0.0;
    info.eccentricity = std::sqrt(std::max(0.0, 1.0 + 2.0 * energy * h * h / (mu * mu)));
    if (info.bound) {
        info.semiMajorAxis = -mu / (2.0 * energy);
        info.period = kTwoPi * std::sqrt(info.semiMajorAxis * info.semiMajorAxis * info.semiMajorAxis / mu);
    }
    return info;
}

TrajectoryPrediction GravityEngine::predictTrajectory(Vec2 startPos, Vec2 startVel, double probeRadius,
                                                      int steps, double dt) const {
    TrajectoryPrediction out;
    out.points.reserve(static_cast<std::size_t>(std::max(steps, 0)) + 1);
    const bool mutual = mutualGravityActive();
    const double epsSq = epsilon * epsilon;

    struct Snap {
        Vec2 pos, vel, acc;
        double mass;
        double radius;
        bool fixed;
    };
    std::vector<Snap> snap;
    snap.reserve(bodies.size());
    for (const auto& b : bodies) snap.push_back({b.pos, b.vel, Vec2{}, b.mass, b.radius, b.isStatic});

    // Small systems: let the other bodies move too (exact N-body preview).
    // Large systems: freeze them to keep the preview cheap.
    const bool moveBodies = mutual && snap.size() <= 48;

    auto bodyAccels = [&]() {
        for (auto& s : snap) s.acc = Vec2{};
        for (std::size_t i = 0; i < snap.size(); ++i) {
            for (std::size_t j = i + 1; j < snap.size(); ++j) {
                const Vec2 r = snap[j].pos - snap[i].pos;
                const double d2 = r.lengthSq() + epsSq;
                const double f = G / (d2 * std::sqrt(d2));
                if (!snap[i].fixed) snap[i].acc += r * (f * snap[j].mass);
                if (!snap[j].fixed) snap[j].acc -= r * (f * snap[i].mass);
            }
        }
    };
    auto probeAccel = [&](Vec2 p) {
        Vec2 a = enableUniformGravity ? uniformGravity : Vec2{};
        if (mutual) {
            for (const auto& s : snap) {
                const Vec2 r = s.pos - p;
                const double d2 = r.lengthSq() + epsSq;
                a += r * (G * s.mass / (d2 * std::sqrt(d2)));
            }
        }
        return a;
    };

    if (mutual) {
        const int k = dominantAttractorIndex(startPos, -1, 0.0);
        if (k >= 0) {
            const Body& d = bodies[static_cast<std::size_t>(k)];
            const Vec2 r = startPos - d.pos;
            const Vec2 v = startVel - d.vel;
            const double energy = 0.5 * v.lengthSq() - G * d.mass / std::sqrt(r.lengthSq() + epsSq);
            out.hasOrbitClass = true;
            out.bound = energy < 0.0;
            out.attractorName = d.name;
        }
    }

    if (moveBodies) bodyAccels();
    std::vector<Vec2> oldAcc(moveBodies ? snap.size() : 0);
    Vec2 p = startPos;
    Vec2 v = startVel;
    Vec2 a = probeAccel(p);
    const RoomBounds& rb = roomBounds;

    for (int stepIdx = 0; stepIdx < steps; ++stepIdx) {
        out.points.push_back(p);

        p += v * dt + a * (0.5 * dt * dt);
        if (moveBodies) {
            for (auto& s : snap) {
                if (!s.fixed) s.pos += s.vel * dt + s.acc * (0.5 * dt * dt);
            }
            for (std::size_t i = 0; i < snap.size(); ++i) oldAcc[i] = snap[i].acc;
            bodyAccels();
            for (std::size_t i = 0; i < snap.size(); ++i) {
                if (!snap[i].fixed) snap[i].vel += (oldAcc[i] + snap[i].acc) * (0.5 * dt);
            }
        }
        const Vec2 aNew = probeAccel(p);
        v += (a + aNew) * (0.5 * dt);
        a = aNew;

        for (std::size_t i = 0; i < snap.size(); ++i) {
            const double reach = snap[i].radius + probeRadius;
            if (snap[i].pos.distanceSq(p) < reach * reach) {
                out.hitsBody = true;
                out.hitName = bodies[i].name;
                out.points.push_back(p);
                out.endPos = p;
                return out;
            }
        }

        if (rb.enabled) {
            auto bounce = [&](bool hit, double& posComp, double limit, double& velComp, double rest) {
                if (!hit) return false;
                if (std::abs(velComp) >= rb.shatterVelocity) return true;
                posComp = limit;
                velComp = -velComp * rest;
                return false;
            };
            bool shatter = false;
            shatter |= bounce(p.y + probeRadius > rb.yMax && v.y > 0, p.y, rb.yMax - probeRadius, v.y, rb.floorRestitution);
            shatter |= bounce(p.y - probeRadius < rb.yMin && v.y < 0, p.y, rb.yMin + probeRadius, v.y, rb.ceilingRestitution);
            shatter |= bounce(p.x - probeRadius < rb.xMin && v.x < 0, p.x, rb.xMin + probeRadius, v.x, rb.wallRestitution);
            shatter |= bounce(p.x + probeRadius > rb.xMax && v.x > 0, p.x, rb.xMax - probeRadius, v.x, rb.wallRestitution);
            if (shatter) {
                out.wouldShatter = true;
                out.points.push_back(p);
                out.endPos = p;
                return out;
            }
        }
    }
    out.endPos = p;
    return out;
}

// ---------------------------------------------------------------------------
// Body management
// ---------------------------------------------------------------------------

int GravityEngine::addBody(Body b) {
    if (b.id == 0) b.id = allocateId();
    if (b.isStatic) b.vel = Vec2{};
    const int id = b.id;
    bodies.push_back(std::move(b));
    idIndexDirty_ = true;
    computeAccelerations();
    markEnergyBaseline();
    return id;
}

void GravityEngine::clearBodies() {
    bodies.clear();
    effects.clear();
    events.clear();
    idIndexDirty_ = true;
    markEnergyBaseline();
}

bool GravityEngine::deleteBody(int id) {
    const std::ptrdiff_t idx = indexOf(id);
    if (idx < 0) return false;
    bodies.erase(bodies.begin() + idx);
    idIndexDirty_ = true;
    computeAccelerations();
    markEnergyBaseline();
    return true;
}

void GravityEngine::notifyBodiesEdited() {
    for (auto& b : bodies) {
        if (b.isStatic) b.vel = Vec2{};
    }
    computeAccelerations();
    markEnergyBaseline();
}

void GravityEngine::rebuildIdIndex() const {
    idIndex_.clear();
    idIndex_.reserve(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) idIndex_[bodies[i].id] = i;
    idIndexDirty_ = false;
}

std::ptrdiff_t GravityEngine::indexOf(int id) const {
    if (idIndexDirty_ || idIndex_.size() != bodies.size()) rebuildIdIndex();
    auto it = idIndex_.find(id);
    if (it != idIndex_.end() && it->second < bodies.size() && bodies[it->second].id == id) {
        return static_cast<std::ptrdiff_t>(it->second);
    }
    // Index stale (bodies modified directly): rebuild once and retry.
    rebuildIdIndex();
    it = idIndex_.find(id);
    return it != idIndex_.end() ? static_cast<std::ptrdiff_t>(it->second) : -1;
}

Body* GravityEngine::getBodyById(int id) {
    const std::ptrdiff_t idx = indexOf(id);
    return idx >= 0 ? &bodies[static_cast<std::size_t>(idx)] : nullptr;
}

const Body* GravityEngine::getBodyById(int id) const {
    const std::ptrdiff_t idx = indexOf(id);
    return idx >= 0 ? &bodies[static_cast<std::size_t>(idx)] : nullptr;
}

int GravityEngine::findNearestBody(Vec2 worldPos, double pickPadding) const {
    int bestId = -1;
    double bestScore = 1e300;
    for (const auto& b : bodies) {
        const double reach = b.radius + pickPadding;
        const double d2 = b.pos.distanceSq(worldPos);
        if (d2 >= reach * reach) continue;
        // Prefer the body whose surface is closest to the cursor.
        const double score = std::sqrt(d2) - b.radius;
        if (score < bestScore) {
            bestScore = score;
            bestId = b.id;
        }
    }
    return bestId;
}

const char* GravityEngine::getIntegratorName() const {
    switch (integrator) {
        case IntegratorType::VelocityVerlet: return "Velocity Verlet";
        case IntegratorType::SymplecticEuler: return "Symplectic Euler";
    }
    return "Unknown";
}

const char* GravityEngine::getCollisionModeName() const {
    switch (collisionMode) {
        case CollisionMode::None: return "Ghost";
        case CollisionMode::Elastic: return "Bounce";
        case CollisionMode::InelasticMerge: return "Merge";
        case CollisionMode::DestructiveShatter: return "Shatter";
    }
    return "Unknown";
}
