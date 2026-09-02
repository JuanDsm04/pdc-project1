#include "stones.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "parallel.hpp"

namespace {

// Same scripted orbit every stone has moved on since the placeholder glow. Kept here now
// that Soul and Power need their position for physics, not just for rendering.
struct OrbitParams {
    float r, g, b;
    float orbitRadius, orbitSpeed, phase, bob;
};

const OrbitParams kOrbits[6] = {
    {0.20f, 0.45f, 1.00f, 0.80f, 0.42f, 0.0f, 0.35f},  // space
    {1.00f, 0.85f, 0.15f, 0.55f, 0.61f, 1.0f, 0.20f},  // mind
    {1.00f, 0.15f, 0.20f, 0.95f, 0.29f, 2.1f, 0.45f},  // reality
    {0.45f, 0.12f, 1.00f, 0.70f, 0.51f, 3.4f, 0.28f},  // power
    {0.25f, 1.00f, 0.40f, 0.45f, 0.73f, 4.2f, 0.15f},  // time
    {1.00f, 0.32f, 0.03f, 0.88f, 0.36f, 5.1f, 0.40f},  // soul
};

// The merge cycle. Ten seconds of contained chambers is enough to read all six powers
// before the walls drop; the rest is the gather, the collision, and the reform.
constexpr double kContainedDuration = 10.0;
constexpr double kConvergeDuration  = 3.0;
constexpr double kImpactDuration    = 2.0;
constexpr double kReformDuration    = 3.0;
constexpr double kCycleDuration =
    kContainedDuration + kConvergeDuration + kImpactDuration + kReformDuration;

// How far from the world origin the stones mill about during impact. They must not stack on
// one point or there is nothing for the collisions at step 17 to resolve.
constexpr float kImpactOrbitRadius = 0.34f;

// Pull toward the centre while gathering, and toward the newly assigned chamber while
// reforming. Strong enough that a cloud crosses the ring inside the phase it is given.
constexpr float kConvergePull = 2.60f;
constexpr float kReformPull   = 3.40f;

// How much of the globe the returning cloud is aimed across.
constexpr float kReformSpread  = 0.62f;

// Smoothstep, so the walls do not snap open and the stones do not start moving at full
// speed on the first frame of a phase.
inline float smoothstep01(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// A stone no longer crosses the whole world. It drifts on a small orbit inside its own
// chamber, which keeps it moving without letting it wander into anyone else's globe. The
// authored orbitRadius and bob are reused as shape, scaled down to the chamber.
constexpr float kStoneOrbitScale = 0.30f;
constexpr float kStoneBobScale   = 0.22f;

Vec3 orbitPosition(const OrbitParams& o, double time, int chamber) {
    const float angle = static_cast<float>(time) * o.orbitSpeed + o.phase;
    const Vec3 local = {std::cos(angle) * o.orbitRadius * kChamberRadius * kStoneOrbitScale,
                        std::sin(angle * 1.7f + o.phase) * o.bob * kChamberRadius * kStoneBobScale,
                        std::sin(angle) * o.orbitRadius * kChamberRadius * kStoneOrbitScale};
    return chamberCenter(chamber) + local;
}

// Where a stone sits once the walls are down. Each keeps its own phase around a small orbit
// at the centre, so the six arrive as a milling cluster rather than a single stacked point.
Vec3 impactPosition(const OrbitParams& o, double time, int chamber) {
    const float angle = static_cast<float>(time) * 0.9f +
                        static_cast<float>(chamber) * (6.28318531f / kChamberCount);
    return {std::cos(angle) * kImpactOrbitRadius,
            std::sin(angle * 1.4f + o.phase) * kImpactOrbitRadius * 0.45f,
            std::sin(angle) * kImpactOrbitRadius};
}

// Soul: particles inside kCaptureRadius are held in a circular orbit instead of left to
// free fall, which is what "capture" means here. kOuterRadius is wider, so particles get a
// plain inverse square pull toward the stone before they are close enough to be captured,
// instead of a hard edge where nothing happens until they cross the capture line.
// Every length below is written as a fraction of the chamber rather than an absolute world
// distance. The stones were originally tuned against a box more than twice this wide, and
// left alone their influence radii would each swallow a whole chamber whole, flattening the
// structure that makes them tell apart: no funnel, no bubble edge, no shell, just six
// uniformly affected globes.
constexpr float kSoulCaptureRadius = kChamberRadius * 0.36f;
constexpr float kSoulOuterRadius   = kChamberRadius * 0.78f;
constexpr float kSoulSoftening     = kChamberRadius * 0.04f;

// Gravity is not a length, so it does not scale with one. Orbital speed goes as sqrt(G/r),
// so holding the visual orbit rate steady while r shrinks means G falls with the cube of
// the scale, not linearly.
constexpr float kSoulG             = 0.095f;

// How fast a captured particle's velocity is corrected toward a clean circular orbit.
// Higher settles particles into a tight ring faster; too high and the correction itself
// becomes visible as an oscillation instead of a smooth spiral into the ring.
constexpr float kOrbitDamping = 2.5f;

// Every kSoulReleasePeriod seconds, captured particles are let go for kSoulReleaseDuration
// seconds: the orbit lock is skipped and capture is not reevaluated, so whatever velocity
// a particle already has carries it away from the ring under its own momentum.
constexpr double kSoulReleasePeriod   = 6.0;
constexpr double kSoulReleaseDuration = 1.0;

// Power: a new shockwave front spawns every kShockSpawnPeriod seconds and expands at
// kShockSpeed until it passes kShockMaxRadius, wide enough to clear the whole particle
// volume. The impulse is a thin gaussian shell around the front's current radius rather
// than a hard step, so it reads as a wave passing through instead of a sphere popping.
constexpr double kShockSpawnPeriod = 0.9;
constexpr float  kShockSpeed       = kChamberRadius * 1.05f;
constexpr float  kShockMaxRadius   = kChamberRadius * 2.05f;
constexpr float  kShockShellWidth  = kChamberRadius * 0.065f;
constexpr float  kShockImpulse     = 0.70f;

// Space: a one way wormhole. Only the entry mouth, which sits on the stone itself,
// swallows particles; the exit only emits. A two way pair would trap a particle bouncing
// between the mouths forever, since it always arrives inside the other one's radius.
constexpr double kSpaceJumpPeriod = 3.5;
constexpr float  kSpaceJumpRange  = kChamberRadius * 0.42f;

// Without an inflow the portal only catches whatever happens to wander into it, so nothing
// on screen connects the two mouths and the stones just look like they are drifting. Pulling
// particles in over a much wider radius than the mouth builds a visible funnel feeding the
// entry, and since velocity carries through the portal, an equally visible spray leaving the
// exit. The swirl term makes them spiral in rather than fall straight, which reads as a
// vortex instead of a smudge.
constexpr float kSpaceInflowRadius = kChamberRadius * 1.30f;
constexpr float kSpaceInflowPull   = 1.00f;
constexpr float kSpaceInflowSwirl  = 0.70f;

// Both mouths must stay at least this far from the origin. The exit is the entry negated,
// so a stone drifting onto the origin would put the two mouths on top of each other and a
// particle would teleport into the entry it just left, every step, forever.
constexpr float kSpaceMinOriginDistance = kChamberRadius * 0.30f;

// Converts teleports per particle into the 0 to 1 range the mouths pulse over, then eases
// toward it so the flare tracks throughput without flickering frame to frame.
constexpr float kSpaceActivityScale = 220.0f;
constexpr float kSpaceActivityEase  = 0.12f;

// Time: dt is scaled down toward kTimeMinScale at the center of the bubble, so particles
// crossing it visibly wade through it and speed back up on the far side. Anything at or
// below zero would freeze them permanently rather than slow them.
constexpr float  kTimeBubbleRadius = kChamberRadius * 0.66f;
constexpr float  kTimeMinScale     = 0.10f;

// Position snapshots for the rewind, kept every kHistoryStride physics steps. Sixteen
// slots at a 120 Hz step is a shade under a second of recallable past. Cost is
// 12 bytes per particle per slot, which is the memory bandwidth this stone is meant to
// stress; it also means history is the one part of the project whose footprint scales
// with --particles, worth watching when that flag lands.
constexpr int    kHistorySlots  = 16;
constexpr int    kHistoryStride = 6;

constexpr double kTimeRewindPeriod   = 7.5;
constexpr double kTimeRewindDuration = 0.45;

// Reality: positions are rotated about the stone's own axis by an angle that falls off with
// distance, so straight paths bend into a spiral sheet rather than merely speeding up. The
// per particle sine and cosine are the point of this stone, not an accident: it is the
// highest FLOP kernel of the six and the one that shows what happens when a parallel loop
// is arithmetic bound instead of memory bound.
constexpr float kRealityRadius = kChamberRadius * 1.30f;
constexpr float kRealityTwist  = 3.10f;

// Warped matter is dragged toward a hot saturated core colour. Reality is the only stone
// that edits a particle's appearance rather than only its motion.
constexpr float kRealityTintRate = 1.30f;
constexpr float kRealityTint[3]  = {1.00f, 0.13f, 0.17f};

// Mind: classic boids, but weighed against grid cell summaries rather than individual
// neighbours. Twenty seven cell reads per particle is tractable where five thousand pairwise
// distance checks is not, and it keeps the irregular, cache unfriendly access pattern that
// makes this stone worth measuring separately.
constexpr float kMindRadius     = kChamberRadius * 1.30f;
// Cohesion is deliberately weak. Because the flock is now sealed in a chamber and the 27
// cell neighbourhood spans most of it, cohesion is effectively global: every particle pulls
// toward one point and nothing resists at range, so a strong term collapses the whole flock
// into a knot a tenth of the globe wide. The vortex term supplies what stops that, the same
// way a real flock or a galaxy resists collapse, by circulating rather than by pushing back.
constexpr float kMindCohesion   = 0.55f;
constexpr float kMindVortex     = 0.70f;
constexpr float kMindAlignment  = 2.10f;
constexpr float kMindSeparation = 0.30f;
// A reciprocal of a length, so it scales the other way from every other constant here.
constexpr float kMindSeparationFalloff = 27.0f;
constexpr float kMindSpeedLimit = 0.42f;

// Rodrigues rotation of a vector about a unit axis. Reality is the only caller, but keeping
// it out of the loop body keeps that loop readable.
inline Vec3 rotateAboutAxis(Vec3 v, Vec3 axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

// An orthonormal frame anchored to a portal mouth. Rebuilding it every frame is six stone
// sized operations, so it stays outside the particle loop and the loop just reads it.
void portalFrame(Vec3 center, Vec3& normal, Vec3& tangent, Vec3& bitangent) {
    normal = normalize(center);
    if (lengthSq(normal) < 0.5f) normal = {0.0f, 1.0f, 0.0f};

    const Vec3 reference =
        std::fabs(normal.y) > 0.95f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    tangent = normalize(cross(reference, normal));
    bitangent = cross(normal, tangent);
}

// Power is a broad, heavy shard with a broken shoulder and a flatter base. Soul is taller,
// narrower and more flame-like. Points run clockwise in screen space and both polygons
// contain the origin, which lets stoneShapeRadius intersect a ray with their edges.
constexpr StoneOutlinePoint kPowerOutline[] = {
    {-0.18f, -1.12f}, { 0.23f, -1.20f}, { 0.58f, -1.02f}, { 0.76f, -0.61f},
    { 0.70f, -0.23f}, { 0.88f,  0.18f}, { 0.70f,  0.68f}, { 0.35f,  1.00f},
    {-0.12f,  1.10f}, {-0.55f,  0.94f}, {-0.79f,  0.57f}, {-0.88f,  0.08f},
    {-0.75f, -0.48f}, {-0.50f, -0.91f},
};

constexpr StoneFacetSeed kPowerFacets[] = {
    {-0.48f, -0.64f, 0.74f}, { 0.02f, -0.82f, 1.20f}, { 0.48f, -0.48f, 0.88f},
    {-0.50f, -0.05f, 1.08f}, { 0.10f, -0.10f, 0.79f}, { 0.55f,  0.21f, 1.17f},
    {-0.31f,  0.59f, 0.82f}, { 0.25f,  0.68f, 1.06f},
};

// Space is a symmetric cut crystal with sharp poles, deliberately unlike Power's broad
// broken shard and Soul's lopsided flame, so the three finished stones read apart at a
// glance rather than looking like one shape in three colors.
constexpr StoneOutlinePoint kSpaceOutline[] = {
    { 0.00f, -1.20f}, { 0.34f, -0.79f}, { 0.63f, -0.43f}, { 0.81f,  0.02f},
    { 0.59f,  0.51f}, { 0.31f,  0.89f}, { 0.00f,  1.13f}, {-0.31f,  0.89f},
    {-0.59f,  0.51f}, {-0.81f,  0.02f}, {-0.63f, -0.43f}, {-0.34f, -0.79f},
};

// Seeds pushed out toward the poles and edges leave a bright core cell in the middle, which
// is what gives a cut stone its table facet.
constexpr StoneFacetSeed kSpaceFacets[] = {
    { 0.00f, -0.72f, 1.24f}, { 0.44f, -0.31f, 0.80f}, { 0.47f,  0.29f, 1.12f},
    { 0.00f,  0.64f, 0.75f}, {-0.47f,  0.29f, 1.16f}, {-0.44f, -0.31f, 0.84f},
    { 0.00f,  0.00f, 1.30f}, { 0.23f, -0.03f, 0.71f},
};

constexpr StoneOutlinePoint kSoulOutline[] = {
    {-0.10f, -1.36f}, { 0.25f, -1.25f}, { 0.37f, -0.94f}, { 0.55f, -0.68f},
    { 0.48f, -0.31f}, { 0.61f,  0.08f}, { 0.54f,  0.55f}, { 0.40f,  0.98f},
    { 0.15f,  1.27f}, {-0.20f,  1.32f}, {-0.43f,  1.08f}, {-0.55f,  0.66f},
    {-0.49f,  0.22f}, {-0.62f, -0.20f}, {-0.46f, -0.69f}, {-0.35f, -1.12f},
};

constexpr StoneFacetSeed kSoulFacets[] = {
    {-0.25f, -0.98f, 0.78f}, { 0.18f, -0.91f, 1.16f}, {-0.31f, -0.39f, 1.07f},
    { 0.23f, -0.43f, 0.83f}, {-0.05f,  0.02f, 1.22f}, { 0.35f,  0.29f, 0.76f},
    {-0.28f,  0.54f, 0.87f}, { 0.10f,  0.87f, 1.10f},
};

// Mind is a wide, squat hexagonal cut, the only stone broader than it is tall.
constexpr StoneOutlinePoint kMindOutline[] = {
    { 0.00f, -0.86f}, { 0.52f, -0.72f}, { 0.98f, -0.38f}, { 1.18f,  0.00f},
    { 0.98f,  0.38f}, { 0.52f,  0.72f}, { 0.00f,  0.86f}, {-0.52f,  0.72f},
    {-0.98f,  0.38f}, {-1.18f,  0.00f}, {-0.98f, -0.38f}, {-0.52f, -0.72f},
};

// Seeds laid out in horizontal bands, which reads as a wide table facet across the middle.
constexpr StoneFacetSeed kMindFacets[] = {
    {-0.72f, -0.30f, 0.80f}, { 0.00f, -0.44f, 1.18f}, { 0.72f, -0.30f, 0.88f},
    {-0.88f,  0.12f, 1.10f}, { 0.00f,  0.02f, 1.32f}, { 0.88f,  0.12f, 1.04f},
    {-0.44f,  0.52f, 0.76f}, { 0.46f,  0.52f, 0.84f},
};

// Reality is aether rather than a cut stone, so its outline is deliberately lopsided: the
// upper point is drawn out and pushed off centre, and the left flank bulges. It is the only
// silhouette here that is not close to symmetric about either axis.
constexpr StoneOutlinePoint kRealityOutline[] = {
    { 0.10f, -1.24f}, { 0.44f, -0.86f}, { 0.72f, -0.44f}, { 0.86f,  0.06f},
    { 0.74f,  0.52f}, { 0.44f,  0.88f}, { 0.02f,  1.06f}, {-0.40f,  0.94f},
    {-0.76f,  0.62f}, {-0.94f,  0.14f}, {-0.82f, -0.34f}, {-0.56f, -0.74f},
    {-0.24f, -1.02f},
};

// Uneven, off axis seeds so the interior swirls instead of resolving into clean planes,
// matching what the stone does to the particles around it.
constexpr StoneFacetSeed kRealityFacets[] = {
    {-0.34f, -0.74f, 1.14f}, { 0.30f, -0.60f, 0.78f}, {-0.62f, -0.16f, 0.86f},
    { 0.20f, -0.06f, 1.28f}, { 0.58f,  0.26f, 0.82f}, {-0.36f,  0.42f, 1.08f},
    { 0.12f,  0.66f, 0.75f}, {-0.06f,  0.14f, 1.20f},
};

// Time is a cushion cut: flat edges with pronounced corners at the diagonals. Corners reach
// 0.99 where the edge midpoints sit at 0.78, which is what keeps it reading as a square
// rather than collapsing back toward the circle the unfinished stones used.
constexpr StoneOutlinePoint kTimeOutline[] = {
    { 0.00f, -0.78f}, { 0.42f, -0.72f}, { 0.70f, -0.70f}, { 0.78f, -0.34f},
    { 0.80f,  0.00f}, { 0.78f,  0.34f}, { 0.70f,  0.70f}, { 0.42f,  0.72f},
    { 0.00f,  0.78f}, {-0.42f,  0.72f}, {-0.70f,  0.70f}, {-0.78f,  0.34f},
    {-0.80f,  0.00f}, {-0.78f, -0.34f}, {-0.70f, -0.70f}, {-0.42f, -0.72f},
};

// One bright centre cell ringed by alternating light and dark quadrants, so the face reads
// as a dial.
constexpr StoneFacetSeed kTimeFacets[] = {
    { 0.00f,  0.00f, 1.32f}, {-0.48f, -0.48f, 0.79f}, { 0.48f, -0.48f, 1.12f},
    { 0.48f,  0.48f, 0.81f}, {-0.48f,  0.48f, 1.10f}, { 0.00f, -0.60f, 0.94f},
    { 0.60f,  0.00f, 0.87f}, { 0.00f,  0.60f, 1.05f},
};

template <size_t N, size_t M>
void setVisual(Stone& stone, const StoneOutlinePoint (&outline)[N],
               const StoneFacetSeed (&facets)[M]) {
    static_assert(N <= kMaxStoneOutlinePoints, "stone outline storage is too small");
    static_assert(M <= kMaxStoneFacetSeeds, "stone facet storage is too small");
    stone.outlineCount = static_cast<int>(N);
    stone.facetSeedCount = static_cast<int>(M);
    std::copy(outline, outline + N, stone.outline);
    std::copy(facets, facets + M, stone.facetSeeds);
}

float cross2(StoneOutlinePoint a, StoneOutlinePoint b) {
    return a.x * b.y - a.y * b.x;
}

}  // namespace

float stoneShapeRadius(const Stone& stone, float angle) {
    if (stone.outlineCount < 3) return 1.0f;

    const StoneOutlinePoint ray = {std::cos(angle), std::sin(angle)};
    float nearest = 1000.0f;

    for (int i = 0; i < stone.outlineCount; ++i) {
        const StoneOutlinePoint a = stone.outline[i];
        const StoneOutlinePoint b = stone.outline[(i + 1) % stone.outlineCount];
        const StoneOutlinePoint edge = {b.x - a.x, b.y - a.y};
        const float denominator = cross2(ray, edge);
        if (std::fabs(denominator) < 1e-6f) continue;

        const float distance = cross2(a, edge) / denominator;
        const float alongEdge = cross2(a, ray) / denominator;
        if (distance >= 0.0f && alongEdge >= 0.0f && alongEdge <= 1.0f) {
            nearest = std::min(nearest, distance);
        }
    }

    // Authored outlines contain the origin, but retaining a safe circular fallback avoids
    // a broken render if a future stone is edited into a non-star-shaped polygon.
    return nearest < 1000.0f ? nearest : 1.0f;
}

float stoneSurfaceLighting(const Stone& stone, float x, float y) {
    if (stone.facetSeedCount < 2) return 1.0f;

    float nearest = 1000.0f;
    float second = 1000.0f;
    float planeLight = 1.0f;

    for (int i = 0; i < stone.facetSeedCount; ++i) {
        const StoneFacetSeed& seed = stone.facetSeeds[i];
        // A small shear stops the cells from looking like regular round bubbles and makes
        // their boundaries read as long crystal planes.
        const float dx = (x - seed.x) + 0.22f * (y - seed.y);
        const float dy = (y - seed.y) * 0.82f;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq < nearest) {
            second = nearest;
            nearest = distanceSq;
            planeLight = seed.light;
        } else if (distanceSq < second) {
            second = distanceSq;
        }
    }

    const float seam = std::exp(-std::fabs(second - nearest) * 32.0f);
    const float directional = 1.0f + 0.10f * (-0.65f * x - 0.35f * y);
    const float fineTexture = 0.035f * std::sin(x * 31.0f + y * 19.0f) +
                              0.025f * std::sin(x * 13.0f - y * 37.0f);
    const float light = planeLight * directional + seam * 0.34f + fineTexture;
    return std::max(0.48f, std::min(1.48f, light));
}

Stones::Stones() {
    m_stones.resize(6);
    for (size_t i = 0; i < 6; ++i) {
        m_stones[i].kind = static_cast<StoneKind>(i);
        m_stones[i].r = kStoneColor[i][0];
        m_stones[i].g = kStoneColor[i][1];
        m_stones[i].b = kStoneColor[i][2];
    }

    setVisual(m_stones[static_cast<size_t>(StoneKind::Power)], kPowerOutline, kPowerFacets);
    setVisual(m_stones[static_cast<size_t>(StoneKind::Soul)], kSoulOutline, kSoulFacets);
    setVisual(m_stones[static_cast<size_t>(StoneKind::Space)], kSpaceOutline, kSpaceFacets);
    setVisual(m_stones[static_cast<size_t>(StoneKind::Mind)], kMindOutline, kMindFacets);
    setVisual(m_stones[static_cast<size_t>(StoneKind::Reality)], kRealityOutline, kRealityFacets);
    setVisual(m_stones[static_cast<size_t>(StoneKind::Time)], kTimeOutline, kTimeFacets);
}

void Stones::update(double time, float dt, ParticleSystem& particles) {
    const double inCycle = std::fmod(time, kCycleDuration);
    const Phase previous = m_phase;

    if (inCycle < kContainedDuration) {
        m_phase = Phase::Contained;
        m_gather = 0.0f;
    } else if (inCycle < kContainedDuration + kConvergeDuration) {
        m_phase = Phase::Converge;
        m_gather = smoothstep01(
            static_cast<float>((inCycle - kContainedDuration) / kConvergeDuration));
    } else if (inCycle < kContainedDuration + kConvergeDuration + kImpactDuration) {
        m_phase = Phase::Impact;
        m_gather = 1.0f;
    } else {
        m_phase = Phase::Reform;
        const double into =
            inCycle - kContainedDuration - kConvergeDuration - kImpactDuration;
        m_gather = 1.0f - smoothstep01(static_cast<float>(into / kReformDuration));
    }

    // Crossing into Reform is the one moment particles change owner. Doing it on the
    // boundary rather than continuously means the re-sort happens once per cycle instead of
    // every frame, and gives the reform phase a fixed target to pull each particle toward.
    if (m_phase == Phase::Reform && previous != Phase::Reform) {
        reassignChambers(particles);
        ++m_cycleCount;
    }

    for (size_t i = 0; i < m_stones.size(); ++i) {
        const Vec3 home = orbitPosition(kOrbits[i], time, static_cast<int>(i));
        if (m_gather <= 0.0f) {
            m_stones[i].position = home;
        } else {
            const Vec3 gathered = impactPosition(kOrbits[i], time, static_cast<int>(i));
            m_stones[i].position = home + (gathered - home) * m_gather;
        }
    }

    for (ShockFront& front : m_shockFronts) front.age += dt;

    std::vector<ShockFront> alive;
    alive.reserve(m_shockFronts.size());
    for (const ShockFront& front : m_shockFronts) {
        if (front.age * kShockSpeed <= kShockMaxRadius) alive.push_back(front);
    }
    m_shockFronts = std::move(alive);

    if (time - m_lastShockSpawn >= kShockSpawnPeriod) {
        m_lastShockSpawn = time;
        m_shockFronts.push_back(ShockFront{});
    }

    m_releasing = std::fmod(time, kSoulReleasePeriod) < kSoulReleaseDuration;

    // Space does not merely orbit: every kSpaceJumpPeriod it blinks to a new offset from
    // its scripted path, hashed from a counter so the sequence is reproducible from the
    // run alone and identical in both execution modes.
    if (time - m_lastSpaceJump >= kSpaceJumpPeriod) {
        m_lastSpaceJump = time;
        ++m_spaceJumpCounter;

        uint32_t h = hashU32(m_spaceJumpCounter * 2654435761u);
        const float ox = randomSigned(h); h = hashU32(h);
        const float oy = randomSigned(h); h = hashU32(h);
        const float oz = randomSigned(h);
        m_spaceJumpOffset = Vec3{ox, oy * 0.55f, oz} * kSpaceJumpRange;
    }

    Stone& space = m_stones[static_cast<size_t>(StoneKind::Space)];
    space.position += m_spaceJumpOffset;

    // Both mouths are the same point mirrored through the chamber centre, so an entry
    // sitting on that centre puts them inside one another and a particle teleports into the
    // mouth it just left on every step. Push it off the centre first.
    // The mirror the exit is reflected through follows the stone in from its chamber, so
    // the wormhole keeps working while the walls are down instead of flinging particles back
    // to an empty globe.
    const Vec3 chamberHome = chamberCenter(static_cast<int>(StoneKind::Space));
    const Vec3 spaceHome = chamberHome + (Vec3{} - chamberHome) * m_gather;
    const Vec3 fromHome = space.position - spaceHome;
    const float homeDistance = length(fromHome);
    if (homeDistance < kSpaceMinOriginDistance) {
        const Vec3 direction =
            homeDistance > 1e-4f ? fromHome * (1.0f / homeDistance) : Vec3{1.0f, 0.0f, 0.0f};
        space.position = spaceHome + direction * kSpaceMinOriginDistance;
    }

    // The orbit and the jump offset together reach past the wall the particles bounce off,
    // so both mouths have to be pulled back inside it. An exit sitting outside the box
    // would emit particles into a region the integrator immediately clamps, pinning them
    // flat against the wall instead of letting them stream out of the ring. A whole mouth
    // radius of margin keeps the opening itself clear of the wall, not just its center.
    const float reach = (m_gather > 0.0f ? kImpactOrbitRadius + kChamberRadius * 0.4f
                                         : kChamberRadius) - kSpacePortalRadius;
    const Vec3 offset = space.position - spaceHome;
    const float offsetLength = length(offset);
    if (offsetLength > reach) space.position = spaceHome + offset * (reach / offsetLength);

    // The exit sits opposite the entry through the chamber centre, as far from it as the
    // globe allows. Mirroring a position that is already inside the chamber keeps the exit
    // inside it too, so nothing can be emitted through a wall.
    m_spaceExit = spaceHome + (spaceHome - space.position);

    m_spaceActivity += (m_spaceActivityTarget - m_spaceActivity) * kSpaceActivityEase;

    const double rewindPhase = std::fmod(time, kTimeRewindPeriod);
    m_rewinding = rewindPhase < kTimeRewindDuration;
    m_rewindProgress =
        m_rewinding ? static_cast<float>(rewindPhase / kTimeRewindDuration) : 0.0f;
}

// Snapshots every position into the ring's next slot. Parallel because each particle only
// writes its own element of the destination slot, and nothing reads the slot until a later
// frame.
void Stones::recordHistory(const ParticleSystem& particles) {
    const int count = particles.count();

    if (m_historyCount != count) {
        m_historyCount = count;
        m_historyFilled = 0;
        m_historyCursor = 0;
        const size_t total = static_cast<size_t>(count) * kHistorySlots;
        m_historyX.assign(total, 0.0f);
        m_historyY.assign(total, 0.0f);
        m_historyZ.assign(total, 0.0f);
    }

    const size_t base = static_cast<size_t>(m_historyCursor) * count;
    float* hx = m_historyX.data() + base;
    float* hy = m_historyY.data() + base;
    float* hz = m_historyZ.data() + base;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        hx[i] = particles.px[i];
        hy[i] = particles.py[i];
        hz[i] = particles.pz[i];
    }

