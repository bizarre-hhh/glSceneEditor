#include "scene_viewport.h"
#include "model_names.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <QFile>
#include <QFileInfo>
#define NOMINMAX
#include <windows.h>
#include <imm.h>

// GLAD provides the OpenGL declarations used by GLFW and the renderers.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "ImGuizmo.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "base_plate.h"
#include "shader.h"
#include "stl_model.h"
#include "view_cube.h"

namespace
{
constexpr float kPlateContactToleranceMm = 1.0e-4f;

void GlfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

std::filesystem::path GetExecutableDirectory()
{
    wchar_t executable_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path, ARRAYSIZE(executable_path));
    if (length == 0)
        return std::filesystem::current_path();
    return std::filesystem::path(
        std::wstring(executable_path, length)).parent_path();
}

bool IsLinked(const Shader& shader)
{
    GLint linked = GL_FALSE;
    glGetProgramiv(shader.ID, GL_LINK_STATUS, &linked);
    return linked == GL_TRUE;
}

// Parameters of the near-to-far segment that lie inside the axis-aligned box.
bool SegmentIntersectsBox(const glm::vec3& start, const glm::vec3& segment,
                          const glm::vec3& minimum, const glm::vec3& maximum,
                          float& enter, float& leave)
{
    enter = 0.0f;
    leave = 1.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (segment[axis] == 0.0f)
        {
            if (start[axis] < minimum[axis] || start[axis] > maximum[axis])
                return false;
            continue;
        }
        float first = (minimum[axis] - start[axis]) / segment[axis];
        float last = (maximum[axis] - start[axis]) / segment[axis];
        if (first > last)
            std::swap(first, last);
        enter = std::max(enter, first);
        leave = std::min(leave, last);
        if (enter > leave)
            return false;
    }
    return enter >= 0.0f && enter <= 1.0f;
}

bool ProjectToScreen(const glm::vec3& point, const glm::mat4& view_projection,
                     const ImVec2& display_size, ImVec2& screen)
{
    const glm::vec4 clip = view_projection * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0f || clip.z < -clip.w || clip.z > clip.w)
        return false;
    const float x = clip.x / clip.w;
    const float y = clip.y / clip.w;
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;
    screen = ImVec2((x * 0.5f + 0.5f) * display_size.x,
                    (0.5f - y * 0.5f) * display_size.y);
    return true;
}

void DrawAxisLabels(ImFont* font, const Camera& camera, const glm::mat4& view_projection,
                    const BasePlate& plate)
{
    if (font == nullptr)
        return;

    const ImVec2 display_size = ImGui::GetIO().DisplaySize;
    if (display_size.x <= 0.0f || display_size.y <= 0.0f)
        return;

    const float dpi_scale = ImGui::GetStyle().FontScaleDpi;
    const float font_size = 24.0f * dpi_scale;
    const float tip_gap = 27.0f * dpi_scale;
    const float label_padding = 7.0f * dpi_scale;
    const float rim_width = 2.5f * dpi_scale;
    const glm::vec3 axis_lengths = plate.AxisLengthsWorld();
    const glm::vec3 origin(0.0f, 0.0f, 0.0f);
    struct AxisLabel
    {
        const char* text;
        glm::vec3 tip;
        glm::vec3 base;
        ImU32 rim_color;
    };
    const AxisLabel labels[] = {
        {"X", {axis_lengths.x, 0.0f, BasePlate::kHorizontalAxisZ},
         {0.0f, 0.0f, BasePlate::kHorizontalAxisZ},
         IM_COL32(255, 92, 86, 255)},
        {"Y", {0.0f, axis_lengths.y, BasePlate::kHorizontalAxisZ},
         {0.0f, 0.0f, BasePlate::kHorizontalAxisZ},
         IM_COL32(80, 225, 105, 255)},
        {"Z", {0.0f, 0.0f, axis_lengths.z}, origin,
         IM_COL32(95, 154, 255, 255)},
    };

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    for (const AxisLabel& label : labels)
    {
        if (!camera.IsAxisVisible(label.tip - label.base))
            continue;
        ImVec2 tip;
        if (!ProjectToScreen(label.tip, view_projection, display_size, tip) ||
            tip.x < 0.0f || tip.x > display_size.x ||
            tip.y < 0.0f || tip.y > display_size.y)
            continue;

        ImVec2 base;
        const bool has_base = ProjectToScreen(
            label.base, view_projection, display_size, base);
        float dx = has_base ? tip.x - base.x : 0.0f;
        float dy = has_base ? tip.y - base.y : -1.0f;
        const float direction_length = std::sqrt(dx * dx + dy * dy);
        if (direction_length < 1.0f)
        {
            dx = 0.0f;
            dy = -1.0f;
        }
        else
        {
            dx /= direction_length;
            dy /= direction_length;
        }

        const ImVec2 text_size = font->CalcTextSizeA(
            font_size, FLT_MAX, 0.0f, label.text);
        const float radius =
            std::max(text_size.x, text_size.y) * 0.5f + label_padding;
        if (display_size.x <= radius * 2.0f ||
            display_size.y <= radius * 2.0f)
            continue;
        ImVec2 center(tip.x + dx * tip_gap, tip.y + dy * tip_gap);
        center.x = std::clamp(center.x, radius, display_size.x - radius);
        center.y = std::clamp(center.y, radius, display_size.y - radius);
        draw_list->AddCircleFilled(center, radius, IM_COL32(19, 25, 34, 244));
        draw_list->AddCircle(center, radius, label.rim_color, 0, rim_width);
        draw_list->AddText(font, font_size,
                           ImVec2(center.x - text_size.x * 0.5f,
                                  center.y - text_size.y * 0.5f),
                           IM_COL32(255, 255, 255, 255), label.text);
    }
}
} // namespace

SceneViewport::SceneViewport() = default;

SceneViewport::~SceneViewport()
{
    ReleaseGraphics();
    if (window_ != nullptr)
    {
        const HWND native_window = glfwGetWin32Window(window_);
        if (ime_detached_)
            ImmAssociateContext(native_window,
                                static_cast<HIMC>(previous_ime_context_));
        glfwDestroyWindow(window_);
    }
    if (glfw_initialized_)
        glfwTerminate();
}

bool SceneViewport::Initialize()
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
        return false;
    glfw_initialized_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    const float content_scale =
        ImGui_ImplGlfw_GetContentScaleForMonitor(primary_monitor);
    window_ = glfwCreateWindow(
        static_cast<int>(kInitialWidth * content_scale),
        static_cast<int>(kInitialHeight * content_scale),
        "glSceneEditor - Scene", nullptr, nullptr);
    if (window_ == nullptr)
        return false;

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    glfwSetScrollCallback(window_, ScrollCallback);
    glfwSetMouseButtonCallback(window_, MouseButtonCallback);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        std::cerr << "Failed to initialize GLAD.\n";
        return false;
    }
    glad_ready_ = true;

    const HWND native_window = glfwGetWin32Window(window_);
    previous_ime_context_ = ImmAssociateContext(native_window, nullptr);
    ime_detached_ = true;
    glEnable(GL_DEPTH_TEST);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_context_ready_ = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->AddFontDefault();
    axis_label_font_ = io.Fonts->AddFontDefaultVector();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(content_scale);
    style.FontScaleDpi = content_scale;
    auto& gizmo_style = ImGuizmo::GetStyle();
    gizmo_style = ImGuizmo::Style();
    gizmo_style.TranslationLineThickness = 3.0f * content_scale;
    gizmo_style.TranslationLineArrowSize = 7.0f * content_scale;
    gizmo_style.RotationLineThickness = 3.0f * content_scale;
    gizmo_style.ScaleLineThickness = 3.0f * content_scale;
    gizmo_style.ScaleLineCircleSize = 7.0f * content_scale;
    gizmo_style.CenterCircleSize = 6.0f * content_scale;
    gizmo_style.HatchedAxisLineThickness = 4.0f * content_scale;
    gizmo_style.Colors[ImGuizmo::SELECTION] = ImVec4(1.0f, 0.85f, 0.15f, 1.0f);
    style.GrabMinSize = 16.0f;
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.25f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.55f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.70f, 1.0f, 1.0f);

    // ImGui chains the callbacks registered above.
    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true))
        return false;
    imgui_glfw_ready_ = true;
    if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
        return false;
    imgui_opengl_ready_ = true;

    const std::filesystem::path shaders =
        GetExecutableDirectory() / "resources" / "shaders";
    const std::string stl_vertex = (shaders / "stl.vs").string();
    const std::string stl_fragment = (shaders / "stl.fs").string();
    const std::string plate_vertex = (shaders / "base_plate.vs").string();
    const std::string plate_fragment = (shaders / "base_plate.fs").string();
    stl_shader_ = std::make_unique<Shader>(stl_vertex.c_str(),
                                            stl_fragment.c_str());
    plate_shader_ = std::make_unique<Shader>(plate_vertex.c_str(),
                                              plate_fragment.c_str());
    if (!IsLinked(*stl_shader_) || !IsLinked(*plate_shader_))
    {
        std::cerr << "Failed to link scene shaders.\n";
        return false;
    }
    base_plate_ = std::make_unique<BasePlate>(plate_size_mm_);

    // Bounds share the plate's unlit position/color shader and one line buffer.
    glGenVertexArrays(1, &bounds_vao_);
    glGenBuffers(1, &bounds_vbo_);
    glBindVertexArray(bounds_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, bounds_vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Slice contours use the same position/color layout as bounds.
    glGenVertexArrays(1, &slice_vao_);
    glGenBuffers(1, &slice_vbo_);
    glBindVertexArray(slice_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, slice_vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void SceneViewport::ShowNativeWindow()
{
    glfwShowWindow(window_);
}

void* SceneViewport::NativeHandle() const
{
    return glfwGetWin32Window(window_);
}

void SceneViewport::FramebufferSizeCallback(GLFWwindow*, int width, int height)
{
    glViewport(0, 0, width, height);
}

void SceneViewport::ScrollCallback(GLFWwindow* window, double, double y_offset)
{
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
        return;
    auto* self = static_cast<SceneViewport*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && !self->model_drag_ && !ImGuizmo::IsUsingAny() && !ImGuizmo::IsOver())
    {
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        glfwGetCursorPos(window, &cursor_x, &cursor_y);
        const auto anchor = self->ZoomAnchorAt(
            glm::vec2(static_cast<float>(cursor_x), static_cast<float>(cursor_y)));
        self->camera_controller_.OnScroll(
            static_cast<float>(y_offset), anchor ? &*anchor : nullptr);
    }
}

void SceneViewport::MouseButtonCallback(GLFWwindow* window, int button,
                                        int action, int)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
        return;
    auto* self = static_cast<SceneViewport*>(glfwGetWindowUserPointer(window));
    if (self == nullptr || self->IsTransformEditing())
        return;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    self->pending_pick_ = glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

glm::mat4 SceneViewport::ProjectionMatrix(float aspect) const
{
    return camera_controller_.GetCamera().GetProjectionMatrix(aspect);
}

void SceneViewport::DrawSettings()
{
    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(420.0f, 220.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("Scene Settings");
    ImGui::Text("GLFW + OpenGL + Dear ImGui + Assimp");
    ImGui::Text("RMB: orbit | MMB: pan | Wheel: zoom | Home: reset");
    ImGui::Separator();
    Camera& camera = camera_controller_.GetCamera();
    int projection_mode = camera.IsOrthographic() ? 1 : 0;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Projection");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##projection", &projection_mode, "Perspective\0Orthographic\0"))
        camera.SetOrthographic(projection_mode == 1);
    ImGui::Separator();
    ImGui::Checkbox("Show ImGui demo", &show_demo_window_);
    ImGui::Checkbox("Wireframe", &wireframe_);
    ImGui::Text("Base plate: %.1f x %.1f mm, grid: %d mm",
                plate_size_mm_.x, plate_size_mm_.y, BasePlate::kGridStepMm);
    const glm::vec3 axis_mm =
        BasePlate::AxisLengthsForSize(plate_size_mm_) *
        BasePlate::kMmPerWorldUnit;
    ImGui::Text("Axis length: %.1f x %.1f x %.1f mm (20%% beyond plate)",
                axis_mm.x, axis_mm.y, axis_mm.z);
    ImGui::Text("Origin: plate corner | X red | Y green | Z blue (up)");
    ImGui::Text("Loaded STL models: %zu", models_.size());
    if (const auto* info = CurrentModelInfo())
    {
        const glm::vec3 size = info->size_mm;
        ImGui::Text("STL size: %.1f x %.1f x %.1f mm",
                    size.x, size.y, size.z);
    }
    ImGui::TextUnformatted("Clear color (RGB)");
    ImGui::SliderFloat("Red", &clear_color_[0], 0.0f, 1.0f, "R: %.2f");
    ImGui::SliderFloat("Green", &clear_color_[1], 0.0f, 1.0f, "G: %.2f");
    ImGui::SliderFloat("Blue", &clear_color_[2], 0.0f, 1.0f, "B: %.2f");
    ImGui::SameLine();
    ImGui::ColorButton("Color preview", ImVec4(
        clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]),
        ImGuiColorEditFlags_NoTooltip, ImVec2(52.0f, 52.0f));

    float fov = camera.Zoom();
    if (ImGui::InputFloat("FOV", &fov, 1.0f, 5.0f, "%.1f"))
        camera.SetZoom(fov);
    float yaw = camera.Yaw();
    float pitch = camera.Pitch();
    const bool yaw_changed = ImGui::InputFloat("Yaw", &yaw, 1.0f, 5.0f, "%.1f");
    const bool pitch_changed =
        ImGui::InputFloat("Pitch", &pitch, 1.0f, 5.0f, "%.1f");
    if (yaw_changed || pitch_changed)
        camera.SetRotation(yaw, pitch);
    glm::vec3 target = camera.Target();
    if (ImGui::InputFloat3("Target", glm::value_ptr(target), "%.2f"))
        camera.SetTarget(target);
    float distance = camera.Distance();
    if (ImGui::InputFloat("Distance", &distance, 1.0f, 5.0f, "%.2f"))
        camera.SetDistance(distance);
    const glm::vec3 position = camera.Position();
    ImGui::Text("Camera position: %.2f, %.2f, %.2f",
                position.x, position.y, position.z);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
}

