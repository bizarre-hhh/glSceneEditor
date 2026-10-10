#include "scene_viewport.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
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
        self->camera_controller_.OnScroll(static_cast<float>(y_offset));
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
    ImGui::Text("Base plate: 100 x 100 mm, grid: 10 mm");
    ImGui::TextUnformatted("Axis length: 120 mm (20% beyond plate)");
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

bool SceneViewport::RenderFrame()
{
    glfwMakeContextCurrent(window_);
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
    camera_controller_.Update(window_, io.WantCaptureMouse || over_cube || model_drag_.has_value() || ImGuizmo::IsUsingAny(),
                              io.WantCaptureKeyboard || model_drag_.has_value() || ImGuizmo::IsUsingAny());
    if (show_demo_window_)
        ImGui::ShowDemoWindow(&show_demo_window_);
    if (show_settings_)
        DrawSettings();
    ViewCube::Draw(camera_controller_, dpi_scale);

    const Camera& camera = camera_controller_.GetCamera();
    const float aspect = static_cast<float>(display_width) / display_height;
    const glm::mat4 projection = ProjectionMatrix(aspect);
    const glm::mat4 view = camera.GetViewMatrix();
    const bool over_gizmo = DrawModelGizmo(view, projection, over_cube);
    if (pending_pick_)
    {
        const glm::vec2 position = *pending_pick_;
        pending_pick_.reset();
        if (!IsTransformEditing() && !io.WantCaptureMouse && !over_gizmo && !ViewCube::Contains(position, cube_layout))
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
        const auto* dragged = ModelInfo(model_drag_->id);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !dragged || !dragged->selected || !dragged->visible ||
            ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            EndModelTransform();
        else
        {
            UpdateModelDrag(glm::vec2(io.MousePos.x, io.MousePos.y));
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }
    DrawAxisLabels(axis_label_font_, camera, projection * view, *base_plate_);
    ImGui::Render();

    glViewport(0, 0, display_width, display_height);
    glClearColor(clear_color_[0], clear_color_[1],
                 clear_color_[2], clear_color_[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const bool has_bounds = std::any_of(models_.begin(), models_.end(), [](const ModelEntry& entry)
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
    DrawModelBounds(view, projection);

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
    loaded->SetPlacementCenterMm(plate_size_mm_ * 0.5f);
    size_mm = loaded->SizeMm();
    StlModelInfo info{path, size_mm, loaded->TriangleCount()};
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
        .arg(QFileInfo(QString::fromUtf8(path.c_str())).fileName());
    RecordOperation(std::move(operation));
    ++model_state_revision_;
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
        .arg(QFileInfo(QString::fromUtf8(entry->info.path.c_str())).fileName());
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
    ModelEntry entry{std::move(model), saved.info, saved.selection_order, saved.stl_data};
    RefreshModelTransformInfo(entry);
    const auto index = std::min(saved.index, models_.size());
    models_.insert(models_.begin() + index, std::move(entry));
    ChooseActiveModel();
    RefreshSceneBounds();
    ++model_state_revision_;
    return true;
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

bool SceneViewport::BeginModelTransform(ModelId id, TransformMode mode)
{
    EndModelTransform();
    const auto* entry = FindModel(id);
    if (!entry) return false;
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
    const QString name = QFileInfo(QString::fromUtf8(entry->info.path.c_str())).fileName();
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

void SceneViewport::ApplyPlateSizeMm(const glm::vec2& size_mm)
{
    glfwMakeContextCurrent(window_);
    base_plate_->SetSize(size_mm);
    plate_size_mm_ = size_mm;
    camera_controller_.SetPlateSizeMm(size_mm);
    RefreshSceneBounds();
}

bool SceneViewport::SetPlateSizeMm(const glm::vec2& size_mm, QString& error)
{
    error.clear();
    if (!PlateDimensions::IsValid(size_mm))
    {
        error = QStringLiteral("平台长度和宽度必须在 1 至 10000 mm 之间。");
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
    operation.description = QStringLiteral("修改平台尺寸：%1 × %2 mm → %3 × %4 mm")
        .arg(plate_size_mm_.x, 0, 'f', 2).arg(plate_size_mm_.y, 0, 'f', 2)
        .arg(size_mm.x, 0, 'f', 2).arg(size_mm.y, 0, 'f', 2);
    ApplyPlateSizeMm(size_mm);
    ResetView();
    AdvanceProjectRevision();
    operation.after_revision = project_revision_;
    RecordOperation(std::move(operation));
    return true;
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
            info.local_translation_mm, info.local_scale, info.local_rotation_degrees, info.placement_center_mm});
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
    ApplyPlateSizeMm(glm::vec2(PlateDimensions::kDefaultMm));
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
    if (stl_shader_)
        glDeleteProgram(stl_shader_->ID);
    if (plate_shader_)
        glDeleteProgram(plate_shader_->ID);
    stl_shader_.reset();
    plate_shader_.reset();
}
