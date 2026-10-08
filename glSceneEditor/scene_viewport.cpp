#include "scene_viewport.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
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

void DrawAxisLabels(ImFont* font, const Camera& camera, const glm::mat4& view_projection)
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
    const float axis_length = BasePlate::kAxisLengthWorld;
    const glm::vec3 origin(0.0f, 0.0f, 0.0f);
    struct AxisLabel
    {
        const char* text;
        glm::vec3 tip;
        glm::vec3 base;
        ImU32 rim_color;
    };
    const AxisLabel labels[] = {
        {"X", {axis_length, 0.0f, BasePlate::kHorizontalAxisZ},
         {0.0f, 0.0f, BasePlate::kHorizontalAxisZ},
         IM_COL32(255, 92, 86, 255)},
        {"Y", {0.0f, axis_length, BasePlate::kHorizontalAxisZ},
         {0.0f, 0.0f, BasePlate::kHorizontalAxisZ},
         IM_COL32(80, 225, 105, 255)},
        {"Z", {0.0f, 0.0f, axis_length}, origin,
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
    base_plate_ = std::make_unique<BasePlate>();

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
    if (self != nullptr && !self->model_drag_)
        self->camera_controller_.OnScroll(static_cast<float>(y_offset));
}

void SceneViewport::MouseButtonCallback(GLFWwindow* window, int button,
                                        int action, int)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
        return;
    auto* self = static_cast<SceneViewport*>(glfwGetWindowUserPointer(window));
    if (self == nullptr)
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
        return false;

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
    const ImGuiIO& io = ImGui::GetIO();
    const float dpi_scale = ImGui::GetStyle().FontScaleDpi;
    const ViewCube::Layout cube_layout = ViewCube::GetLayout(
        glm::vec2(io.DisplaySize.x, io.DisplaySize.y), dpi_scale);
    const bool over_cube = ViewCube::Contains(glm::vec2(io.MousePos.x, io.MousePos.y), cube_layout);
    camera_controller_.Update(window_, io.WantCaptureMouse || over_cube || model_drag_.has_value(),
                              io.WantCaptureKeyboard || model_drag_.has_value());
    if (show_demo_window_)
        ImGui::ShowDemoWindow(&show_demo_window_);
    if (show_settings_)
        DrawSettings();
    ViewCube::Draw(camera_controller_, dpi_scale);

    const Camera& camera = camera_controller_.GetCamera();
    const float aspect = static_cast<float>(display_width) / display_height;
    const glm::mat4 projection = ProjectionMatrix(aspect);
    const glm::mat4 view = camera.GetViewMatrix();
    if (pending_pick_)
    {
        const glm::vec2 position = *pending_pick_;
        pending_pick_.reset();
        if (!io.WantCaptureMouse && !ViewCube::Contains(position, cube_layout))
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
            model_drag_.reset();
        else
        {
            UpdateModelDrag(glm::vec2(io.MousePos.x, io.MousePos.y));
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }
    DrawAxisLabels(axis_label_font_, camera, projection * view);
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
    model_drag_.reset();
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
    glfwMakeContextCurrent(window_);
    auto loaded = std::make_unique<StlModel>();
    if (!loaded->Load(path, error))
        return false;
    size_mm = loaded->SizeMm();
    StlModelInfo info{path, size_mm, loaded->TriangleCount()};
    info.id = next_model_id_++;
    ReleaseCursor();
    const bool keep_active = IsModelSelected();
    models_.push_back({std::move(loaded), std::move(info)});
    if (!keep_active)
        active_model_id_ = models_.back().info.id;
    RefreshModelTransformInfo(models_.back());
    RefreshSceneBounds();
    ++model_state_revision_;
    return true;
}

void SceneViewport::SetModelVisible(ModelId id, bool visible)
{
    auto* entry = FindModel(id);
    if (!entry || entry->info.visible == visible)
        return;
    entry->info.visible = visible;
    if (!visible && model_drag_ && model_drag_->id == id)
        model_drag_.reset();
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
    auto* entry = FindModel(id);
    if (!entry)
        return;
    const bool changed = entry->info.selected != selected ||
                         (selected && active_model_id_ != id);
    entry->info.selected = selected;
    if (selected)
    {
        active_model_id_ = id;
        if (changed)
            entry->selection_order = model_state_revision_ + 1;
    }
    else
    {
        if (model_drag_ && model_drag_->id == id)
            model_drag_.reset();
        if (active_model_id_ == id)
            ChooseActiveModel();
    }
    if (changed)
        ++model_state_revision_;
}

void SceneViewport::ClearModelSelection()
{
    bool changed = false;
    for (auto& entry : models_)
    {
        changed |= entry.info.selected;
        entry.info.selected = false;
    }
    model_drag_.reset();
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
    drag.initial_translation_mm = glm::vec2(entry->model->Transform().translation_mm);
    const glm::vec3 direction = glm::normalize(ray->end - ray->start);
    // In edge-on standard views a horizontal plane is parallel to the ray.
    // Use a screen-facing plane there, and still apply only the XY component.
    if (std::abs(direction.z) < 1.0e-3f)
        drag.plane_normal = direction;
    camera_controller_.ReleaseCursor();
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
    const glm::vec2 translation = drag.initial_translation_mm +
        glm::vec2(point - drag.anchor) * BasePlate::kMmPerWorldUnit;
    const glm::vec2 previous(entry->model->Transform().translation_mm);
    if (!std::isfinite(translation.x) || !std::isfinite(translation.y) ||
        glm::length(translation - previous) < 1.0e-4f)
        return;
    // No plate-boundary clamp: models may be placed anywhere in the XY plane.
    entry->model->SetTranslationXY(translation);
    RefreshModelTransformInfo(*entry);
    RefreshSceneBounds();
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
    const auto found = std::find_if(models_.begin(), models_.end(),
        [id](const ModelEntry& entry) { return entry.info.id == id; });
    if (found == models_.end())
        return;
    ReleaseCursor();
    glfwMakeContextCurrent(window_);
    models_.erase(found);
    if (active_model_id_ == id)
        ChooseActiveModel();
    RefreshSceneBounds();
    ++model_state_revision_;
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

void SceneViewport::ReleaseGraphics()
{
    if (graphics_released_)
        return;
    graphics_released_ = true;
    ReleaseCursor();
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
