#pragma once

#include "math3d.hpp"

struct Projected {
    float x = 0.0f;      // screen pixels
    float y = 0.0f;
    float depth = 0.0f;  // distance along the view axis
    float scale = 0.0f;  // multiply a world radius by this to get a pixel radius
    bool  visible = false;
};

// Pinhole camera on a slow orbit around the origin. There is no matrix stack because the
// projection is only ever needed one point at a time inside the hot loop, so carrying an
// orthonormal basis and three dot products is cheaper than a full matrix multiply.
class Camera {
public:
    void setViewport(int width, int height, float verticalFovDegrees = 60.0f);
    // gather is 0 with the stones in their chambers and 1 with them at the centre. The
    // camera closes in as it rises so the merge fills the frame.
    void update(double time, float gather);

    Projected project(Vec3 world) const;

    Vec3 position() const { return m_position; }

private:
    Vec3 m_position;
    Vec3 m_right;
    Vec3 m_up;
    Vec3 m_forward;

    float m_focal = 0.0f;
    float m_halfWidth = 0.0f;
    float m_halfHeight = 0.0f;
};