void SceneViewport::DrawModelBounds(const glm::mat4& view, const glm::mat4& projection)
{
    if (models_hidden_)
        return;
    const auto count = std::count_if(models_.begin(), models_.end(), [](const ModelEntry& entry)
    { return entry.info.visible && entry.info.bounding_box_visible; });
    if (count == 0)
        return;

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(count) * 24 * 6);
    const glm::vec3 color(0.0f, 0.65f, 0.85f);
    for (const auto& entry : models_)
    {
        const auto& info = entry.info;
        if (!info.visible || !info.bounding_box_visible)
            continue;
        const glm::vec3 minimum = info.world_bounds_min_mm / BasePlate::kMmPerWorldUnit;
        const glm::vec3 maximum = info.world_bounds_max_mm / BasePlate::kMmPerWorldUnit;
        std::array<glm::vec3, 8> corners;
        for (int corner = 0; corner < 8; ++corner)
            corners[corner] = {corner & 1 ? maximum.x : minimum.x,
                               corner & 2 ? maximum.y : minimum.y,
                               corner & 4 ? maximum.z : minimum.z};
        // Each corner connects only to its positive X/Y/Z neighbor: 12 edges.
        for (int corner = 0; corner < 8; ++corner)
            for (int axis = 0; axis < 3; ++axis)
                if ((corner & (1 << axis)) == 0)
                    for (const int endpoint : {corner, corner | (1 << axis)})
                    {
                        const glm::vec3& point = corners[endpoint];
                        vertices.insert(vertices.end(),
                            {point.x, point.y, point.z, color.x, color.y, color.z});
                    }
    }

    plate_shader_->use();
    plate_shader_->setMat4("view", view);
    plate_shader_->setMat4("projection", projection);
    plate_shader_->setFloat("alpha", 1.0f);
    plate_shader_->setFloat("zOffset", 0.0f);
    glBindVertexArray(bounds_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, bounds_vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
                 vertices.data(), GL_STREAM_DRAW);

    // Scene depth hides edges behind models and the plate; equal-depth surface edges remain visible.
    const GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLint previous_depth_function = GL_LESS;
    GLboolean depth_write = GL_TRUE;
    GLfloat previous_width = 1.0f;
    GLfloat width_range[2]{1.0f, 1.0f};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);
    glGetIntegerv(GL_DEPTH_FUNC, &previous_depth_function);
    glGetFloatv(GL_LINE_WIDTH, &previous_width);
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, width_range);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glLineWidth(std::clamp(2.0f, width_range[0], width_range[1]));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size() / 6));
    glLineWidth(previous_width);
    glDepthMask(depth_write);
    glDepthFunc(previous_depth_function);
    if (!depth_test)
        glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SceneViewport::DrawSlicePreview(const glm::mat4& view, const glm::mat4& projection)
{
    if (!slice_visible_ || slice_layers_.empty() || !plate_shader_)
        return;
    const bool draw_contours =
        slice_contours_visible_ && slice_contour_vertex_count_ > 0;
    const bool draw_hatches =
        slice_hatches_visible_ && slice_hatch_vertex_count_ > 0;
    const bool draw_arrows =
        slice_direction_visible_ && slice_hatches_visible_ &&
        slice_arrow_vertex_count_ > 0;
    const bool draw_jumps =
        slice_jumps_visible_ && slice_jump_vertex_count_ > 0;
    if (!draw_contours && !draw_hatches && !draw_arrows && !draw_jumps)
        return;

    GLboolean previous_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean previous_depth_write = GL_TRUE;
    GLint previous_depth_function = GL_LESS;
    GLfloat previous_width = 1.0f;
    GLfloat width_range[2]{1.0f, 1.0f};
    GLboolean previous_offset_line = glIsEnabled(GL_POLYGON_OFFSET_LINE);
    GLfloat previous_offset_factor = 0.0f;
    GLfloat previous_offset_units = 0.0f;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previous_depth_write);
    glGetIntegerv(GL_DEPTH_FUNC, &previous_depth_function);
    glGetFloatv(GL_LINE_WIDTH, &previous_width);
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, width_range);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previous_offset_factor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previous_offset_units);

    plate_shader_->use();
    plate_shader_->setMat4("view", view);
    plate_shader_->setMat4("projection", projection);
    plate_shader_->setFloat("alpha", 1.0f);
    plate_shader_->setFloat("zOffset", 0.0f);

    glBindVertexArray(slice_vao_);
    if (draw_hatches || draw_arrows || draw_jumps)
    {
        // Fills, scan direction and travel moves are inside the solid; draw
        // them as an x-ray overlay.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_POLYGON_OFFSET_LINE);
        glLineWidth(std::clamp(1.0f, width_range[0], width_range[1]));
        if (draw_hatches)
            glDrawArrays(GL_LINES, slice_contour_vertex_count_,
                         slice_hatch_vertex_count_);
        if (draw_arrows)
            glDrawArrays(GL_LINES,
                         slice_contour_vertex_count_ +
                             slice_hatch_vertex_count_,
                         slice_arrow_vertex_count_);
        if (draw_jumps)
            glDrawArrays(GL_LINES,
                         slice_contour_vertex_count_ +
                             slice_hatch_vertex_count_ +
                             slice_arrow_vertex_count_,
                         slice_jump_vertex_count_);
    }
    if (draw_contours)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        glLineWidth(std::clamp(2.0f, width_range[0], width_range[1]));
        glDrawArrays(GL_LINES, 0, slice_contour_vertex_count_);
    }
    glLineWidth(previous_width);
    glPolygonOffset(previous_offset_factor, previous_offset_units);
    if (previous_offset_line)
        glEnable(GL_POLYGON_OFFSET_LINE);
    else
        glDisable(GL_POLYGON_OFFSET_LINE);
    glDepthMask(previous_depth_write);
    glDepthFunc(previous_depth_function);
    if (previous_depth_test)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

