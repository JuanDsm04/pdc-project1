#include "camera.hpp"

namespace {

// Points nearer than this along the view axis are rejected. Without it the perspective
// divide explodes as a point crosses the eye plane and a single particle smears across
// the whole screen.
constexpr float kNearPlane = 0.05f;

constexpr float kOrbitRadius = 5.4f;
constexpr float kOrbitSpeed  = 0.11f;

}  // namespace

void Camera::setViewport(int width, int height, float verticalFovDegrees) {
    m_halfWidth  = width * 0.5f;
    m_halfHeight = height * 0.5f;

    const float fovRadians = verticalFovDegrees * 3.14159265f / 180.0f;
    m_focal = m_halfHeight / std::tan(fovRadians * 0.5f);
}

void Camera::update(double time) {
    const float angle = static_cast<float>(time * kOrbitSpeed);
    // Enough tilt to see the ring as a ring rather than edge on, without ever looking
    // straight down at it.
    const float height = 1.5f + 0.7f * std::sin(static_cast<float>(time * 0.07));

    m_position = {std::cos(angle) * kOrbitRadius, height, std::sin(angle) * kOrbitRadius};

    m_forward = normalize(-m_position);
    m_right   = normalize(cross(m_forward, Vec3{0.0f, 1.0f, 0.0f}));
    m_up      = cross(m_right, m_forward);
}

Projected Camera::project(Vec3 world) const {
    const Vec3 relative = world - m_position;
    const float depth = dot(relative, m_forward);

    Projected out;
    if (depth <= kNearPlane) return out;

    const float invDepth = 1.0f / depth;
    const float scale = m_focal * invDepth;

    out.x = m_halfWidth + dot(relative, m_right) * scale;
    out.y = m_halfHeight - dot(relative, m_up) * scale;
    out.depth = depth;
    out.scale = scale;
    out.visible = true;
    return out;
}
