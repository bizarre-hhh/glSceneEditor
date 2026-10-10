#ifndef PLATE_DIMENSIONS_H
#define PLATE_DIMENSIONS_H

#include <cmath>
#include <glm/vec2.hpp>

namespace PlateDimensions
{
constexpr float kDefaultMm = 100.0f;
constexpr float kMinMm = 1.0f;
constexpr float kMaxMm = 10000.0f;
inline bool IsValid(const glm::vec2& size_mm)
{
    return std::isfinite(size_mm.x) && std::isfinite(size_mm.y) &&
        size_mm.x >= kMinMm && size_mm.x <= kMaxMm &&
        size_mm.y >= kMinMm && size_mm.y <= kMaxMm;
}
} // namespace PlateDimensions

#endif