bool SceneViewport::RenderFrame()
{
    glfwMakeContextCurrent(window_);
    if (!slice_layers_.empty() &&
        slice_source_project_revision_ != project_revision_)
    {
        ClearSlicePreview();
        slice_invalidated_ = true;
    }
    glfwPollEvents();
    if (glfwWindowShouldClose(window_))
    {
        // Qt closeEvent can cancel shutdown after an unsaved-changes prompt.
        glfwSetWindowShouldClose(window_, GLFW_FALSE);
        return false;
    }

    int window_width = 0;
    int window_height = 0;
    int display_width = 0;
    int display_height = 0;
    glfwGetWindowSize(window_, &window_width, &window_height);
    glfwGetFramebufferSize(window_, &display_width, &display_height);
    if (window_width <= 0 || window_height <= 0 ||
        display_width <= 0 || display_height <= 0)
    {
        ReleaseCursor();
        return true;
    }
    if (!initial_view_fitted_)
    {
        camera_controller_.ResetView(
            static_cast<float>(display_width) / display_height);
        initial_view_fitted_ = true;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    const ImGuiIO& io = ImGui::GetIO();
    const float dpi_scale = ImGui::GetStyle().FontScaleDpi;
    const ViewCube::Layout cube_layout = ViewCube::GetLayout(
        glm::vec2(io.DisplaySize.x, io.DisplaySize.y), dpi_scale);
    const bool over_cube = ViewCube::Contains(glm::vec2(io.MousePos.x, io.MousePos.y), cube_layout);
    const auto pan_depth = [this](float x, float y) -> std::optional<float>
    {
        const Camera& camera = camera_controller_.GetCamera();
        const auto ray = MouseRayAt({x, y});
        if (!ray)
            return std::nullopt;
        const auto hit = ClosestScenePoint(*ray);
        if (!hit)
            return std::nullopt;
        const glm::vec3 forward =
            glm::normalize(camera.Target() - camera.Position());
        const float depth = glm::dot(*hit - camera.Position(), forward);
        return depth > 0.0f ? std::optional<float>(depth) : std::nullopt;
    };
    camera_controller_.Update(window_, io.WantCaptureMouse || over_cube || model_drag_.has_value() || ImGuizmo::IsUsingAny(),
                              io.WantCaptureKeyboard || model_drag_.has_value() || ImGuizmo::IsUsingAny(),
                              pan_depth);
    if (show_demo_window_)
        ImGui::ShowDemoWindow(&show_demo_window_);
    if (show_settings_)
        DrawSettings();
    ViewCube::Draw(camera_controller_, dpi_scale);

    const Camera& camera = camera_controller_.GetCamera();
    const float aspect = static_cast<float>(display_width) / display_height;
    const glm::mat4 projection = ProjectionMatrix(aspect);
    const glm::mat4 view = camera.GetViewMatrix();
    const bool over_gizmo =
        models_hidden_ ? false : DrawModelGizmo(view, projection, over_cube);
    if (pending_pick_)
    {
        const glm::vec2 position = *pending_pick_;
        pending_pick_.reset();
        if (!models_hidden_ && !IsTransformEditing() && !io.WantCaptureMouse &&
            !over_gizmo && !ViewCube::Contains(position, cube_layout))
        {
            const auto ray = MouseRayAt(position);
            glm::vec3 hit_point(0.0f);
            ModelEntry* hit = ray ? ClosestModel(*ray, &hit_point) : nullptr;
            if (hit)
            {
                const ModelId id = hit->info.id;
                const bool was_selected = hit->info.selected;
                SetModelSelected(id, true);
                if (was_selected && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                    BeginModelDrag(id, position, hit_point);
            }
            else if (ray)
                ClearModelSelection();
        }
    }
    if (model_drag_)
    {
        if (models_hidden_)
            EndModelTransform();
        else
        {
            const auto* dragged = ModelInfo(model_drag_->id);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !dragged ||
                !dragged->selected || !dragged->visible ||
                ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                EndModelTransform();
            else
            {
                UpdateModelDrag(glm::vec2(io.MousePos.x, io.MousePos.y));
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }
    }
    DrawAxisLabels(axis_label_font_, camera, projection * view, *base_plate_);
    ImGui::Render();

    glViewport(0, 0, display_width, display_height);
    glClearColor(clear_color_[0], clear_color_[1],
                 clear_color_[2], clear_color_[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const bool has_bounds = !models_hidden_ &&
        std::any_of(models_.begin(), models_.end(), [](const ModelEntry& entry)
    { return entry.info.visible && entry.info.bounding_box_visible; });
    GLboolean previous_offset_fill = GL_FALSE;
    GLfloat previous_offset_factor = 0.0f, previous_offset_units = 0.0f;
    if (has_bounds)
    {
        // Separate coplanar filled surfaces from the bounds by a small depth
        // offset, avoiding broken surface edges without exposing rear edges.
        previous_offset_fill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previous_offset_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previous_offset_units);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
    }
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);
    if (!models_hidden_)
        for (const auto& entry : models_)
            if (entry.info.visible)
                entry.model->Draw(*stl_shader_, view, projection, entry.info.selected);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    base_plate_->Draw(*plate_shader_, view, projection, camera);
    if (has_bounds)
    {
        glPolygonOffset(previous_offset_factor, previous_offset_units);
        if (!previous_offset_fill)
            glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (!models_hidden_)
        DrawModelBounds(view, projection);
    DrawSlicePreview(view, projection);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
    return true;
}

void SceneViewport::ReleaseCursor()
{
    camera_controller_.ReleaseCursor();
    EndModelTransform();
    gizmo_dragging_ = false;
    if (imgui_context_ready_)
    {
        ImGuizmo::PushID("modelTransform");
        ImGuizmo::Enable(false);
        ImGuizmo::PopID();
    }
    pending_pick_.reset();
}

SceneViewport::ModelEntry* SceneViewport::FindModel(ModelId id)
{
    const auto found = std::find_if(models_.begin(), models_.end(),
        [id](const ModelEntry& entry) { return entry.info.id == id; });
    return found == models_.end() ? nullptr : &*found;
}

const SceneViewport::ModelEntry* SceneViewport::FindModel(ModelId id) const
{
    const auto found = std::find_if(models_.begin(), models_.end(),
        [id](const ModelEntry& entry) { return entry.info.id == id; });
    return found == models_.end() ? nullptr : &*found;
}

const SceneViewport::StlModelInfo* SceneViewport::ModelInfo(ModelId id) const
{
    const auto* entry = FindModel(id);
    return entry ? &entry->info : nullptr;
}

std::vector<SceneViewport::StlModelInfo> SceneViewport::ModelInfos() const
{
    std::vector<StlModelInfo> result;
    result.reserve(models_.size());
    for (const auto& entry : models_)
        result.push_back(entry.info);
    return result;
}

void SceneViewport::ChooseActiveModel()
{
    if (IsTransformEditing() && FindModel(transform_editing_model_id_))
    {
        active_model_id_ = transform_editing_model_id_;
        return;
    }
    const ModelEntry* recent = nullptr;
    for (const auto& entry : models_)
        if (entry.info.selected && (!recent || entry.selection_order > recent->selection_order))
            recent = &entry;
    if (recent)
    {
        active_model_id_ = recent->info.id;
        return;
    }
    if (!FindModel(active_model_id_))
        active_model_id_ = models_.empty() ? 0 : models_.back().info.id;
}

bool SceneViewport::LoadStl(const std::string& path, std::string& error,
                            glm::vec3& size_mm)
{
    QFile file(QString::fromUtf8(path.c_str()));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.size() > 512 * 1024 * 1024)
    {
        error = "Cannot read the STL file (supported size: 1 byte to 512 MB).";
        return false;
    }
    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError || data.size() != file.size())
    {
        error = "Failed to read the complete STL file.";
        return false;
    }
    glfwMakeContextCurrent(window_);
    auto loaded = std::make_unique<StlModel>();
    if (!loaded->LoadFromMemory(data.constData(), data.size(), error))
        return false;
    loaded->SetPlacementCenterMm(
        glm::vec2(plate_size_mm_.x, plate_size_mm_.y) * 0.5f);
    size_mm = loaded->SizeMm();
    StlModelInfo info{path, size_mm, loaded->TriangleCount()};
    QSet<QString> names;
    for (const auto& entry : models_) names.insert(entry.info.name.toCaseFolded());
    info.name = ModelNames::Unique(ModelNames::FromPath(QString::fromUtf8(path.c_str())), names);
    info.source_file_size = static_cast<std::size_t>(data.size());
    info.id = next_model_id_++;
    ReleaseCursor();
    const bool keep_active = IsModelSelected();
    models_.push_back({std::move(loaded), std::move(info), 0, std::move(data)});
    if (!keep_active)
        active_model_id_ = models_.back().info.id;
    RefreshModelTransformInfo(models_.back());
    RefreshSceneBounds();
    Operation operation;
    operation.kind = OperationKind::Import;
    operation.model = CaptureModel(models_.back());
    operation.before_revision = project_revision_;
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    operation.description = QStringLiteral("打开 STL：%1")
        .arg(models_.back().info.name);
    RecordOperation(std::move(operation));
    ++model_state_revision_;
    return true;
}

bool SceneViewport::PlaceSelectedModelsOnPlate(QString& error)
{
    error.clear();
    if (!window_ || graphics_released_ || IsTransformEditing())
    {
        error = QStringLiteral("请先关闭变换参数窗口，并确认场景已就绪。");
        return false;
    }
    if (std::none_of(models_.begin(), models_.end(), [](const auto& entry) { return entry.info.selected; }))
    {
        error = QStringLiteral("请先选中需要贴合基板的模型。");
        return false;
    }
    ReleaseCursor();
    Operation operation;
    operation.kind = OperationKind::TransformModels;
    operation.before_revision = project_revision_;
    for (const auto& entry : models_)
    {
        if (!entry.info.selected || std::abs(entry.info.world_bounds_min_mm.z) <= kPlateContactToleranceMm) continue;
        const auto before = entry.model->Transform();
        auto after = before;
        // Displacement is stored along local rotated axes. Convert the world
        // vertical move back to local coordinates so world X/Y stay fixed.
        const glm::dmat3 rotation(ModelTransformMath::Rotation(before.rotation_degrees));
        after.translation_mm = glm::vec3(glm::dvec3(before.translation_mm) +
            glm::inverse(rotation) * glm::dvec3(0, 0, -entry.info.world_bounds_min_mm.z));
        operation.transforms.push_back({entry.info.id, before, after});
    }
    if (operation.transforms.empty()) return true;
    if (!ApplyTransforms(operation.transforms, true))
    {
        error = QStringLiteral("模型变换超出有效范围，无法贴合基板。");
        return false;
    }
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    operation.description = QStringLiteral("贴合基板：%1 个模型 | 最低 Z = 0 mm，X/Y 不变")
        .arg(static_cast<qulonglong>(operation.transforms.size()));
    RecordOperation(std::move(operation));
    return true;
}

bool SceneViewport::DuplicateSelectedModels(QString& error)
{
    return RepeatSelectedModels(1, 2, {5.0f, 5.0f}, true, error);
}

bool SceneViewport::ArraySelectedModels(int rows, int columns, const glm::vec2& gap_mm,
                                       QString& error)
{
    return RepeatSelectedModels(rows, columns, gap_mm, false, error);
}

bool SceneViewport::RepeatSelectedModels(int rows, int columns, const glm::vec2& gap_mm,
                                        bool duplicate, QString& error)
{
    error.clear();
    if (!window_ || graphics_released_ || IsTransformEditing())
    {
        error = QStringLiteral("请先关闭变换参数窗口，并确认场景已就绪。");
        return false;
    }
    if (rows < 1 || rows > 100 || columns < 1 || columns > 100 ||
        (rows == 1 && columns == 1) ||
        !std::isfinite(gap_mm.x) || !std::isfinite(gap_mm.y) ||
        gap_mm.x < 0 || gap_mm.y < 0 || gap_mm.x > 10000 || gap_mm.y > 10000)
    {
        error = QStringLiteral("行列数须为 1 至 100 且至少一项大于 1，间隙须为 0 至 10000 mm。");
        return false;
    }
    std::vector<SavedModel> sources;
    glm::vec2 minimum(FLT_MAX), maximum(-FLT_MAX);
    for (const auto& entry : models_)
        if (entry.info.selected)
        {
            sources.push_back(CaptureModel(entry));
            minimum = glm::min(minimum, glm::vec2(entry.info.world_bounds_min_mm));
            maximum = glm::max(maximum, glm::vec2(entry.info.world_bounds_max_mm));
        }
    if (sources.empty())
    {
        error = QStringLiteral("请先选中需要复制或阵列的模型。");
        return false;
    }
    const std::size_t generated_count = sources.size() * (rows * columns - 1);
    if (generated_count > kMaxGeneratedModels || models_.size() + generated_count > 10000)
    {
        error = QStringLiteral("一次最多新增 %1 个模型，工程总模型数不能超过 10000。")
            .arg(static_cast<qulonglong>(kMaxGeneratedModels));
        return false;
    }
    const glm::vec2 step = maximum - minimum + gap_mm;
    if (!std::isfinite(step.x) || !std::isfinite(step.y))
    {
        error = QStringLiteral("模型范围过大，无法生成阵列。");
        return false;
    }

    // Stage every mesh before changing the scene, selection or history.
    glfwMakeContextCurrent(window_);
    std::vector<ModelEntry> additions;
    additions.reserve(generated_count);
    ModelId next_id = next_model_id_;
    auto next_order = next_selection_order_;
    QSet<QString> names;
    for (const auto& entry : models_) names.insert(entry.info.name.toCaseFolded());
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column)
        {
            if (row == 0 && column == 0) continue;
            const glm::vec3 world_offset(column * step.x, row * step.y, 0.0f);
            for (const auto& source : sources)
            {
                SavedModel copy = source;
                copy.info.id = next_id++;
                copy.info.name = ModelNames::Unique(source.info.name, names);
                copy.info.selected = true;
                copy.info.visible = true;
                copy.selection_order = next_order++;
                // Stored translation follows local rotated axes; the grid follows world X/Y.
                const auto rotation = glm::mat3(ModelTransformMath::Rotation(copy.info.local_rotation_degrees));
                copy.info.local_translation_mm += glm::transpose(rotation) * world_offset;
                ModelEntry entry;
                if (!BuildModelEntry(copy, entry, error)) return false;
                additions.push_back(std::move(entry));
            }
        }

    ReleaseCursor();
    Operation operation;
    operation.kind = OperationKind::AddModels;
    operation.before_revision = project_revision_;
    operation.previous_active_id = active_model_id_;
    for (auto& entry : models_)
        if (entry.info.selected)
        {
            operation.previous_selection.push_back({entry.info.id, entry.selection_order});
            entry.info.selected = false;
        }
    models_.reserve(models_.size() + generated_count);
    operation.added_models.reserve(generated_count);
    for (auto& entry : additions)
    {
        models_.push_back(std::move(entry));
        operation.added_models.push_back(CaptureModel(models_.back()));
    }
    next_model_id_ = next_id;
    next_selection_order_ = next_order;
    ChooseActiveModel();
    RefreshSceneBounds();
    ++model_state_revision_;
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    operation.description = duplicate
        ? QStringLiteral("复制 STL：%1 个模型 | X 方向间隙 5.00 mm")
              .arg(static_cast<qulonglong>(sources.size()))
        : QStringLiteral("阵列 STL：%1 个模型 | %2 行 × %3 列 | 间隙 X %4 / Y %5 mm | 新增 %6 个")
              .arg(static_cast<qulonglong>(sources.size())).arg(rows).arg(columns)
              .arg(gap_mm.x, 0, 'f', 2).arg(gap_mm.y, 0, 'f', 2)
              .arg(static_cast<qulonglong>(generated_count));
    RecordOperation(std::move(operation));
    return true;
}

bool SceneViewport::RenameModel(ModelId id, const QString& requested_name, QString& error)
{
    error.clear();
    auto* entry = FindModel(id);
    if (!entry)
    {
        error = QStringLiteral("模型已不存在，无法修改名称。");
        return false;
    }
    const QString name = ModelNames::Normalize(requested_name);
    if (!ModelNames::IsValid(name))
    {
        error = QStringLiteral("名称不能为空，最多 256 个字符，不能包含换行或控制字符。");
        return false;
    }
    for (const auto& other : models_)
        if (other.info.id != id && other.info.name.toCaseFolded() == name.toCaseFolded())
        {
            error = QStringLiteral("已有模型使用该名称，请输入不同的名称。");
            return false;
        }
    if (entry->info.name == name) return true;

    ReleaseCursor();
    Operation operation;
    operation.kind = OperationKind::Rename;
    operation.model.info.id = id;
    operation.before_name = entry->info.name;
    operation.after_name = name;
    operation.before_revision = project_revision_;
    operation.description = QStringLiteral("修改模型名称：%1 → %2").arg(entry->info.name, name);
    entry->info.name = name;
    ++model_state_revision_;
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    RecordOperation(std::move(operation));
    return true;
}

void SceneViewport::SetModelVisible(ModelId id, bool visible)
{
    auto* entry = FindModel(id);
    if (!entry || entry->info.visible == visible)
        return;
    entry->info.visible = visible;
    if (!visible && pending_transform_ && pending_transform_->id == id)
        ReleaseCursor();
    ++model_state_revision_;
}

void SceneViewport::SetModelBoundingBoxVisible(ModelId id, bool visible)
{
    auto* entry = FindModel(id);
    if (!entry || entry->info.bounding_box_visible == visible)
        return;
    entry->info.bounding_box_visible = visible;
    ++model_state_revision_;
}

void SceneViewport::SetModelSelected(ModelId id, bool selected)
{
    if (IsTransformEditing()) return;
    auto* entry = FindModel(id);
    if (!entry)
        return;
    if (selected && active_model_id_ != id)
        ReleaseCursor();
    const bool changed = entry->info.selected != selected ||
                         (selected && active_model_id_ != id);
    entry->info.selected = selected;
    if (selected)
    {
        active_model_id_ = id;
        if (changed)
            entry->selection_order = next_selection_order_++;
    }
    else
    {
        if (pending_transform_ && pending_transform_->id == id)
            ReleaseCursor();
        if (active_model_id_ == id)
            ChooseActiveModel();
    }
    if (changed)
        ++model_state_revision_;
}

void SceneViewport::ClearModelSelection()
{
    if (IsTransformEditing()) return;
    bool changed = false;
    for (auto& entry : models_)
    {
        changed |= entry.info.selected;
        entry.info.selected = false;
    }
    ReleaseCursor();
    if (changed)
        ++model_state_revision_;
}

std::optional<glm::vec3> SceneViewport::ClosestScenePoint(
    const MouseRay& ray) const
{
    if (models_hidden_)
        return std::nullopt;
    const glm::vec3 segment = ray.end - ray.start;
    const float length_squared = glm::dot(segment, segment);
    if (length_squared <= 0.0f)
        return std::nullopt;

    // Bounds only prune the list; the pivot must be the real visible surface,
    // otherwise zoom and pan slow down by the gap between the box and mesh.
    struct Candidate
    {
        float entry = 0.0f;
        const ModelEntry* model = nullptr;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(models_.size());
    for (const auto& entry : models_)
    {
        if (!entry.info.visible)
            continue;
        glm::vec3 minimum(0.0f);
        glm::vec3 maximum(0.0f);
        entry.model->GetWorldBounds(minimum, maximum);
        float enter = 0.0f;
        float leave = 0.0f;
        if (SegmentIntersectsBox(ray.start, segment, minimum, maximum,
                                 enter, leave))
            candidates.push_back({enter, &entry});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b)
              { return a.entry < b.entry; });

    float nearest = FLT_MAX;
    glm::vec3 nearest_point(0.0f);

    // The plate is opaque from above and the main zoom surface there.
    const glm::vec2 plate_world(plate_size_mm_.x / BasePlate::kMmPerWorldUnit,
                                plate_size_mm_.y / BasePlate::kMmPerWorldUnit);
    if (segment.z != 0.0f)
    {
        const float t = -ray.start.z / segment.z;
        if (t >= 0.0f && t <= 1.0f)
        {
            const glm::vec3 point = ray.start + segment * t;
            if (point.x >= 0.0f && point.x <= plate_world.x &&
                point.y >= 0.0f && point.y <= plate_world.y)
            {
                nearest = t;
                nearest_point = point;
            }
        }
    }

    for (const Candidate& candidate : candidates)
    {
        if (candidate.entry > nearest)
            break;
        glm::vec3 hit(0.0f);
        if (!candidate.model->model->IntersectsSegment(
                ray.start, ray.end, &hit))
            continue;
        const float t = glm::dot(hit - ray.start, segment) / length_squared;
        if (t < nearest)
        {
            nearest = t;
            nearest_point = hit;
        }
    }

    if (nearest == FLT_MAX)
        return std::nullopt;
    return nearest_point;
}

std::optional<glm::vec3> SceneViewport::ZoomAnchorAt(
    const glm::vec2& position) const
{
    // Orthographic zoom is centered and already keeps a constant rate.
    if (camera_controller_.GetCamera().IsOrthographic())
        return std::nullopt;
    if (const auto ray = MouseRayAt(position))
        if (const auto hit = ClosestScenePoint(*ray))
            return hit;
    // Cursor over the background: zoom about the content in the view center
    // so the visible scene still scales at the same rate.
    int width = 0;
    int height = 0;
    glfwGetWindowSize(window_, &width, &height);
    if (width <= 0 || height <= 0)
        return std::nullopt;
    if (const auto ray = MouseRayAt(
            glm::vec2(width * 0.5f, height * 0.5f)))
        return ClosestScenePoint(*ray);
    return std::nullopt;
}

std::optional<SceneViewport::MouseRay> SceneViewport::MouseRayAt(
    const glm::vec2& position, bool require_inside) const
{
    if (!window_ || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        position.x <= -FLT_MAX || position.y <= -FLT_MAX)
        return std::nullopt;
    int width = 0, height = 0, framebuffer_width = 0, framebuffer_height = 0;
    glfwGetWindowSize(window_, &width, &height);
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    if (width <= 0 || height <= 0 || framebuffer_width <= 0 || framebuffer_height <= 0 ||
        (require_inside && (position.x < 0 || position.x >= width ||
                            position.y < 0 || position.y >= height)))
        return std::nullopt;
    const float aspect = static_cast<float>(framebuffer_width) / framebuffer_height;
    const glm::mat4 inverse_view_projection = glm::inverse(
        ProjectionMatrix(aspect) * camera_controller_.GetCamera().GetViewMatrix());
    const float x = 2.0f * position.x / width - 1.0f;
    const float y = 1.0f - 2.0f * position.y / height;
    const glm::vec4 near_point = inverse_view_projection * glm::vec4(x, y, -1, 1);
    const glm::vec4 far_point = inverse_view_projection * glm::vec4(x, y, 1, 1);
    if (std::abs(near_point.w) < 1.0e-8f || std::abs(far_point.w) < 1.0e-8f)
        return std::nullopt;
    return MouseRay{glm::vec3(near_point) / near_point.w, glm::vec3(far_point) / far_point.w};
}

SceneViewport::ModelEntry* SceneViewport::ClosestModel(
    const MouseRay& ray, glm::vec3* hit_point)
{
    if (models_hidden_)
        return nullptr;
    ModelEntry* closest = nullptr;
    float nearest_distance = FLT_MAX;
    for (auto& entry : models_)
    {
        glm::vec3 point(0.0f);
        if (!entry.info.visible ||
            !entry.model->IntersectsSegment(ray.start, ray.end, &point))
            continue;
        const glm::vec3 offset = point - ray.start;
        const float distance = glm::dot(offset, offset);
        if (distance < nearest_distance)
        {
            nearest_distance = distance;
            closest = &entry;
            if (hit_point)
                *hit_point = point;
        }
    }
    return closest;
}

bool SceneViewport::SelectModelAt(float x_pixels, float y_pixels, glm::vec3* hit_point)
{
    if (IsTransformEditing()) return false;
    const auto ray = MouseRayAt({x_pixels, y_pixels});
    if (!ray)
        return false;
    auto* hit = ClosestModel(*ray, hit_point);
    if (hit)
        SetModelSelected(hit->info.id, true);
    else
        ClearModelSelection();
    return hit != nullptr;
}

void SceneViewport::BeginModelDrag(ModelId id, const glm::vec2& position,
                                   const glm::vec3& hit_point)
{
    const auto ray = MouseRayAt(position);
    const auto* entry = FindModel(id);
    if (!ray || !entry || !entry->info.visible || !entry->info.selected)
        return;
    ModelDrag drag;
    drag.id = id;
    drag.anchor = hit_point;
    drag.initial_translation_mm = entry->model->Transform().translation_mm;
    const glm::vec3 direction = glm::normalize(ray->end - ray->start);
    // In edge-on standard views a horizontal plane is parallel to the ray.
    // Use a screen-facing plane there, and still apply only the XY component.
    if (std::abs(direction.z) < 1.0e-3f)
        drag.plane_normal = direction;
    camera_controller_.ReleaseCursor();
    if (BeginModelMove(id))
        model_drag_ = drag;
}

void SceneViewport::UpdateModelDrag(const glm::vec2& position)
{
    const auto ray = MouseRayAt(position, false);
    if (!ray || !model_drag_)
        return;
    auto* entry = FindModel(model_drag_->id);
    if (!entry)
        return;
    const ModelDrag& drag = *model_drag_;
    const glm::vec3 direction = ray->end - ray->start;
    const float denominator = glm::dot(direction, drag.plane_normal);
    if (std::abs(denominator) < glm::length(direction) * 1.0e-6f)
        return;
    const float distance = glm::dot(drag.anchor - ray->start, drag.plane_normal) / denominator;
    if (distance < 0.0f || !std::isfinite(distance))
        return;
    const glm::vec3 point = ray->start + direction * distance;
    const glm::vec3 world_delta = glm::vec3(glm::vec2(point - drag.anchor), 0) * BasePlate::kMmPerWorldUnit;
    const auto rotation = glm::mat3(ModelTransformMath::Rotation(entry->info.local_rotation_degrees));
    const glm::vec3 translation = drag.initial_translation_mm + glm::transpose(rotation) * world_delta;
    if (glm::length(translation - entry->info.local_translation_mm) < 1.0e-4f)
        return;
    MoveModelTo(entry->info.id, translation);
}

void SceneViewport::RefreshModelTransformInfo(ModelEntry& entry)
{
    auto& info = entry.info;
    glm::vec3 minimum, maximum;
    entry.model->GetWorldBounds(minimum, maximum);
    info.world_position_mm = (minimum + maximum) * (BasePlate::kMmPerWorldUnit * 0.5f);
    info.world_bounds_min_mm = minimum * BasePlate::kMmPerWorldUnit;
    // Avoid displaying negative zero from sub-micron floating-point noise at the plate.
    if (std::abs(info.world_bounds_min_mm.z) <= kPlateContactToleranceMm)
        info.world_bounds_min_mm.z = 0.0f;
    info.world_bounds_max_mm = maximum * BasePlate::kMmPerWorldUnit;
    const auto& transform = entry.model->Transform();
    info.local_translation_mm = transform.translation_mm;
    info.placement_center_mm = entry.model->PlacementCenterMm();
    info.local_scale = transform.scale;
    info.local_rotation_degrees = transform.rotation_degrees;
    ++info.transform_revision;
}

void SceneViewport::RefreshSceneBounds()
{
    if (models_.empty())
    {
        camera_controller_.SetSceneBounds(glm::vec3(0.0f), glm::vec3(0.0f));
        return;
    }
    glm::vec3 minimum(FLT_MAX), maximum(-FLT_MAX);
    for (const auto& entry : models_)
    {
        minimum = glm::min(minimum, entry.info.world_bounds_min_mm / BasePlate::kMmPerWorldUnit);
        maximum = glm::max(maximum, entry.info.world_bounds_max_mm / BasePlate::kMmPerWorldUnit);
    }
    camera_controller_.SetSceneBounds(minimum, maximum);
}

void SceneViewport::RemoveStl(ModelId id)
{
    if (!FindModel(id))
        return;
    ReleaseCursor();
    auto* entry = FindModel(id);
    Operation operation;
    operation.kind = OperationKind::Delete;
    operation.model = CaptureModel(*entry);
    operation.before_revision = project_revision_;
    operation.description = QStringLiteral("删除 STL：%1")
        .arg(entry->info.name);
    EraseModel(id);
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    RecordOperation(std::move(operation));
}

SceneViewport::SavedModel SceneViewport::CaptureModel(const ModelEntry& entry) const
{
    const auto index = static_cast<std::size_t>(&entry - models_.data());
    return {entry.info, entry.stl_data, entry.selection_order, index};
}

bool SceneViewport::RestoreModel(const SavedModel& saved, QString& error)
{
    return RestoreModels({saved}, error);
}

bool SceneViewport::BuildModelEntry(const SavedModel& saved, ModelEntry& entry, QString& error)
{
    glfwMakeContextCurrent(window_);
    auto model = std::make_unique<StlModel>();
    std::string load_error;
    if (!model->LoadFromMemory(saved.stl_data.constData(), saved.stl_data.size(), load_error))
    {
        error = QStringLiteral("无法恢复 STL：%1").arg(QString::fromLocal8Bit(load_error.c_str()));
        return false;
    }
    model->SetPlacementCenterMm(saved.info.placement_center_mm);
    model->SetTransform({saved.info.local_translation_mm, saved.info.local_scale,
                         saved.info.local_rotation_degrees});
    entry = {std::move(model), saved.info, saved.selection_order, saved.stl_data};
    RefreshModelTransformInfo(entry);
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(entry.info.world_bounds_min_mm[axis]) ||
            !std::isfinite(entry.info.world_bounds_max_mm[axis]))
        {
            error = QStringLiteral("模型变换超出有效范围，无法生成副本。");
            return false;
        }
    return true;
}

