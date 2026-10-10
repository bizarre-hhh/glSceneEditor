#ifndef MODEL_TRANSFORM_H
#define MODEL_TRANSFORM_H

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>

enum class TransformMode { Translate, Rotate, Scale };

struct ModelTransform
{
    glm::vec3 translation_mm{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 rotation_degrees{0.0f};
    bool operator==(const ModelTransform&) const = default;
};

namespace ModelTransformMath
{
inline bool IsValid(const ModelTransform& transform)
{
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(transform.translation_mm[axis]) ||
            !std::isfinite(transform.rotation_degrees[axis]) ||
            !std::isfinite(transform.scale[axis]) || transform.scale[axis] <= 0)
            return false;
    return true;
}

inline glm::mat4 Rotation(const glm::vec3& degrees)
{
    const auto radians = glm::radians(degrees);
    return glm::eulerAngleXYZ(radians.x, radians.y, radians.z);
}

// The existing file format stores displacement along the rotated model axes.
// Keep that convention and expose a conventional centered TRS matrix to gizmos.
inline glm::mat4 GizmoMatrix(const ModelTransform& transform, const glm::vec3& pivot_world)
{
    const auto rotation = Rotation(transform.rotation_degrees);
    const glm::vec3 center = pivot_world + glm::mat3(rotation) * (transform.translation_mm / 10.0f);
    return glm::translate(glm::mat4(1), center) * rotation *
           glm::scale(glm::mat4(1), transform.scale);
}

inline ModelTransform RotateKeepingCenter(ModelTransform transform, const glm::vec3& degrees)
{
    const glm::vec3 offset = glm::mat3(Rotation(transform.rotation_degrees)) * transform.translation_mm;
    transform.rotation_degrees = degrees;
    transform.translation_mm = glm::transpose(glm::mat3(Rotation(degrees))) * offset;
    return transform;
}

inline glm::vec3 ClosestEuler(const glm::mat4& rotation, const glm::vec3& previous)
{
    glm::vec3 angles;
    glm::extractEulerAngleXYZ(rotation, angles.x, angles.y, angles.z);
    angles = glm::degrees(angles);
    glm::vec3 alternative(angles.x + 180.0f, 180.0f - angles.y, angles.z + 180.0f);
    for (int axis = 0; axis < 3; ++axis)
    {
        angles[axis] += 360.0f * std::round((previous[axis] - angles[axis]) / 360.0f);
        alternative[axis] += 360.0f * std::round((previous[axis] - alternative[axis]) / 360.0f);
    }
    return glm::length(alternative - previous) < glm::length(angles - previous) ? alternative : angles;
}

inline bool FromGizmo(const glm::mat4& matrix, const glm::vec3& pivot_world,
                      TransformMode mode, ModelTransform& transform)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    if (mode == TransformMode::Scale)
    {
        for (int axis = 0; axis < 3; ++axis)
            transform.scale[axis] = glm::length(glm::vec3(matrix[axis]));
    }
    else
    {
        if (mode == TransformMode::Rotate)
        {
            glm::mat4 rotation(1);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float length = glm::length(glm::vec3(matrix[axis]));
                if (length < 1.0e-8f) return false;
                rotation[axis] = glm::vec4(glm::vec3(matrix[axis]) / length, 0);
            }
            transform.rotation_degrees = ClosestEuler(rotation, transform.rotation_degrees);
        }
        transform.translation_mm = glm::transpose(glm::mat3(Rotation(transform.rotation_degrees))) *
            (glm::vec3(matrix[3]) - pivot_world) * 10.0f;
    }
    return IsValid(transform);
}

inline bool NearlyEqual(const ModelTransform& a, const ModelTransform& b)
{
    return glm::all(glm::lessThanEqual(glm::abs(a.translation_mm - b.translation_mm), glm::vec3(0.0001f))) &&
           glm::all(glm::lessThanEqual(glm::abs(a.rotation_degrees - b.rotation_degrees), glm::vec3(0.0001f))) &&
           glm::all(glm::lessThanEqual(glm::abs(a.scale - b.scale), glm::vec3(0.000001f)));
}
} // namespace ModelTransformMath

#endif
