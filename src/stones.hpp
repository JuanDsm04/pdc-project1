#pragma once

#include <cstdint>
#include <vector>

#include "math3d.hpp"
#include "chambers.hpp"
#include "grid.hpp"
#include "particles.hpp"

enum class StoneKind { Space, Mind, Reality, Power, Time, Soul, Count };

// Mouth radius of the Space stone's wormhole. Shared with the renderer so the ring drawn
// at the exit is exactly the opening particles actually come out of, rather than a
// decorative circle that drifts out of step with the physics.
constexpr float kSpacePortalRadius = kChamberRadius * 0.21f;

// The finished stones use hand-authored, convex-ish outlines rather than a noisy
// circle. Keeping the points in normalized screen space makes the silhouette cheap to
// rasterize while still allowing every stone to have a completely different aspect ratio.
constexpr int kMaxStoneOutlinePoints = 16;
constexpr int kMaxStoneFacetSeeds = 8;

struct StoneOutlinePoint {
    float x = 0.0f;
    float y = 0.0f;
};

// A small Voronoi field inside the silhouette gives the stone broad crystal planes. The
// value is the amount of light caught by that plane; it is intentionally authored with the
// outline so the internal structure is different for every stone as well.
struct StoneFacetSeed {
    float x = 0.0f;
    float y = 0.0f;
    float light = 1.0f;
};

// A stone's own moving body: what App needs to render it, plus what applyForces needs to
// know where it is. Orbit choreography lives in stones.cpp, not here, since nothing outside
// Stones ever reads it; only kind, color and the current position cross that boundary.
struct Stone {
    StoneKind kind;
    float r, g, b;
    Vec3  position;
    Vec3  velocity;

    int outlineCount = 0;
    StoneOutlinePoint outline[kMaxStoneOutlinePoints] = {};

    int facetSeedCount = 0;
    StoneFacetSeed facetSeeds[kMaxStoneFacetSeeds] = {};
};

// Distance from the stone's center to its polygon edge along a screen-space ray. A stone
// with no authored outline returns 1, so unfinished stones retain their circular glow.
float stoneShapeRadius(const Stone& stone, float angle);

// Lighting multiplier for a normalized point inside a stone. Authored facet seeds form
// irregular planes and bright seams; unfinished circular stones simply return 1.
float stoneSurfaceLighting(const Stone& stone, float x, float y);

// Where the ring is in its cycle. The chambers exist to make the six powers legible, but
// sealed globes can never collide or snap, so the walls dissolve periodically and everything
// converges on the centre before reforming.
enum class Phase { Contained, Converge, Impact, Reform };

// Shared representation for Power's regular fronts and the stronger fronts emitted by
// stone collisions. Collision fronts carry their blended colour into both the particle
// tint and the visible ring.
struct ShockFront {
    Vec3 center;
    float age = 0.0f;
    float speed = 0.0f;
    float maxRadius = 0.0f;
    float shellWidth = 0.0f;
    float impulse = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    bool fromCollision = false;
};

// Small, short-lived visual particles emitted at the contact point. They are deliberately
// separate from the configurable simulation cloud: a collision effect must not silently
// change the --particles workload that later benchmark steps measure.
struct CollisionSpark {
    Vec3 position;
    Vec3 velocity;
    float age = 0.0f;
    float lifetime = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

// Owns the six stones' positions and the forces the stones that already have physics
// exert on the particle system. Reality and Mind still only move on their scripted orbit
// for now; their powers arrive at step 12.
class Stones {
public:
    Stones();

    // Advances stone positions and ages or spawns Power's shockwave fronts. Serial: six
    // bodies and a handful of shockwave fronts is negligible next to the particle loop.
    // Takes the particle system because the transition into Reform reassigns every particle
    // to a new chamber and re-sorts them, which cannot be done from outside without leaking
    // the phase boundary into the caller.
    void update(double time, float dt, ParticleSystem& particles);

    Phase phase() const { return m_phase; }

    // 0 while the stones sit in their chambers, 1 while they are gathered at the centre.
    // Drives stone positions, the camera push in, and whether forces are chamber local.
    float gather() const { return m_gather; }

    bool chamberWallsUp() const { return m_phase == Phase::Contained; }