bool SceneViewport::RestoreModels(const std::vector<SavedModel>& saved, QString& error)
{
    std::vector<ModelEntry> restored;
    restored.reserve(saved.size());
    for (const auto& model : saved)
    {
        ModelEntry entry;
        if (!BuildModelEntry(model, entry, error)) return false;
        restored.push_back(std::move(entry));
    }
    models_.reserve(models_.size() + saved.size());
    for (std::size_t i = 0; i < saved.size(); ++i)
        models_.insert(models_.begin() + std::min(saved[i].index, models_.size()), std::move(restored[i]));
    ChooseActiveModel();
    RefreshSceneBounds();
    ++model_state_revision_;
    return true;
}

void SceneViewport::RestoreSelection(const std::vector<SavedSelection>& selection, ModelId active_id)
{
    for (auto& entry : models_) entry.info.selected = false;
    for (const auto& saved : selection)
        if (auto* entry = FindModel(saved.id))
        {
            entry->info.selected = true;
            entry->selection_order = saved.order;
        }
    active_model_id_ = active_id;
    ChooseActiveModel();
    ++model_state_revision_;
}

void SceneViewport::EraseModel(ModelId id)
{
    const auto found = std::find_if(models_.begin(), models_.end(),
        [id](const ModelEntry& entry) { return entry.info.id == id; });
    if (found == models_.end())
        return;
    if (id == transform_editing_model_id_) EndTransformEditing();
    glfwMakeContextCurrent(window_);
    models_.erase(found);
    if (active_model_id_ == id)
        ChooseActiveModel();
    RefreshSceneBounds();
    ++model_state_revision_;
}

