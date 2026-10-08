#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "camera.h"

struct GLFWwindow;

class CameraController
{
public:
    enum class StandardView { Front, Back, Left, Right, Top, Bottom };

    CameraController();

    Camera& GetCamera() { return camera_; }
    const Camera& GetCamera() const { return camera_; }

    void Update(GLFWwindow* window, bool capture_mouse, bool capture_keyboard);
    void OnScroll(float y_offset);
    void ReleaseCursor();
    void SetSceneBounds(const glm::vec3& minimum, const glm::vec3& maximum);
    void ResetView(float aspect);
    void SetStandardView(StandardView view);

private:
    enum class DragMode { None, Orbit, Pan };

    Camera camera_;
    glm::vec3 scene_min_{0.0f};
    glm::vec3 scene_max_{0.0f};
    glm::vec3 content_min_{0.0f};
    glm::vec3 content_max_{0.0f};
    float last_mouse_x_ = 0.0f;
    float last_mouse_y_ = 0.0f;
    DragMode drag_mode_ = DragMode::None;
    bool home_was_down_ = false;
};

#endif
