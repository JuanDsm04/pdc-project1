#pragma once

#include <cmath>
#include <cstdint>

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr float kPi = 3.14159265f;

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline Vec3 operator*(float s, Vec3 v) { return v * s; }
inline Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }

inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
inline Vec3& operator*=(Vec3& v, float s) { v = v * s; return v; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float lengthSq(Vec3 v) { return dot(v, v); }
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : Vec3{};
}

// Randomness is derived by hashing an index rather than advancing a shared generator.
// A stateless hash lets any thread produce particle i's values without coordinating with
// the others, and it makes a run reproducible from its seed alone, which is what allows
// the sequential and parallel modes to be compared frame for frame.
inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline uint32_t hashCombine(uint32_t a, uint32_t b) {
    return hashU32(a ^ (b * 0x9e3779b9U));
}

// Uses the top bits, which are the best mixed, and lands in [0,1).
inline float randomFloat(uint32_t h) {
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

inline float randomSigned(uint32_t h) { return randomFloat(h) * 2.0f - 1.0f; }