void SceneViewport::AdvanceProjectRevision()
{
    // State identities are never reused when a new branch replaces redo steps.
    project_revision_ = next_project_revision_++;
}

void SceneViewport::AppendHistory(const QString& description)
{
    operation_history_.push_back(description);
    ++history_revision_;
}

void SceneViewport::RecordOperation(Operation operation)
{
    operations_.erase(operations_.begin() + operation_cursor_, operations_.end());
    AppendHistory(operation.description);
    operations_.push_back(std::move(operation));
    operation_cursor_ = operations_.size();
}

void SceneViewport::ClearHistory()
{
    operations_.clear();
    operation_cursor_ = 0;
    operation_history_.clear();
    ++history_revision_;
}

bool SceneViewport::CanUndo() const
{
    const auto* entry = pending_transform_ ? FindModel(pending_transform_->id) : nullptr;
    return operation_cursor_ > 0 || (entry && entry->model->Transform() != pending_transform_->before_transform);
}

bool SceneViewport::CanRedo() const
{
    const auto* entry = pending_transform_ ? FindModel(pending_transform_->id) : nullptr;
    return operation_cursor_ < operations_.size() &&
        (!entry || entry->model->Transform() == pending_transform_->before_transform);
}

bool SceneViewport::ApplyTransform(ModelEntry& entry, const ModelTransform& transform)
{
    if (!ModelTransformMath::IsValid(transform)) return false;
    const auto previous = entry.model->Transform();
    if (previous == transform) return true;
    entry.model->SetTransform(transform);
    glm::vec3 minimum, maximum;
    entry.model->GetWorldBounds(minimum, maximum);
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(minimum[axis] * BasePlate::kMmPerWorldUnit) ||
            !std::isfinite(maximum[axis] * BasePlate::kMmPerWorldUnit))
        {
            entry.model->SetTransform(previous);
            return false;
        }
    RefreshModelTransformInfo(entry);
    RefreshSceneBounds();
    return true;
}

bool SceneViewport::IsModelPositionAllowed(const glm::vec3& position_mm) const
{
    // The usable platform spans [0, size]. Allow one more platform size in
    // each direction, so X is [-length, 2 * length] and likewise for Y and Z.
    return position_mm.x >= -plate_size_mm_.x &&
        position_mm.x <= 2.0f * plate_size_mm_.x &&
        position_mm.y >= -plate_size_mm_.y &&
        position_mm.y <= 2.0f * plate_size_mm_.y &&
        position_mm.z >= -plate_size_mm_.z &&
        position_mm.z <= 2.0f * plate_size_mm_.z;
}

bool SceneViewport::ApplyTransforms(const std::vector<ModelTransformChange>& changes, bool forward)
{
    for (const auto& change : changes)
        if (!FindModel(change.id) || !ModelTransformMath::IsValid(forward ? change.after : change.before))
            return false;
    std::vector<ModelTransform> previous;
    previous.reserve(changes.size());
    for (std::size_t i = 0; i < changes.size(); ++i)
    {
        auto* entry = FindModel(changes[i].id);
        previous.push_back(entry->model->Transform());
        if (!ApplyTransform(*entry, forward ? changes[i].after : changes[i].before))
        {
            // A rejected member must not leave the rest of the selection moved.
            for (std::size_t restored = 0; restored < i; ++restored)
                ApplyTransform(*FindModel(changes[restored].id), previous[restored]);
            return false;
        }
    }
    return true;
}

