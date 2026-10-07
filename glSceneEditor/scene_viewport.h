#ifndef SCENE_VIEWPORT_H
#define SCENE_VIEWPORT_H

#include <glm/vec3.hpp>

#include <memory>
#include <string>

#include "camera_controller.h"

class BasePlate;
class Shader;
class StlModel;
struct ImFont;
struct GLFWwindow;

// Owns the GLFW/OpenGL scene, graphics resources, STL model and camera input.
// The Qt window that wraps NativeHandle() must be destroyed before this object.
class SceneViewport
{
public:
    static constexpr int kInitialWidth = 1280;
    static constexpr int kInitialHeight = 800;

    SceneViewport();
    ~SceneViewport();
    SceneViewport(const SceneViewport&) = delete;
    SceneViewport& operator=(const SceneViewport&) = delete;

    bool Initialize();
    void ShowNativeWindow();
    void* NativeHandle() const;
    bool RenderFrame(); // false requests application shutdown
    void ReleaseCursor();
    bool LoadStl(const std::string& path, std::string& error, glm::vec3& size_mm);
    void SetSettingsVisible(bool visible) { show_settings_ = visible; }
    void ReleaseGraphics(); // call before the Qt window destroys its wrapper

private:
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset);
    void DrawSettings();

    GLFWwindow* window_ = nullptr;
    void* previous_ime_context_ = nullptr;
    bool glfw_initialized_ = false;
    bool glad_ready_ = false;
    bool ime_detached_ = false;
    bool imgui_context_ready_ = false;
    bool imgui_glfw_ready_ = false;
    bool imgui_opengl_ready_ = false;
    bool graphics_released_ = false;
    bool initial_view_fitted_ = false;

    std::unique_ptr<Shader> stl_shader_;
    std::unique_ptr<Shader> plate_shader_;
    std::unique_ptr<BasePlate> base_plate_;
    std::unique_ptr<StlModel> current_model_;
    ImFont* axis_label_font_ = nullptr;
    CameraController camera_controller_;

    bool show_demo_window_ = false;
    bool show_settings_ = false;
    bool wireframe_ = false;
    float clear_color_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

#endif
