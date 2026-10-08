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
    SetSceneBounds(glm::vec3(0.0f), glm::vec3(0.0f));
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

    const bool home_down = IsPhysicalKeyDown(VK_HOME);
    if (cursor_over_scene && !capture_keyboard)
    {
        RECT client_rect{};
        if (GetClientRect(native_window, &client_rect) &&
            client_rect.bottom > client_rect.top)
        {
            const float aspect = static_cast<float>(client_rect.right - client_rect.left) /
                                 (client_rect.bottom - client_rect.top);

            if (home_down && !home_was_down_)
                ResetView(aspect);
        }
    }
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
    content_min_ = minimum;
    content_max_ = maximum;
    scene_min_ = glm::min(minimum, glm::vec3(0.0f));
    scene_max_ = glm::max(maximum, glm::vec3(BasePlate::kAxisLengthWorld));
}


void CameraController::ResetView(float aspect)
{
    // Look along +Y so the plate's +X edge runs horizontally on screen.
    camera_.SetRotation(-90.0f, 35.0f);
    camera_.FrameBounds(scene_min_, scene_max_, aspect);

    // A lower observation center raises the plate in the viewport. Fit the
    // actual plate, axes and model instead of the much larger enclosing sphere.
    glm::vec3 target = camera_.Target();
    target.z = scene_min_.z + (scene_max_.z - scene_min_.z) * 0.25f;
    camera_.SetTarget(target);
    const glm::mat3 rotation(camera_.GetViewMatrix());
    const float half_fov = std::tan(glm::radians(camera_.Zoom()) * 0.5f);
    const float horizontal_limit = half_fov * std::max(aspect, 0.01f) * 0.84f;
    const float upper_limit = half_fov * 0.76f;
    const float lower_limit = half_fov * 0.78f;
    float distance = 0.05f;
    const auto include_point = [&](const glm::vec3& point)
    {
        const glm::vec3 view_point = rotation * (point - target);
        const float horizontal_distance = std::abs(view_point.x) / horizontal_limit;
        const float vertical_distance = view_point.y >= 0.0f
            ? view_point.y / upper_limit : -view_point.y / lower_limit;
        distance = std::max(distance, view_point.z +
                           std::max(horizontal_distance, vertical_distance));
    };
    const auto include_box = [&](const glm::vec3& minimum, const glm::vec3& maximum)
    {
        for (int corner = 0; corner < 8; ++corner)
            include_point({corner & 1 ? maximum.x : minimum.x,
                           corner & 2 ? maximum.y : minimum.y,
                           corner & 4 ? maximum.z : minimum.z});
    };
    include_box(content_min_, content_max_);
    include_box(glm::vec3(0.0f),
                {BasePlate::kSizeWorld, BasePlate::kSizeWorld, 0.0f});
    // Include the width of the solid axis shafts and arrowheads, with room
    // around the projected tips for their on-screen letter markers.
    constexpr float axis_radius = 0.2f;
    const float length = BasePlate::kAxisLengthWorld;
    const float axis_z = BasePlate::kHorizontalAxisZ;
    include_box({0.0f, -axis_radius, axis_z - axis_radius},
                {length, axis_radius, axis_z + axis_radius});
    include_box({-axis_radius, 0.0f, axis_z - axis_radius},
                {axis_radius, length, axis_z + axis_radius});
    include_box({-axis_radius, -axis_radius, 0.0f},
                {axis_radius, axis_radius, length});
    camera_.SetDistance(distance);
}

void CameraController::SetStandardView(StandardView view)
{
    ReleaseCursor();
    switch (view)
    {
    case StandardView::Front:  camera_.SetRotation(-90.0f, 0.0f); break;
    case StandardView::Back:   camera_.SetRotation(90.0f, 0.0f); break;
    case StandardView::Left:   camera_.SetRotation(180.0f, 0.0f); break;
    case StandardView::Right:  camera_.SetRotation(0.0f, 0.0f); break;
    case StandardView::Top:    camera_.SetRotation(-90.0f, 90.0f); break;
    case StandardView::Bottom: camera_.SetRotation(-90.0f, -90.0f); break;
    }
    camera_.SetOrthographic(true);
}