    m_historyCursor = (m_historyCursor + 1) % kHistorySlots;
    if (m_historyFilled < kHistorySlots) ++m_historyFilled;
}

// The force pass is six loops rather than one. Each runs over a single chamber's
// contiguous range with exactly one stone's kernel inside it, so the six way gate that
// stood here before is gone entirely: no branch to mispredict, no divergence between
// neighbouring particles in a thread's chunk, and each loop touching one narrow slice of
// memory instead of striding across all of it.
//
// The gated single loop it replaces is still the honest comparison point, and step 21
// measures against it. Every loop is spread across all threads, which is what keeps the
// six wildly different kernel costs from landing on different threads; assigning a whole
// chamber to one thread is the other partitioning, and the one expected to lose.

void Stones::applyForces(ParticleSystem& particles, const SpatialGrid& grid, float dt) {
    if (m_phase != Phase::Contained) applyMergeDrift(particles, dt);

    applySoul(particles, dt);
    applyPower(particles, dt);
    applySpace(particles, dt);
    applyTime(particles);
    applyReality(particles, dt);
    applyMind(particles, grid, dt);

    if (++m_historyStep >= kHistoryStride) {
        m_historyStep = 0;
        if (!m_rewinding) recordHistory(particles);
    }
}

namespace {

// Every kernel opens and closes the same way, so the boilerplate lives here rather than
// being retyped six times with six chances to load one array and store another.
struct ChamberRange {
    int begin;
    int end;
};

inline ChamberRange rangeOf(const ParticleSystem& particles, StoneKind kind, bool contained) {
    // Walls down means ownership is suspended: every stone reaches every particle, so each
    // of the six kernels runs the full array. Six times the work of a contained frame, which
    // is why the merge and the contained phase have to be benchmarked separately rather than
    // averaged into one number.
    if (!contained) return {0, particles.count()};
    const int c = static_cast<int>(kind);
    return {particles.chamberBegin(c), particles.chamberEnd(c)};
}

}  // namespace

