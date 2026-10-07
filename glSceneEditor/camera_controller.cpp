#include "camera_controller.h"

#define NOMINMAX
#include <windows.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "base_plate.h"

namespace
{
bool IsPhysicalKeyDown(int virtual_key)
{
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

void ClipCursorToClient(HWND window)
{
    RECT client_rect{};
    if (!GetClientRect(window, &client_rect))
        return;

    POINT top_left{client_rect.left, client_rect.top};
    POINT bottom_right{client_rect.right, client_rect.bottom};
    if (!ClientToScreen(window, &top_left) ||
        !ClientToScreen(window, &bottom_right))
        return;

    const RECT screen_rect{
        top_left.x, top_left.y, bottom_right.x, bottom_right.y
    };
    ClipCursor(&screen_rect);
}
} // namespace

CameraController::CameraController()
{
    const float size = BasePlate::kAxisLengthWorld;
    SetSceneBounds({0.0f, 0.0f, 0.0f},
                   {size, size, size});
    ResetView(16.0f / 9.0f);
}

void CameraController::Update(GLFWwindow* window, bool capture_mouse,
                              bool capture_keyboard)
{
    const HWND native_window = glfwGetWin32Window(window);
    const HWND host_window = GetAncestor(native_window, GA_ROOT);
    const bool host_active = GetForegroundWindow() == host_window;

    POINT cursor_position{};
    RECT scene_rect{};
    const bool has_cursor_position = GetCursorPos(&cursor_position) != FALSE;
    const HWND window_under_cursor = has_cursor_position
        ? WindowFromPoint(cursor_position) : nullptr;
    const HWND cursor_root = window_under_cursor != nullptr
        ? GetAncestor(window_under_cursor, GA_ROOT) : nullptr;
    const bool cursor_over_scene =
        host_active && cursor_root == host_window &&
        GetWindowRect(native_window, &scene_rect) &&
        PtInRect(&scene_rect, cursor_position);

    if (drag_mode_ != DragMode::None)
    {
        const int button = drag_mode_ == DragMode::Orbit ? VK_RBUTTON : VK_MBUTTON;
        if (!host_active || !IsPhysicalKeyDown(button))
            ReleaseCursor();
    }
    if (drag_mode_ == DragMode::None && cursor_over_scene && !capture_mouse)
    {
        if (IsPhysicalKeyDown(VK_RBUTTON))
            drag_mode_ = DragMode::Orbit;
        else if (IsPhysicalKeyDown(VK_MBUTTON))
            drag_mode_ = DragMode::Pan;

        if (drag_mode_ != DragMode::None)
        {
            last_mouse_x_ = static_cast<float>(cursor_position.x);
            last_mouse_y_ = static_cast<float>(cursor_position.y);
            ClipCursorToClient(native_window);
        }
    }

    // Win32 polling is used because the GLFW window is embedded in Qt.
    if (drag_mode_ != DragMode::None)
    {
        POINT current_position{};
        if (GetCursorPos(&current_position))
        {
            const float mouse_x = static_cast<float>(current_position.x);
            const float mouse_y = static_cast<float>(current_position.y);
            const float delta_x = mouse_x - last_mouse_x_;
            const float delta_y = mouse_y - last_mouse_y_;
            if (drag_mode_ == DragMode::Orbit)
                camera_.Orbit(delta_x, delta_y);
            else
            {
                RECT client_rect{};
                if (GetClientRect(native_window, &client_rect))
                    camera_.Pan(delta_x, delta_y,
                                client_rect.bottom - client_rect.top);
            }
            last_mouse_x_ = mouse_x;
            last_mouse_y_ = mouse_y;
        }
    }

    const bool f_down = IsPhysicalKeyDown('F');
    const bool home_down = IsPhysicalKeyDown(VK_HOME);
    if (cursor_over_scene && !capture_keyboard)
    {
        RECT client_rect{};
        if (GetClientRect(native_window, &client_rect) &&
            client_rect.bottom > client_rect.top)
        {
            const float aspect = static_cast<float>(client_rect.right - client_rect.left) /
                                 (client_rect.bottom - client_rect.top);
            if (f_down && !f_was_down_)
                FrameScene(aspect);
            if (home_down && !home_was_down_)
                ResetView(aspect);
        }
    }
    f_was_down_ = f_down;
    home_was_down_ = home_down;
}

void CameraController::OnScroll(float y_offset)
{
    camera_.Dolly(y_offset);
}

void CameraController::ReleaseCursor()
{
    if (drag_mode_ != DragMode::None)
        ClipCursor(nullptr);
    drag_mode_ = DragMode::None;
}

void CameraController::SetSceneBounds(const glm::vec3& minimum,
                                      const glm::vec3& maximum)
{
    scene_min_ = minimum;
    scene_max_ = maximum;
}

void CameraController::FrameScene(float aspect)
{
    camera_.FrameBounds(scene_min_, scene_max_, aspect);
}

void CameraController::ResetView(float aspect)
{
    // Look along +Y so the plate's +X edge runs horizontally on screen.
    camera_.SetRotation(-90.0f, 35.0f);
    FrameScene(aspect);
}