    // Every stone's power applied to every particle in one pass. Parallel over particles:
    // an iteration only ever touches its own particle, plus two shared counters that are
    // integer reductions and so stay exact. The grid is read only here; Mind consults its
    // cell summaries and nothing writes back into it.
    void applyForces(ParticleSystem& particles, const SpatialGrid& grid, float dt);

private:
    // One kernel per chamber, each over that chamber's contiguous range with no gate.
    void applySoul(ParticleSystem& particles, float dt);
    void applyPower(ParticleSystem& particles, float dt);
    void applySpace(ParticleSystem& particles, float dt);
    void applyTime(ParticleSystem& particles);
    void applyReality(ParticleSystem& particles, float dt);
    void applyMind(ParticleSystem& particles, const SpatialGrid& grid, float dt);

    // The pull that actually moves the cloud during a merge: toward the centre while the
    // stones gather, toward each particle's newly assigned chamber while they reform.
    void applyMergeDrift(ParticleSystem& particles, float dt);

    void reassignChambers(ParticleSystem& particles);

public:
    const Stone& stone(StoneKind kind) const { return m_stones[static_cast<size_t>(kind)]; }
    const std::vector<Stone>& all() const { return m_stones; }

    // Fed by the capture reduction in applyForces; used to brighten Soul's own glow with
    // how much it currently holds, so the capture and release cycle reads visually even
    // before the HUD or a report surfaces the raw count.
    int soulCapturedCount() const { return m_soulCapturedCount; }

    // Where particles swallowed by the Space stone come back out. Rendered as a ring so
    // that a particle vanishing at one portal and reappearing here reads as a wormhole
    // rather than as a glitch.
    Vec3 spaceExitPortal() const { return m_spaceExit; }

    // True while the Time stone is running its bubble backwards, so the renderer can tint
    // the moment rather than leaving the reversal unexplained.
    bool timeRewinding() const { return m_rewinding; }

    // Particles the Space stone swallowed on the last step. Mirrors soulCapturedCount as a
    // cheap way to confirm the portal is actually firing without watching the window.
    int spaceTeleportCount() const { return m_spaceTeleportCount; }

    // Smoothed share of the cloud crossing the wormhole, in roughly 0 to 1. Both mouths
    // brighten with it, which is what ties the swallow and the emission together as one
    // event on screen instead of two unrelated lights.
    float spaceActivity() const { return m_spaceActivity; }

    // Read-only effect state consumed by App's CPU renderer.
    const std::vector<ShockFront>& shockFronts() const { return m_shockFronts; }
    const std::vector<CollisionSpark>& collisionSparks() const { return m_collisionSparks; }
    float collisionFlash() const { return m_collisionFlash; }
    Vec3 collisionFlashColor() const { return m_collisionFlashColor; }
    int stoneCollisionCount() const { return m_stoneCollisionCount; }

private:
    // Snapshots of every particle position, kept in a ring so the Time stone has a past to
    // rewind into. Stored here rather than in ParticleSystem because Time is the only thing
    // that reads it and integrate never touches it, so it would only pollute that loop's
    // cache footprint. Laid out slot major, index [slot * count + particle].
    void recordHistory(const ParticleSystem& particles);
    void simulateImpact(float dt);
    void emitCollisionEffects(int first, int second, Vec3 point, Vec3 normal);

    std::vector<Stone> m_stones;
    std::vector<ShockFront> m_shockFronts;
    std::vector<CollisionSpark> m_collisionSparks;

    double m_lastShockSpawn = -1e9;
    int    m_soulCapturedCount = 0;
    bool   m_releasing = false;

    Vec3   m_spaceJumpOffset;
    Vec3   m_spaceExit;
    double m_lastSpaceJump = -1e9;
    uint32_t m_spaceJumpCounter = 0;
    int      m_spaceTeleportCount = 0;
    float    m_spaceActivity = 0.0f;
    float    m_spaceActivityTarget = 0.0f;

    Phase  m_phase = Phase::Contained;
    float  m_gather = 0.0f;
    uint32_t m_cycleCount = 0;

    Vec3 m_reformStart[kChamberCount] = {};
    uint16_t m_collisionContacts = 0;
    uint32_t m_collisionEventCounter = 0;
    int m_stoneCollisionCount = 0;
    float m_collisionFlash = 0.0f;
    Vec3 m_collisionFlashColor = {1.0f, 1.0f, 1.0f};

    std::vector<float> m_historyX, m_historyY, m_historyZ;
    int  m_historyCount = 0;   // particles the ring is currently sized for
    int  m_historyFilled = 0;  // slots written so far, so a fresh run cannot read garbage
    int  m_historyCursor = 0;  // next slot to write
    int  m_historyStep = 0;
    bool m_rewinding = false;
    float m_rewindProgress = 0.0f;
};
