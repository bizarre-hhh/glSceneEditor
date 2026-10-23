#ifndef CAMERA_H
#define CAMERA_H

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// An orbit camera keeps a point of interest in view while it rotates and zooms.
class Camera
{
public:
    glm::mat4 GetViewMatrix() const
    {
        return glm::lookAt(Position(), target_, UpDirection());
    }

    glm::mat4 GetProjectionMatrix(float aspect) const
    {
        const float near_plane = std::max(0.01f, distance_ / 1000.0f);
        const float far_plane = std::max(100.0f, distance_ * 4.0f +
                                                scene_radius_ * 2.0f);
        if (orthographic_)
        {
            // Match the perspective scale at the orbit target while removing
            // foreshortening, so the XY plate is edge-on in standard side views.
            const float half_height = distance_ * std::tan(glm::radians(zoom_) * 0.5f);
            const float half_width = half_height * aspect;
            return glm::ortho(-half_width, half_width, -half_height, half_height,
                              near_plane, far_plane);
        }
        return glm::perspective(glm::radians(zoom_), aspect, near_plane, far_plane);
    }

    bool IsOrthographic() const { return orthographic_; }
    void SetOrthographic(bool enabled) { orthographic_ = enabled; }

    bool IsAxisVisible(const glm::vec3& direction) const
    {
        if (!orthographic_)
            return true;
        const glm::vec3 view_direction = glm::normalize(target_ - Position());
        // An axis parallel to the sightline collapses to a point in an
        // orthographic view. Suppress both its solid arrow and its label.
        return std::abs(glm::dot(view_direction, glm::normalize(direction))) < 0.99999f;
    }

    glm::vec3 Position() const
    {
        const float yaw = glm::radians(yaw_);
        const float pitch = glm::radians(pitch_);
        const float horizontal = distance_ * std::cos(pitch);
        return target_ + glm::vec3(horizontal * std::cos(yaw),
                                   horizontal * std::sin(yaw),
                                   distance_ * std::sin(pitch));
    }

    // An explicit orbit basis stays well-defined for exact top/bottom views.
    glm::vec3 UpDirection() const
    {
        const float yaw = glm::radians(yaw_);
        const float pitch = glm::radians(pitch_);
        return glm::vec3(-std::cos(yaw) * std::sin(pitch),
                         -std::sin(yaw) * std::sin(pitch), std::cos(pitch));
    }

    const glm::vec3& Target() const { return target_; }
    float Distance() const { return distance_; }
    float Yaw() const { return yaw_; }
    float Pitch() const { return pitch_; }
    float Zoom() const { return zoom_; }
    float SceneRadius() const { return scene_radius_; }

    void SetTarget(const glm::vec3& target) { target_ = target; }
    void SetDistance(float distance)
    {
        distance_ = glm::clamp(distance, kMinDistance, max_distance_);
    }
    // Home framing uses this to bound how far the user can zoom out.
    void SetMaxDistance(float distance)
    {
        max_distance_ = glm::clamp(distance, kMinDistance, kMaxDistance);
    }
    void ClearMaxDistance() { max_distance_ = kMaxDistance; }
    void SetZoom(float zoom) { zoom_ = glm::clamp(zoom, 1.0f, 90.0f); }
    void SetRotation(float yaw, float pitch)
    {
        yaw_ = yaw;
        pitch_ = glm::clamp(pitch, -90.0f, 90.0f);
        orthographic_ = false;
    }

    void Orbit(float x_pixels, float y_pixels)
    {
        if (x_pixels == 0.0f && y_pixels == 0.0f)
            return;
        SetRotation(yaw_ + x_pixels * 0.25f,
                    pitch_ - y_pixels * 0.25f);
    }

    // reference_distance is the view-space depth of the surface being
    // dragged, so the grabbed point follows the mouse exactly. The orbit
    // distance is only a fallback when there is no surface under the cursor.
    void Pan(float x_pixels, float y_pixels, float reference_distance,
             int viewport_height)
    {
        if (viewport_height <= 0 || reference_distance <= 0.0f)
            return;
        const glm::vec3 front = glm::normalize(target_ - Position());
        const glm::vec3 right = glm::normalize(
            glm::cross(front, UpDirection()));
        const glm::vec3 up = glm::cross(right, front);
        const float world_units_per_pixel =
            2.0f * reference_distance * std::tan(glm::radians(zoom_) * 0.5f) /
            static_cast<float>(viewport_height);
        target_ += (-right * x_pixels + up * y_pixels) * world_units_per_pixel;
    }

    void Dolly(float wheel_steps)
    {
        SetDistance(distance_ * std::pow(kWheelZoomStep, wheel_steps));
    }

    // Zoom toward a point on the scene instead of the fixed orbit target.
    // Both the camera and the target move toward the anchor so it stays under
    // the cursor and keeps growing at the wheel's exponential rate. Zooming
    // toward a target in front of the geometry would slow down and stall, as
    // the camera can never pass it.
    void DollyTowards(float wheel_steps, const glm::vec3& anchor)
    {
        if (orthographic_)
        {
            Dolly(wheel_steps);
            return;
        }
        const float next_distance = glm::clamp(
            distance_ * std::pow(kWheelZoomStep, wheel_steps),
            kMinDistance, max_distance_);
        const float applied = next_distance / distance_;
        target_ += (1.0f - applied) * (anchor - target_);
        distance_ = next_distance;
    }

    void FrameBounds(const glm::vec3& minimum, const glm::vec3& maximum,
                     float aspect)
    {
        target_ = (minimum + maximum) * 0.5f;
        scene_radius_ = std::max(glm::length((maximum - minimum) * 0.5f), 0.5f);
        const float vertical_half_fov = glm::radians(zoom_) * 0.5f;
        const float horizontal_half_fov = std::atan(
            std::tan(vertical_half_fov) * std::max(aspect, 0.01f));
        const float limiting_half_fov =
            std::min(vertical_half_fov, horizontal_half_fov);
        SetDistance(1.15f * scene_radius_ / std::sin(limiting_half_fov));
    }

private:
    static constexpr float kMinDistance = 0.05f;
    static constexpr float kMaxDistance = 100000.0f;
    static constexpr float kWheelZoomStep = 0.85f;

    glm::vec3 target_{0.0f};
    float distance_ = 20.0f;
    float max_distance_ = kMaxDistance;
    float yaw_ = 0.0f;
    float pitch_ = 35.0f;
    float zoom_ = 45.0f;
    float scene_radius_ = 5.0f;
    bool orthographic_ = false;
};

#endif