// Nothing in a stone's own power moves the cloud between chambers, so the merge needs a
// force of its own: inward while the stones gather, and out to each particle's newly
// assigned chamber while they reform.
void Stones::applyMergeDrift(ParticleSystem& particles, float dt) {
    const int count = particles.count();
    const bool reforming = m_phase == Phase::Reform;
    const float strength = reforming ? kReformPull : kConvergePull;

    Vec3 centers[kChamberCount];
    for (int c = 0; c < kChamberCount; ++c) centers[c] = chamberCenter(c);

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        const Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        // Reforming particles aim at spread points inside their globe, not all at its
        // centre. Aiming every one at a single point packs them onto the inner wall and
        // leaves the chamber visibly lopsided for most of the contained phase afterwards.
        Vec3 target;
        if (reforming) {
            uint32_t h = hashCombine(m_cycleCount, static_cast<uint32_t>(i));
            const float ox = randomSigned(h); h = hashU32(h);
            const float oy = randomSigned(h); h = hashU32(h);
            const float oz = randomSigned(h);
            target = centers[particles.chamber[i]] +
                     Vec3{ox, oy, oz} * (kChamberRadius * kReformSpread);
        }

        const Vec3 toTarget = target - pos;
        const float distance = length(toTarget);
        if (distance < 1e-4f) continue;

        // Pull is capped rather than proportional to distance. Proportional would launch a
        // particle on the far side of the ring at several times the speed of one already
        // near the target, and the cloud would arrive as a shell rather than a stream.
        const float reach = distance > 1.0f ? 1.0f : distance;
        const Vec3 direction = toTarget * (1.0f / distance);

        particles.vx[i] += direction.x * (strength * reach * dt);
        particles.vy[i] += direction.y * (strength * reach * dt);
        particles.vz[i] += direction.z * (strength * reach * dt);
    }
}

