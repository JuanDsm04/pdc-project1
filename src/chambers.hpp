#pragma once

#include "math3d.hpp"

// Six spherical chambers on a ring, one stone to each. Confining a stone's particles to its
// own chamber is what makes the six powers legible: in a single shared volume every particle
// was subject to every stone at once, six forces superposed into generic drift, and no
// particle belonged to anything.
constexpr int kChamberCount = 6;

// Adjacent centres sit exactly kRingRadius apart, so the gap between two chamber surfaces is
// kRingRadius minus two radii, currently 0.5. Wide enough that neighbouring globes never
// visually merge.
constexpr float kRingRadius    = 1.6f;
constexpr float kChamberRadius = 0.55f;

// Everything the ring occupies, used to size the spatial grid and place the camera.
constexpr float kRingExtent = kRingRadius + kChamberRadius;

// Canonical stone colours, indexed by StoneKind. Defined here rather than in stones.cpp
// because a particle's colour is now its chamber membership, so the stones and the particle
// system have to agree on this exactly or colour stops meaning ownership.
constexpr float kStoneColor[kChamberCount][3] = {
    {0.20f, 0.45f, 1.00f},  // space
    {1.00f, 0.85f, 0.15f},  // mind
    {1.00f, 0.15f, 0.20f},  // reality
    {0.45f, 0.12f, 1.00f},  // power
    {0.25f, 1.00f, 0.40f},  // time
    {1.00f, 0.32f, 0.03f},  // soul
};

inline Vec3 chamberCenter(int index) {
    const float angle = static_cast<float>(index) * (6.28318531f / kChamberCount);
    return {std::cos(angle) * kRingRadius, 0.0f, std::sin(angle) * kRingRadius};
}
