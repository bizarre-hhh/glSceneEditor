#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <functional>
#include <optional>

#include "camera.h"

struct GLFWwindow;

class CameraController
{
public:
    enum class StandardView { Front, Back, Left, Right, Top, Bottom };
    // View-space depth of the scene surface under a window position.
    using PanDepthProvider = std::function<std::optional<float>(float, float)>;

    CameraController();

    Camera& GetCamera() { return camera_; }
    const Camera& GetCamera() const { return camera_; }

    void Update(GLFWwindow* window, bool capture_mouse, bool capture_keyboard,
                const PanDepthProvider& pan_depth = {});
    // The optional anchor is a world-space point (for example the surface
    // under the cursor) that perspective zoom should converge on.
    void OnScroll(float y_offset, const glm::vec3* zoom_anchor = nullptr);
    void ReleaseCursor();
    void SetPlateSizeMm(const glm::vec3& size_mm);
    void SetSceneBounds(const glm::vec3& minimum, const glm::vec3& maximum);
    void ResetView(float aspect);
    void SetStandardView(StandardView view);

private:
    enum class DragMode { None, Orbit, Pan };

    Camera camera_;
    glm::vec2 plate_size_world_{0.0f};
    glm::vec3 axis_lengths_world_{0.0f};
    glm::vec3 scene_min_{0.0f};
    glm::vec3 scene_max_{0.0f};
    glm::vec3 content_min_{0.0f};
    glm::vec3 content_max_{0.0f};
    float last_mouse_x_ = 0.0f;
    float last_mouse_y_ = 0.0f;
    float pan_reference_distance_ = 0.0f;
    DragMode drag_mode_ = DragMode::None;
    bool home_was_down_ = false;
};

#endif