// Every particle is handed to a new chamber at the moment the reform begins. Round robin by
// index rather than nearest chamber: at this instant the whole cloud is bunched at the
// centre, so nearest would hand most of it to whichever globe happened to be closest and
// leave the rest nearly empty. Round robin keeps the six blocks equal, which keeps the per
// chamber work equal, and it draws from every old chamber evenly so the colours genuinely
// reshuffle instead of rotating as six intact blocks.
void Stones::reassignChambers(ParticleSystem& particles) {
    const int count = particles.count();

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        const int owner = static_cast<int>(
            (static_cast<uint32_t>(i) + m_cycleCount) % static_cast<uint32_t>(kChamberCount));
        particles.chamber[i] = static_cast<uint8_t>(owner);
        particles.cr[i] = kStoneColor[owner][0];
        particles.cg[i] = kStoneColor[owner][1];
        particles.cb[i] = kStoneColor[owner][2];
        particles.captured[i] = 0;
        particles.timeScale[i] = 1.0f;
    }

    particles.regroup();

    // regroup permutes every array, so any index held outside ParticleSystem is now stale.
    // The position history is exactly that, and reading it after this point would rewind
    // particles onto paths that belonged to different particles.
    m_historyFilled = 0;
    m_historyCursor = 0;
    m_historyStep = 0;
}