bool SceneViewport::BeginModelTransform(ModelId id, TransformMode mode)
{
    EndModelTransform();
    const auto* entry = FindModel(id);
    if (!entry) return false;
    if (models_hidden_) SetModelsHidden(false);
    pending_transform_ = PendingTransform{id, mode, entry->model->Transform(), project_revision_};
    return true;
}

bool SceneViewport::TransformModelTo(ModelId id, const ModelTransform& transform, TransformMode mode)
{
    if (pending_transform_ && (pending_transform_->id != id || pending_transform_->mode != mode))
        EndModelTransform();
    auto* entry = FindModel(id);
    if (!entry) return false;
    const auto before = entry->model->Transform();
    const auto before_revision = project_revision_;
    if (!ApplyTransform(*entry, transform)) return false;
    if (before == transform) return true;
    if (mode == TransformMode::Translate &&
        !IsModelPositionAllowed(entry->info.world_position_mm))
    {
        ApplyTransform(*entry, before);
        return false;
    }
    AdvanceProjectRevision();
    if (!pending_transform_)
    {
        pending_transform_ = PendingTransform{id, mode, before, before_revision};
        EndModelTransform();
    }
    return true;
}

bool SceneViewport::MoveModelTo(ModelId id, const glm::vec3& translation_mm)
{
    const auto* entry = FindModel(id);
    if (!entry) return false;
    auto transform = entry->model->Transform();
    transform.translation_mm = translation_mm;
    return TransformModelTo(id, transform, TransformMode::Translate);
}

void SceneViewport::EndModelTransform()
{
    model_drag_.reset();
    if (!pending_transform_) return;
    const PendingTransform pending = *pending_transform_;
    pending_transform_.reset();
    auto* entry = FindModel(pending.id);
    if (!entry) return;
    const auto after = entry->model->Transform();
    if (ModelTransformMath::NearlyEqual(after, pending.before_transform))
    {
        ApplyTransform(*entry, pending.before_transform);
        project_revision_ = pending.before_revision;
        return;
    }
    Operation operation;
    operation.kind = OperationKind::Transform;
    operation.model.info.id = pending.id;
    operation.before_transform = pending.before_transform;
    operation.after_transform = after;
    operation.before_revision = pending.before_revision;
    operation.after_revision = project_revision_;
    const auto format = [](const glm::vec3& value)
    {
        return QStringLiteral("(%1, %2, %3)").arg(value.x, 0, 'f', 3)
            .arg(value.y, 0, 'f', 3).arg(value.z, 0, 'f', 3);
    };
    const QString name = entry->info.name;
    if (pending.mode == TransformMode::Translate)
        operation.description = QStringLiteral("移动 STL：%1 | 位移 %2 → %3 mm")
            .arg(name, format(pending.before_transform.translation_mm), format(after.translation_mm));
    else if (pending.mode == TransformMode::Rotate)
        operation.description = QStringLiteral("旋转 STL：%1 | 角度 %2 → %3 °")
            .arg(name, format(pending.before_transform.rotation_degrees), format(after.rotation_degrees));
    else
        operation.description = QStringLiteral("缩放 STL：%1 | 倍数 %2 → %3")
            .arg(name, format(pending.before_transform.scale), format(after.scale));
    RecordOperation(std::move(operation));
}

bool SceneViewport::BeginTransformEditing(ModelId id, TransformMode mode)
{
    const auto* entry = FindModel(id);
    if (!entry || !entry->info.selected || (IsTransformEditing() && transform_editing_model_id_ != id))
        return false;
    if (models_hidden_) SetModelsHidden(false);
    SetTransformMode(mode);
    const bool starting = !IsTransformEditing();
    transform_editing_model_id_ = id;
    active_model_id_ = id;
    if (starting) ++model_state_revision_;
    return true;
}

void SceneViewport::EndTransformEditing()
{
    ReleaseCursor();
    if (!IsTransformEditing()) return;
    transform_editing_model_id_ = 0;
    ++model_state_revision_;
}

void SceneViewport::SetTransformMode(TransformMode mode)
{
    ReleaseCursor();
    transform_mode_ = mode;
}

std::optional<glm::mat4> SceneViewport::ModelGizmoMatrix(ModelId id) const
{
    const auto* entry = FindModel(id);
    if (!entry) return std::nullopt;
    const glm::vec3 pivot(entry->info.placement_center_mm, entry->info.size_mm.z * 0.5f);
    return ModelTransformMath::GizmoMatrix(entry->model->Transform(), pivot / BasePlate::kMmPerWorldUnit);
}

bool SceneViewport::TransformModelFromGizmo(ModelId id, const glm::mat4& matrix, TransformMode mode)
{
    const auto* entry = FindModel(id);
    if (!entry) return false;
    auto transform = entry->model->Transform();
    const glm::vec3 pivot(entry->info.placement_center_mm, entry->info.size_mm.z * 0.5f);
    if (!ModelTransformMath::FromGizmo(matrix, pivot / BasePlate::kMmPerWorldUnit, mode, transform))
        return false;
    return TransformModelTo(id, transform, mode);
}

bool SceneViewport::DrawModelGizmo(const glm::mat4& view, const glm::mat4& projection, bool over_cube)
{
    if (models_hidden_)
    {
        if (gizmo_dragging_) ReleaseCursor();
        return false;
    }
    const auto* entry = FindModel(transform_editing_model_id_);
    if (!entry || !entry->info.selected || !entry->info.visible || model_drag_)
    {
        if (gizmo_dragging_) ReleaseCursor();
        return false;
    }
    const auto id = entry->info.id;
    auto matrix = *ModelGizmoMatrix(id);
    const auto clip = projection * view * matrix[3];
    if (!camera_controller_.GetCamera().IsOrthographic() && clip.z < 0.001f && !gizmo_dragging_)
        return false;
    const ImGuiIO& io = ImGui::GetIO();
    const auto operation = transform_mode_ == TransformMode::Translate ? ImGuizmo::TRANSLATE :
        transform_mode_ == TransformMode::Rotate ?
        static_cast<ImGuizmo::OPERATION>(ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z) : ImGuizmo::SCALE;
    ImGuizmo::PushID("modelTransform");
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::SetOrthographic(camera_controller_.GetCamera().IsOrthographic());
    ImGuizmo::SetGizmoSizeClipSpace(0.15f);
    ImGuizmo::Enable(!over_cube && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
        operation, ImGuizmo::LOCAL, glm::value_ptr(matrix));
    const bool using_gizmo = ImGuizmo::IsUsing();
    if (using_gizmo && !gizmo_dragging_)
    {
        camera_controller_.ReleaseCursor();
        gizmo_dragging_ = BeginModelTransform(id, transform_mode_);
    }
    if (changed) TransformModelFromGizmo(id, matrix, transform_mode_);
    if (!using_gizmo && gizmo_dragging_)
    {
        EndModelTransform();
        gizmo_dragging_ = false;
    }
    const bool over = ImGuizmo::IsOver(operation) || using_gizmo;
    ImGuizmo::PopID();
    return over;
}

bool SceneViewport::Undo(QString& error)
{
    error.clear();
    ReleaseCursor();
    if (!CanUndo())
        return false;
    auto& operation = operations_[operation_cursor_ - 1];
    switch (operation.kind)
    {
    case OperationKind::Rename:
    {
        auto* entry = FindModel(operation.model.info.id);
        if (!entry) return false;
        entry->info.name = operation.before_name;
        ++model_state_revision_;
        break;
    }
    case OperationKind::TransformModels:
        if (!ApplyTransforms(operation.transforms, false)) return false;
        break;
    case OperationKind::AddModels:
    {
        for (const auto& saved : operation.added_models)
            if (!FindModel(saved.info.id)) return false;
        for (auto& saved : operation.added_models)
            saved = CaptureModel(*FindModel(saved.info.id));
        for (auto it = operation.added_models.rbegin(); it != operation.added_models.rend(); ++it)
            EraseModel(it->info.id);
        RestoreSelection(operation.previous_selection, operation.previous_active_id);
        break;
    }
    case OperationKind::Import:
    {
        const auto* entry = FindModel(operation.model.info.id);
        if (!entry)
            return false;
        operation.model = CaptureModel(*entry);
        EraseModel(entry->info.id);
        break;
    }
    case OperationKind::Delete:
        if (!RestoreModel(operation.model, error))
            return false;
        break;
    case OperationKind::Transform:
    {
        auto* entry = FindModel(operation.model.info.id);
        if (!entry || !ApplyTransform(*entry, operation.before_transform))
            return false;
        break;
    }
    case OperationKind::PlateSize:
        ApplyPlateSizeMm(operation.before_plate_size);
        ResetView();
        break;
    }
    project_revision_ = operation.before_revision;
    --operation_cursor_;
    AppendHistory(QStringLiteral("撤销：%1").arg(operation.description));
    return true;
}

bool SceneViewport::Redo(QString& error)
{
    error.clear();
    ReleaseCursor();
    if (!CanRedo())
        return false;
    auto& operation = operations_[operation_cursor_];
    switch (operation.kind)
    {
    case OperationKind::Rename:
    {
        auto* entry = FindModel(operation.model.info.id);
        if (!entry) return false;
        entry->info.name = operation.after_name;
        ++model_state_revision_;
        break;
    }
    case OperationKind::TransformModels:
        if (!ApplyTransforms(operation.transforms, true)) return false;
        break;
    case OperationKind::AddModels:
        if (!RestoreModels(operation.added_models, error)) return false;
        for (const auto& selected : operation.previous_selection)
            if (auto* entry = FindModel(selected.id)) entry->info.selected = false;
        ChooseActiveModel();
        ++model_state_revision_;
        break;
    case OperationKind::Import:
        if (!RestoreModel(operation.model, error))
            return false;
        break;
    case OperationKind::Delete:
    {
        const auto* entry = FindModel(operation.model.info.id);
        if (!entry)
            return false;
        operation.model = CaptureModel(*entry);
        EraseModel(entry->info.id);
        break;
    }
    case OperationKind::Transform:
    {
        auto* entry = FindModel(operation.model.info.id);
        if (!entry || !ApplyTransform(*entry, operation.after_transform))
            return false;
        break;
    }
    case OperationKind::PlateSize:
        ApplyPlateSizeMm(operation.after_plate_size);
        ResetView();
        break;
    }
    project_revision_ = operation.after_revision;
    ++operation_cursor_;
    AppendHistory(QStringLiteral("回撤：%1").arg(operation.description));
    return true;
}

void SceneViewport::ApplyPlateSizeMm(const glm::vec3& size_mm)
{
    glfwMakeContextCurrent(window_);
    base_plate_->SetSize(size_mm);
    plate_size_mm_ = size_mm;
    camera_controller_.SetPlateSizeMm(size_mm);
    RefreshSceneBounds();
}

