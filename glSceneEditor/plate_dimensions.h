#ifndef PLATE_DIMENSIONS_H
#define PLATE_DIMENSIONS_H

#include <cmath>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace PlateDimensions
{
constexpr float kDefaultMm = 100.0f;
constexpr float kDefaultHeightMm = 100.0f;
constexpr float kMinMm = 1.0f;
constexpr float kMaxMm = 10000.0f;

inline bool IsValidValue(float value)
{
    return std::isfinite(value) && value >= kMinMm && value <= kMaxMm;
}

inline bool IsValid(const glm::vec2& size_mm)
{
    return IsValidValue(size_mm.x) && IsValidValue(size_mm.y);
}

// X/Y size the plate; Z is the build height, stored and edited with the
// same limits but not drawn in the scene.
inline bool IsValid(const glm::vec3& size_mm)
{
    return IsValidValue(size_mm.x) && IsValidValue(size_mm.y) &&
        IsValidValue(size_mm.z);
}

inline glm::vec3 DefaultSizeMm()
{
    return glm::vec3(kDefaultMm, kDefaultMm, kDefaultHeightMm);
}
} // namespace PlateDimensions

#endif