void Stones::applySoul(ParticleSystem& particles, float dt) {
    const ChamberRange range = rangeOf(particles, StoneKind::Soul, m_phase == Phase::Contained);
    const Vec3 soulPos = stone(StoneKind::Soul).position;
    const float localDt = dt;

    int capturedCount = 0;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static) \
        reduction(+:capturedCount)
    for (int i = range.begin; i < range.end; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        Vec3 vel = {particles.vx[i], particles.vy[i], particles.vz[i]};

        // Soul
        {
            const Vec3 toStone = soulPos - pos;
            const float r = length(toStone);

            const bool releasing = m_releasing;
            if (releasing) {
                particles.captured[i] = 0;
            } else if (r < kSoulCaptureRadius) {
                particles.captured[i] = 1;
            }

            if (particles.captured[i]) {
                const Vec3 fromStone = pos - soulPos;
                const float radius = r > kSoulSoftening ? r : kSoulSoftening;
                const Vec3 radialDir = fromStone * (1.0f / radius);

                const float vRadialMag = dot(vel, radialDir);
                const Vec3 vRadial = radialDir * vRadialMag;
                const Vec3 vTangential = vel - vRadial;
                const float tangentialMag = length(vTangential);

                const float orbitSpeed = std::sqrt(kSoulG / radius);
                const float damp =
                    kOrbitDamping * localDt < 1.0f ? kOrbitDamping * localDt : 1.0f;

                const float newRadialMag = vRadialMag * (1.0f - damp);
                float newTangentialMag = tangentialMag;
                if (tangentialMag > 1e-5f) {
                    newTangentialMag += (orbitSpeed - tangentialMag) * damp;
                    vel = radialDir * newRadialMag +
                          (vTangential * (1.0f / tangentialMag)) * newTangentialMag;
                } else {
                    vel = radialDir * newRadialMag;
                }
            } else if (r < kSoulOuterRadius) {
                const float r2 = r * r + kSoulSoftening * kSoulSoftening;
                const float accelMag = kSoulG / r2;
                const Vec3 dir = toStone * (1.0f / (r > 1e-5f ? r : 1e-5f));
                vel += dir * (accelMag * localDt);
            }

            if (particles.captured[i]) ++capturedCount;
        }

        particles.vx[i] = vel.x;
        particles.vy[i] = vel.y;
        particles.vz[i] = vel.z;
    }

    m_soulCapturedCount = capturedCount;
}