bool SceneViewport::SetPlateSizeMm(const glm::vec3& size_mm, QString& error)
{
    error.clear();
    if (!PlateDimensions::IsValid(size_mm))
    {
        error = QStringLiteral(
            "平台长度、宽度和高度必须在 1 至 10000 mm 之间。");
        return false;
    }
    if (!base_plate_ || graphics_released_)
    {
        error = QStringLiteral("场景尚未就绪，无法修改平台尺寸。");
        return false;
    }
    ReleaseCursor();
    if (size_mm == plate_size_mm_)
        return true;
    Operation operation;
    operation.kind = OperationKind::PlateSize;
    operation.before_plate_size = plate_size_mm_;
    operation.after_plate_size = size_mm;
    operation.before_revision = project_revision_;
    operation.description = QStringLiteral(
        "修改平台尺寸：%1 × %2 × %3 mm → %4 × %5 × %6 mm")
        .arg(plate_size_mm_.x, 0, 'f', 2)
        .arg(plate_size_mm_.y, 0, 'f', 2)
        .arg(plate_size_mm_.z, 0, 'f', 2)
        .arg(size_mm.x, 0, 'f', 2)
        .arg(size_mm.y, 0, 'f', 2)
        .arg(size_mm.z, 0, 'f', 2);
    ApplyPlateSizeMm(size_mm);
    ResetView();
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    RecordOperation(std::move(operation));
    return true;
}

