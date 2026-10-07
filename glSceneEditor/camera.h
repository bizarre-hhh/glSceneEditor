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
        return glm::lookAt(Position(), target_, glm::vec3(0.0f, 0.0f, 1.0f));
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

    const glm::vec3& Target() const { return target_; }
    float Distance() const { return distance_; }
    float Yaw() const { return yaw_; }
    float Pitch() const { return pitch_; }
    float Zoom() const { return zoom_; }
    float SceneRadius() const { return scene_radius_; }

    void SetTarget(const glm::vec3& target) { target_ = target; }
    void SetDistance(float distance)
    {
        distance_ = glm::clamp(distance, 0.05f, 100000.0f);
    }
    void SetZoom(float zoom) { zoom_ = glm::clamp(zoom, 1.0f, 90.0f); }
    void SetRotation(float yaw, float pitch)
    {
        yaw_ = yaw;
        pitch_ = glm::clamp(pitch, -89.0f, 89.0f);
    }

    void Orbit(float x_pixels, float y_pixels)
    {
        SetRotation(yaw_ + x_pixels * 0.25f,
                    pitch_ - y_pixels * 0.25f);
    }

    void Pan(float x_pixels, float y_pixels, int viewport_height)
    {
        if (viewport_height <= 0)
            return;
        const glm::vec3 front = glm::normalize(target_ - Position());
        const glm::vec3 right = glm::normalize(
            glm::cross(front, glm::vec3(0.0f, 0.0f, 1.0f)));
        const glm::vec3 up = glm::cross(right, front);
        const float world_units_per_pixel =
            2.0f * distance_ * std::tan(glm::radians(zoom_) * 0.5f) /
            static_cast<float>(viewport_height);
        target_ += (-right * x_pixels + up * y_pixels) * world_units_per_pixel;
    }

    void Dolly(float wheel_steps)
    {
        SetDistance(distance_ * std::pow(0.85f, wheel_steps));
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
    glm::vec3 target_{0.0f};
    float distance_ = 20.0f;
    float yaw_ = 0.0f;
    float pitch_ = 35.0f;
    float zoom_ = 45.0f;
    float scene_radius_ = 5.0f;
};

#endif