void Stones::applyPower(ParticleSystem& particles, float dt) {
    const ChamberRange range = rangeOf(particles, StoneKind::Power, m_phase == Phase::Contained);
    const Vec3 powerPos = stone(StoneKind::Power).position;
    const int frontCount = static_cast<int>(m_shockFronts.size());
    const ShockFront* fronts = m_shockFronts.data();
    const float localDt = dt;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = range.begin; i < range.end; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        Vec3 vel = {particles.vx[i], particles.vy[i], particles.vz[i]};

        // Power
        if (frontCount > 0) {
            const Vec3 toStone = pos - powerPos;
            const float r = length(toStone);
            if (r > 1e-5f) {
                const Vec3 dir = toStone * (1.0f / r);
                for (int f = 0; f < frontCount; ++f) {
                    const float frontRadius = fronts[f].age * kShockSpeed;
                    const float delta = r - frontRadius;
                    const float shell =
                        std::exp(-(delta * delta) / (2.0f * kShockShellWidth * kShockShellWidth));
                    vel += dir * (kShockImpulse * shell * localDt);
                }
            }
        }

        particles.vx[i] = vel.x;
        particles.vy[i] = vel.y;
        particles.vz[i] = vel.z;
    }
}

void Stones::applySpace(ParticleSystem& particles, float dt) {
    const ChamberRange range = rangeOf(particles, StoneKind::Space, m_phase == Phase::Contained);
    const Vec3 spaceEntry = stone(StoneKind::Space).position;
    const Vec3 spaceExit = m_spaceExit;
    const float localDt = dt;

    Vec3 entryN, entryT, entryB;
    Vec3 exitN, exitT, exitB;
    portalFrame(spaceEntry - chamberCenter(static_cast<int>(StoneKind::Space)), entryN, entryT, entryB);
    portalFrame(spaceExit - chamberCenter(static_cast<int>(StoneKind::Space)), exitN, exitT, exitB);

    int teleportCount = 0;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static) \
        reduction(+:teleportCount)
    for (int i = range.begin; i < range.end; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        Vec3 vel = {particles.vx[i], particles.vy[i], particles.vz[i]};

        // Space: the funnel. Everything within the inflow radius is drawn toward the entry
        // and given a sideways kick around the portal axis, so the cloud visibly spirals
        // into the mouth. Velocity is carried through the portal unchanged, so the same
        // motion becomes a spray on the far side and the two events read as one.
        {
            const Vec3 toEntry = spaceEntry - pos;
            const float distanceSq = lengthSq(toEntry);
            if (distanceSq < kSpaceInflowRadius * kSpaceInflowRadius && distanceSq > 1e-8f) {
                const float distance = std::sqrt(distanceSq);
                const Vec3 inward = toEntry * (1.0f / distance);

                const float closeness = 1.0f - distance / kSpaceInflowRadius;
                const float ramp = closeness * closeness;

                vel += inward * (kSpaceInflowPull * ramp * localDt);
                vel += cross(entryN, inward) * (kSpaceInflowSwirl * ramp * localDt);
            }
        }

        // Space: crossing the entry mouth moves the particle to the exit and re-expresses
        // both offset and velocity in the exit's frame, so it leaves the far mouth pointing
        // the way it went in relative to the portal rather than snapping to a fixed heading.
        {
            const Vec3 relative = pos - spaceEntry;
            if (lengthSq(relative) < kSpacePortalRadius * kSpacePortalRadius) {
                const float a = dot(relative, entryN);
                const float b = dot(relative, entryT);
                const float c = dot(relative, entryB);
                pos = spaceExit + exitN * a + exitT * b + exitB * c;

                const float va = dot(vel, entryN);
                const float vb = dot(vel, entryT);
                const float vc = dot(vel, entryB);
                vel = exitN * va + exitT * vb + exitB * vc;

                ++teleportCount;
            }
        }

        particles.px[i] = pos.x;
        particles.py[i] = pos.y;
        particles.pz[i] = pos.z;
        particles.vx[i] = vel.x;
        particles.vy[i] = vel.y;
        particles.vz[i] = vel.z;
    }

    m_spaceTeleportCount = teleportCount;

    const int size = range.end - range.begin;
    const float share = size > 0 ? static_cast<float>(teleportCount) / size : 0.0f;
    const float scaled = share * kSpaceActivityScale;
    m_spaceActivityTarget = scaled > 1.0f ? 1.0f : scaled;
}