bool SceneViewport::PrepareSlice(const SliceSettings& settings,
                                 SliceEstimate& estimate,
                                 QString& error) const
{
    error.clear();
    estimate = {};
    if (models_.empty())
    {
        error = QStringLiteral("没有可切片的模型，请先导入 STL 模型。");
        return false;
    }
    const double layer_thickness_mm = settings.layer_thickness_mm;
    if (!std::isfinite(layer_thickness_mm) ||
        layer_thickness_mm < kMinLayerThicknessMm ||
        layer_thickness_mm > kMaxLayerThicknessMm)
    {
        error = QStringLiteral("层厚必须在 %1 到 %2 mm 之间。")
            .arg(kMinLayerThicknessMm, 0, 'f', 3)
            .arg(kMaxLayerThicknessMm, 0, 'f', 1);
        return false;
    }
    if (settings.hatch.enabled)
    {
        if (!std::isfinite(settings.hatch.spacing_mm) ||
            settings.hatch.spacing_mm < kMinHatchSpacingMm ||
            settings.hatch.spacing_mm > kMaxHatchSpacingMm)
        {
            error = QStringLiteral("扫描间距必须在 %1 到 %2 mm 之间。")
                .arg(kMinHatchSpacingMm, 0, 'f', 3)
                .arg(kMaxHatchSpacingMm, 0, 'f', 1);
            return false;
        }
        if (!std::isfinite(settings.hatch.angle_deg) ||
            !std::isfinite(settings.hatch.layer_angle_increment_deg))
        {
            error = QStringLiteral("扫描角度和层间角度增量必须是有限数值。");
            return false;
        }
        if (!std::isfinite(settings.hatch.spot_compensation_mm) ||
            settings.hatch.spot_compensation_mm < 0.0)
        {
            error = QStringLiteral("光斑补偿必须是非负有限数值。");
            return false;
        }
    }

    constexpr double kBoundsToleranceMm = 1.0e-3;
    QStringList outside_models;
    std::uint64_t triangle_count = 0;
    double max_z_mm = 0.0;
    for (const auto& entry : models_)
    {
        const auto& info = entry.info;
        if (info.world_bounds_min_mm.x < -kBoundsToleranceMm ||
            info.world_bounds_min_mm.y < -kBoundsToleranceMm ||
            info.world_bounds_max_mm.x > plate_size_mm_.x + kBoundsToleranceMm ||
            info.world_bounds_max_mm.y > plate_size_mm_.y + kBoundsToleranceMm)
        {
            outside_models.append(info.name);
        }
        triangle_count += static_cast<std::uint64_t>(info.triangle_count);
        max_z_mm = std::max(max_z_mm, static_cast<double>(info.world_bounds_max_mm.z));
    }

    if (!outside_models.isEmpty())
    {
        error = QStringLiteral("以下模型超出平台范围，请先移回平台内再切片：\n%1")
            .arg(outside_models.join(QStringLiteral("\n")));
        return false;
    }
    if (max_z_mm <= 0.0)
    {
        error = QStringLiteral("所有模型都在平台下方，没有可切片的 Z 范围。");
        return false;
    }

    estimate.max_z_mm = max_z_mm;
    estimate.triangle_count = static_cast<std::size_t>(
        std::min<std::uint64_t>(triangle_count,
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
    const double estimated = std::ceil(max_z_mm / layer_thickness_mm);
    estimate.estimated_layers = estimated <= 0.0
        ? 1
        : static_cast<std::size_t>(estimated);
    if (estimate.estimated_layers > kMaxSliceLayers)
    {
        error = QStringLiteral("预计层数超过 %1，请增大层厚后再切片。")
            .arg(static_cast<qulonglong>(kMaxSliceLayers));
        return false;
    }
    return true;
}

bool SceneViewport::GenerateSlice(const SliceSettings& settings,
                                  const SliceProgressCallback& progress,
                                  bool& cancelled, QString& error, QStringList& issues)
{
    cancelled = false;
    error.clear();
    issues.clear();

    SliceEstimate estimate;
    if (!PrepareSlice(settings, estimate, error))
        return false;

    std::vector<SliceModelInput> inputs;
    inputs.reserve(models_.size());
    for (const auto& entry : models_)
    {
        SliceModelInput input;
        input.id = entry.info.id;
        input.name = entry.info.name.toUtf8().toStdString();
        entry.model->AppendBuildTrianglesMm(input.triangles);
        inputs.push_back(std::move(input));
    }

    SliceResult result;
    try
    {
        result = SliceBuild(inputs, settings, progress);
    }
    catch (const std::exception& exception)
    {
        error = QStringLiteral("切片失败：%1")
            .arg(QString::fromUtf8(exception.what()));
        return false;
    }

    if (result.cancelled)
    {
        cancelled = true;
        return false;
    }
    for (const SliceIssue& issue : result.issues)
    {
        issues.append(QStringLiteral("%1：%2")
            .arg(QString::fromUtf8(issue.name.c_str()),
                 QString::fromUtf8(issue.message.c_str())));
    }
    if (!result.ok)
    {
        QString message = result.error.empty()
            ? QStringLiteral("没有生成任何有效切片。")
            : QString::fromUtf8(result.error.c_str());
        if (!issues.isEmpty())
            message += QStringLiteral("\n\n") + issues.join(QStringLiteral("\n"));
        error = message;
        return false;
    }

    slice_layers_ = std::move(result.layers);
    slice_total_contour_path_length_mm_ = result.total_contour_path_length_mm;
    slice_total_hatch_path_length_mm_ = result.total_hatch_path_length_mm;
    slice_total_path_length_mm_ = result.total_path_length_mm;
    slice_current_layer_ = 0;
    slice_visible_ = true;
    slice_contours_visible_ = true;
    slice_hatches_visible_ = true;
    slice_direction_visible_ = false;
    slice_jumps_visible_ = false;
    models_hidden_ = false;
    slice_applied_settings_ = settings;
    slice_source_project_revision_ = project_revision_;
    slice_invalidated_ = false;
    UploadSliceLayer();
    ++slice_revision_;
    return true;
}

void SceneViewport::ClearSlicePreview()
{
    if (slice_layers_.empty() && !slice_visible_)
        return;
    if (window_ != nullptr && glad_ready_)
        glfwMakeContextCurrent(window_);
    slice_layers_.clear();
    slice_current_layer_ = 0;
    slice_visible_ = false;
    slice_contours_visible_ = true;
    slice_hatches_visible_ = true;
    slice_direction_visible_ = false;
    slice_jumps_visible_ = false;
    models_hidden_ = false;
    slice_total_contour_path_length_mm_ = 0.0;
    slice_total_hatch_path_length_mm_ = 0.0;
    slice_total_path_length_mm_ = 0.0;
    slice_contour_vertex_count_ = 0;
    slice_hatch_vertex_count_ = 0;
    slice_arrow_vertex_count_ = 0;
    slice_jump_vertex_count_ = 0;
    slice_jump_count_ = 0;
    slice_jump_path_length_mm_ = 0.0;
    slice_source_project_revision_ = 0;
    ++slice_revision_;
}

void SceneViewport::SetSlicePreviewVisible(bool visible)
{
    const bool next = visible && !slice_layers_.empty();
    if (slice_visible_ == next)
        return;
    slice_visible_ = next;
    ++slice_revision_;
}

void SceneViewport::SetSliceContoursVisible(bool visible)
{
    if (slice_layers_.empty() || slice_contours_visible_ == visible)
        return;
    slice_contours_visible_ = visible;
    ++slice_revision_;
}

void SceneViewport::SetSliceHatchesVisible(bool visible)
{
    if (slice_layers_.empty() || slice_hatches_visible_ == visible)
        return;
    slice_hatches_visible_ = visible;
    ++slice_revision_;
}

void SceneViewport::SetSliceDirectionVisible(bool visible)
{
    if (slice_layers_.empty() || slice_direction_visible_ == visible)
        return;
    slice_direction_visible_ = visible;
    ++slice_revision_;
}

void SceneViewport::SetSliceJumpsVisible(bool visible)
{
    if (slice_layers_.empty() || slice_jumps_visible_ == visible)
        return;
    slice_jumps_visible_ = visible;
    ++slice_revision_;
}

void SceneViewport::SetModelsHidden(bool hidden)
{
    if (models_hidden_ == hidden)
        return;
    if (hidden)
        EndModelTransform();
    models_hidden_ = hidden;
    ++slice_revision_;
}

bool SceneViewport::SetSliceLayer(std::size_t index)
{
    if (slice_layers_.empty() || index == 0 || index > slice_layers_.size())
        return false;
    const std::size_t next = index - 1;
    if (next == slice_current_layer_)
        return true;
    slice_current_layer_ = next;
    UploadSliceLayer();
    ++slice_revision_;
    return true;
}

SceneViewport::SlicePreviewInfo SceneViewport::SliceInfo() const
{
    SlicePreviewInfo info;
    info.has_preview = !slice_layers_.empty();
    info.visible = slice_visible_ && info.has_preview;
    info.contours_visible = slice_contours_visible_ && info.has_preview;
    info.hatches_visible = slice_hatches_visible_ && info.has_preview;
    info.direction_visible = slice_direction_visible_ && info.has_preview;
    info.jumps_visible = slice_jumps_visible_ && info.has_preview;
    info.models_hidden = models_hidden_;
    info.layer_count = slice_layers_.size();
    info.total_contour_path_length_mm = slice_total_contour_path_length_mm_;
    info.total_hatch_path_length_mm = slice_total_hatch_path_length_mm_;
    info.total_path_length_mm = slice_total_path_length_mm_;
    info.hatch = slice_applied_settings_.hatch;
    if (info.has_preview && slice_current_layer_ < slice_layers_.size())
    {
        const SliceLayer& layer = slice_layers_[slice_current_layer_];
        info.layer_index = slice_current_layer_ + 1;
        info.z_mm = layer.z_mm;
        info.contour_count = layer.contours.size();
        info.hatch_count = layer.hatches.size();
        info.jump_count = slice_jump_count_;
        info.layer_contour_path_length_mm = layer.contour_path_length_mm;
        info.layer_hatch_path_length_mm = layer.hatch_path_length_mm;
        info.layer_jump_path_length_mm = slice_jump_path_length_mm_;
        info.layer_path_length_mm = layer.path_length_mm;
    }
    return info;
}

bool SceneViewport::ConsumeSliceInvalidation()
{
    const bool invalidated = slice_invalidated_;
    slice_invalidated_ = false;
    return invalidated;
}

void SceneViewport::UploadSliceLayer()
{
    slice_contour_vertex_count_ = 0;
    slice_hatch_vertex_count_ = 0;
    slice_arrow_vertex_count_ = 0;
    slice_jump_vertex_count_ = 0;
    slice_jump_count_ = 0;
    slice_jump_path_length_mm_ = 0.0;
    if (slice_layers_.empty() || slice_current_layer_ >= slice_layers_.size() ||
        window_ == nullptr || !glad_ready_ || slice_vao_ == 0)
        return;

    glfwMakeContextCurrent(window_);
    const SliceLayer& layer = slice_layers_[slice_current_layer_];
    const glm::vec3 contour_color(0.0f, 0.85f, 1.0f);
    const glm::vec3 hatch_color(0.95f, 0.10f, 0.70f);
    const glm::vec3 arrow_color(0.98f, 0.98f, 0.98f);
    const glm::vec3 jump_color(0.15f, 1.0f, 0.35f);
    const float z = static_cast<float>(
        layer.z_mm / BasePlate::kMmPerWorldUnit);
    std::vector<float> vertices;
    auto add_segment = [&](const std::array<double, 2>& a,
                           const std::array<double, 2>& b,
                           const glm::vec3& color)
    {
        const float points[] = {
            static_cast<float>(a[0] / BasePlate::kMmPerWorldUnit),
            static_cast<float>(a[1] / BasePlate::kMmPerWorldUnit),
            z,
            static_cast<float>(b[0] / BasePlate::kMmPerWorldUnit),
            static_cast<float>(b[1] / BasePlate::kMmPerWorldUnit),
            z
        };
        for (int point = 0; point < 2; ++point)
        {
            const int offset = point * 3;
            vertices.insert(vertices.end(), {
                points[offset], points[offset + 1], points[offset + 2],
                color.x, color.y, color.z
            });
        }
    };

    for (const SlicePath& path : layer.contours)
        for (std::size_t i = 1; i < path.points.size(); ++i)
            add_segment(path.points[i - 1], path.points[i], contour_color);
    const int contour_vertices = static_cast<int>(vertices.size() / 6);

    for (const SliceHatchLine& line : layer.hatches)
        add_segment(line.start, line.end, hatch_color);
    const int hatch_vertices =
        static_cast<int>(vertices.size() / 6) - contour_vertices;

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kArrowAngle = 30.0 * kPi / 180.0;
    // Keep every arrowhead the same physical size; only the line length
    // varies between scan vectors.
    constexpr double kArrowWingMm = 0.1;
    for (const SliceHatchLine& line : layer.hatches)
    {
        const double dx = line.end[0] - line.start[0];
        const double dy = line.end[1] - line.start[1];
        const double length = std::hypot(dx, dy);
        if (length <= 1.0e-9)
            continue;
        const double ux = dx / length;
        const double uy = dy / length;
        const double back = kArrowWingMm * std::cos(kArrowAngle);
        const double side = kArrowWingMm * std::sin(kArrowAngle);
        const std::array<double, 2> tip = line.end;
        const std::array<double, 2> left{
            tip[0] - ux * back - uy * side,
            tip[1] - uy * back + ux * side};
        const std::array<double, 2> right{
            tip[0] - ux * back + uy * side,
            tip[1] - uy * back - ux * side};
        add_segment(left, tip, arrow_color);
        add_segment(right, tip, arrow_color);
    }
    const int arrow_vertices =
        static_cast<int>(vertices.size() / 6) - contour_vertices - hatch_vertices;

    for (std::size_t i = 1; i < layer.hatches.size(); ++i)
    {
        const SliceHatchLine& previous = layer.hatches[i - 1];
        const SliceHatchLine& next = layer.hatches[i];
        if (previous.model_id != next.model_id)
            continue;
        const std::array<double, 2>& start = previous.end;
        const std::array<double, 2>& end = next.start;
        const double dx = end[0] - start[0];
        const double dy = end[1] - start[1];
        const double length = std::hypot(dx, dy);
        if (length <= 1.0e-9)
            continue;
        ++slice_jump_count_;
        slice_jump_path_length_mm_ += length;
        add_segment(start, end, jump_color);
    }
    const int jump_vertices = static_cast<int>(vertices.size() / 6) -
                              contour_vertices - hatch_vertices -
                              arrow_vertices;

    glBindVertexArray(slice_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, slice_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.empty() ? nullptr : vertices.data(), GL_STREAM_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    slice_contour_vertex_count_ = contour_vertices;
    slice_hatch_vertex_count_ = hatch_vertices;
    slice_arrow_vertex_count_ = arrow_vertices;
    slice_jump_vertex_count_ = jump_vertices;
}

void SceneViewport::ResetView()
{
    ReleaseCursor();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width > 0 && height > 0)
        camera_controller_.ResetView(static_cast<float>(width) / height);
}

void SceneViewport::ResetProjectDisplay()
{
    ClearSlicePreview();
    EndTransformEditing();
    transform_mode_ = TransformMode::Translate;
    camera_controller_.GetCamera() = Camera();
    wireframe_ = false;
    std::fill(std::begin(clear_color_), std::end(clear_color_), 1.0f);
    ResetView();
    // During startup the first frame refits Home to the final embedded viewport size.
    // For an existing editor ResetView already uses its current viewport dimensions.
}

ProjectData SceneViewport::SnapshotProject() const
{
    ProjectData project;
    project.plate_size_mm = plate_size_mm_;
    project.models.reserve(models_.size());
    for (const auto& entry : models_)
    {
        const auto& info = entry.info;
        project.models.push_back({QString::fromUtf8(info.path.c_str()), entry.stl_data,
            info.local_translation_mm, info.local_scale, info.local_rotation_degrees, info.placement_center_mm, info.name});
    }
    return project;
}

bool SceneViewport::ApplyProject(const ProjectData& project, QString& error)
{
    if (!ProjectFile::Validate(project, error))
        return false;
    glfwMakeContextCurrent(window_);
    // Build the complete replacement first. A bad embedded STL keeps the old scene intact.
    std::vector<ModelEntry> replacement;
    replacement.reserve(project.models.size());
    ModelId next_id = next_model_id_;
    QSet<QString> names;
    for (const auto& saved : project.models)
        if (!saved.name.isEmpty()) names.insert(saved.name.toCaseFolded());
    for (const auto& saved : project.models)
    {
        auto model = std::make_unique<StlModel>();
        std::string load_error;
        if (!model->LoadFromMemory(saved.stl_data.constData(), saved.stl_data.size(), load_error))
        {
            error = QStringLiteral("无法恢复模型 %1：%2")
                .arg(QFileInfo(saved.source_path).fileName(), QString::fromLocal8Bit(load_error.c_str()));
            return false;
        }
        model->SetPlacementCenterMm(saved.placement_center_mm);
        model->SetTransform({saved.translation_mm, saved.scale, saved.rotation_degrees});
        StlModelInfo info{saved.source_path.toUtf8().toStdString(),
                          model->SizeMm(), model->TriangleCount()};
        info.id = next_id++;
        info.name = saved.name.isEmpty()
            ? ModelNames::Unique(ModelNames::FromPath(saved.source_path), names) : saved.name;
        info.source_file_size = static_cast<std::size_t>(saved.stl_data.size());
        replacement.push_back({std::move(model), std::move(info), 0, saved.stl_data});
        RefreshModelTransformInfo(replacement.back());
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(replacement.back().info.world_bounds_min_mm[axis]) ||
                !std::isfinite(replacement.back().info.world_bounds_max_mm[axis]))
            {
                error = QStringLiteral("模型变换超出支持的坐标范围。");
                return false;
            }
    }
    ReleaseCursor();
    pending_pick_.reset();
    ApplyPlateSizeMm(project.plate_size_mm);
    models_ = std::move(replacement);
    next_model_id_ = next_id;
    active_model_id_ = 0;
    ChooseActiveModel();
    RefreshSceneBounds();
    ResetProjectDisplay();
    ClearHistory();
    AdvanceProjectRevision();
    ++model_state_revision_;
    return true;
}

void SceneViewport::NewProject()
{
    ReleaseCursor();
    glfwMakeContextCurrent(window_);
    pending_pick_.reset();
    models_.clear();
    ApplyPlateSizeMm(PlateDimensions::DefaultSizeMm());
    active_model_id_ = 0;
    RefreshSceneBounds();
    ResetProjectDisplay();
    ClearHistory();
    AdvanceProjectRevision();
    ++model_state_revision_;
}

void SceneViewport::ReleaseGraphics()
{
    if (graphics_released_)
        return;
    graphics_released_ = true;
    EndTransformEditing();
    if (!window_ || !glad_ready_)
        return;
    glfwMakeContextCurrent(window_);

    if (imgui_opengl_ready_)
        ImGui_ImplOpenGL3_Shutdown();
    if (imgui_glfw_ready_)
        ImGui_ImplGlfw_Shutdown();
    if (imgui_context_ready_)
        ImGui::DestroyContext();
    axis_label_font_ = nullptr;

    models_.clear();
    ClearHistory();
    active_model_id_ = 0;
    ++model_state_revision_;
    pending_pick_.reset();
    if (base_plate_)
        base_plate_->Release();
    base_plate_.reset();
    if (bounds_vbo_ != 0)
        glDeleteBuffers(1, &bounds_vbo_);
    if (bounds_vao_ != 0)
        glDeleteVertexArrays(1, &bounds_vao_);
    bounds_vbo_ = 0;
    bounds_vao_ = 0;
    if (slice_vbo_ != 0)
        glDeleteBuffers(1, &slice_vbo_);
    if (slice_vao_ != 0)
        glDeleteVertexArrays(1, &slice_vao_);
    slice_vbo_ = 0;
    slice_vao_ = 0;
    slice_contour_vertex_count_ = 0;
    slice_hatch_vertex_count_ = 0;
    slice_arrow_vertex_count_ = 0;
    slice_jump_vertex_count_ = 0;
    slice_jump_count_ = 0;
    slice_jump_path_length_mm_ = 0.0;
    slice_layers_.clear();
    slice_current_layer_ = 0;
    slice_visible_ = false;
    slice_contours_visible_ = true;
    slice_hatches_visible_ = true;
    slice_direction_visible_ = false;
    slice_jumps_visible_ = false;
    models_hidden_ = false;
    slice_total_contour_path_length_mm_ = 0.0;
    slice_total_hatch_path_length_mm_ = 0.0;
    slice_total_path_length_mm_ = 0.0;
    slice_applied_settings_ = {};
    if (stl_shader_)
        glDeleteProgram(stl_shader_->ID);
    if (plate_shader_)
        glDeleteProgram(plate_shader_->ID);
    stl_shader_.reset();
    plate_shader_.reset();
}