// Time applies no force of its own: it sets the step every other kernel would integrate
// against, and drives the rewind. There is nothing to scale by dt here, which is why it is
// the one kernel that does not take it.
void Stones::applyTime(ParticleSystem& particles) {
    const ChamberRange range = rangeOf(particles, StoneKind::Time, m_phase == Phase::Contained);
    const Vec3 timePos = stone(StoneKind::Time).position;
    const int count = particles.count();

    const bool rewinding = m_rewinding && m_historyFilled > 1;
    const float* newerX = nullptr; const float* newerY = nullptr; const float* newerZ = nullptr;
    const float* olderX = nullptr; const float* olderY = nullptr; const float* olderZ = nullptr;
    float rewindBlend = 0.0f;
    if (rewinding) {
        const int newest = (m_historyCursor - 1 + kHistorySlots) % kHistorySlots;
        const float reach = m_rewindProgress * (m_historyFilled - 1);
        const int stepBack = static_cast<int>(reach);
        const int stepBackNext = std::min(stepBack + 1, m_historyFilled - 1);
        rewindBlend = reach - static_cast<float>(stepBack);

        const size_t a = static_cast<size_t>((newest - stepBack + kHistorySlots * 2) %
                                             kHistorySlots) * count;
        const size_t b = static_cast<size_t>((newest - stepBackNext + kHistorySlots * 2) %
                                             kHistorySlots) * count;
        newerX = m_historyX.data() + a; newerY = m_historyY.data() + a; newerZ = m_historyZ.data() + a;
        olderX = m_historyX.data() + b; olderY = m_historyY.data() + b; olderZ = m_historyZ.data() + b;
    }

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = range.begin; i < range.end; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};

        // Time: everything below integrates against this particle's own dilated step, not
        // the global one, so a slowed particle is slowed for every stone at once rather
        // than only for its own motion.
        float scale = 1.0f;
        {
            const Vec3 toTime = pos - timePos;
            const float distanceSq = lengthSq(toTime);
            if (distanceSq < kTimeBubbleRadius * kTimeBubbleRadius) {
                const float t = std::sqrt(distanceSq) / kTimeBubbleRadius;
                const float smooth = t * t * (3.0f - 2.0f * t);
                scale = kTimeMinScale + (1.0f - kTimeMinScale) * smooth;
            }
        }

        particles.timeScale[i] = scale;

        if (rewinding) {
            const Vec3 toTime = pos - timePos;
            if (lengthSq(toTime) < kTimeBubbleRadius * kTimeBubbleRadius) {
                const Vec3 newer = {newerX[i], newerY[i], newerZ[i]};
                const Vec3 older = {olderX[i], olderY[i], olderZ[i]};
                pos = newer + (older - newer) * rewindBlend;

                // The particle is being driven from the record, so its own velocity must
                // not advance it as well; without this it drifts forward between slots and
                // the reversal cancels itself out.
                particles.timeScale[i] = 0.0f;
            }
        }

        particles.px[i] = pos.x;
        particles.py[i] = pos.y;
        particles.pz[i] = pos.z;
    }
}

void Stones::applyReality(ParticleSystem& particles, float dt) {
    const ChamberRange range = rangeOf(particles, StoneKind::Reality, m_phase == Phase::Contained);
    const Vec3 realityPos = stone(StoneKind::Reality).position;

    // Twists about its own orbit tangent, so the axis sweeps as the stone travels and the
    // warp never settles into the same orientation twice.
    const Vec3 realityAxis =
        normalize(cross(realityPos - chamberCenter(static_cast<int>(StoneKind::Reality)),
                        Vec3{0.0f, 1.0f, 0.0f}));
    const float localDt = dt;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = range.begin; i < range.end; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        Vec3 vel = {particles.vx[i], particles.vy[i], particles.vz[i]};

        {
            const Vec3 offset = pos - realityPos;
            const float distanceSq = lengthSq(offset);
            if (distanceSq < kRealityRadius * kRealityRadius) {
                const float falloff = 1.0f - std::sqrt(distanceSq) / kRealityRadius;
                const float angle = kRealityTwist * falloff * falloff * localDt;

                pos = realityPos + rotateAboutAxis(offset, realityAxis, angle);
                vel = rotateAboutAxis(vel, realityAxis, angle);

                const float tint = kRealityTintRate * falloff * localDt;
                const float blend = tint > 1.0f ? 1.0f : tint;
                particles.cr[i] += (kRealityTint[0] - particles.cr[i]) * blend;
                particles.cg[i] += (kRealityTint[1] - particles.cg[i]) * blend;
                particles.cb[i] += (kRealityTint[2] - particles.cb[i]) * blend;
            }
        }

        particles.px[i] = pos.x;
        particles.py[i] = pos.y;
        particles.pz[i] = pos.z;
        particles.vx[i] = vel.x;
        particles.vy[i] = vel.y;
        particles.vz[i] = vel.z;
    }
}

void Stones::applyMind(ParticleSystem& particles, const SpatialGrid& grid, float dt) {
    const ChamberRange range = rangeOf(particles, StoneKind::Mind, m_phase == Phase::Contained);
    const Vec3 mindPos = stone(StoneKind::Mind).position;
    const int gridDim = grid.dim();
    const float localDt = dt;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = range.begin; i < range.end; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        Vec3 vel = {particles.vx[i], particles.vy[i], particles.vz[i]};

        {
            const Vec3 toMind = pos - mindPos;
            if (lengthSq(toMind) < kMindRadius * kMindRadius) {
                const int cx = grid.axisCell(pos.x);
                const int cy = grid.axisCell(pos.y);
                const int cz = grid.axisCell(pos.z);

                Vec3 neighbourPosition;
                Vec3 neighbourVelocity;
                int neighbourCount = 0;

                for (int z = cz - 1; z <= cz + 1; ++z) {
                    if (z < 0 || z >= gridDim) continue;
                    for (int y = cy - 1; y <= cy + 1; ++y) {
                        if (y < 0 || y >= gridDim) continue;
                        for (int x = cx - 1; x <= cx + 1; ++x) {
                            if (x < 0 || x >= gridDim) continue;

                            const CellSummary& c = grid.cell(grid.cellIndex(x, y, z));
                            if (c.count == 0) continue;

                            const float weight = static_cast<float>(c.count);
                            neighbourPosition += c.meanPosition * weight;
                            neighbourVelocity += c.meanVelocity * weight;
                            neighbourCount += c.count;
                        }
                    }
                }

                if (neighbourCount > 0) {
                    const float inverse = 1.0f / static_cast<float>(neighbourCount);
                    const Vec3 flockCenter = neighbourPosition * inverse;
                    const Vec3 flockHeading = neighbourVelocity * inverse;

                    const Vec3 toCenter = flockCenter - pos;
                    vel += toCenter * (kMindCohesion * localDt);
                    vel += (flockHeading - vel) * (kMindAlignment * localDt);
                    vel += cross(Vec3{0.0f, 1.0f, 0.0f}, toCenter) * (kMindVortex * localDt);

                    const CellSummary& own = grid.cell(grid.cellIndex(cx, cy, cz));
                    if (own.count > 1) {
                        const Vec3 crowding = pos - own.meanPosition;
                        const float spread = length(crowding);
                        if (spread > 1e-4f) {
                            // Bounded rather than an inverse square. A 1/r term inside a
                            // cell this small peaks around a hundred times the alignment
                            // term and shoves every particle a different way, which wipes
                            // out the alignment the flock is supposed to be built on.
                            const float crowd = 1.0f / (1.0f + spread * kMindSeparationFalloff);
                            vel += crowding * (kMindSeparation * crowd * localDt / spread);
                        }
                    }

                    // Flocking is a positive feedback loop: alignment pulls a particle
                    // toward the group's velocity, which then raises the group's velocity.
                    // Without a ceiling the whole flock accelerates until it is a streak.
                    const float speed = length(vel);
                    if (speed > kMindSpeedLimit) vel *= kMindSpeedLimit / speed;
                }
            }
        }

        particles.vx[i] = vel.x;
        particles.vy[i] = vel.y;
        particles.vz[i] = vel.z;
    }
}
