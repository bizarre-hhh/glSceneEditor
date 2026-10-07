#include "scene_viewport.h"

#include <algorithm>
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

void DrawAxisLabels(ImFont* font, const glm::mat4& view_projection)
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
    if (self != nullptr)
        self->camera_controller_.OnScroll(static_cast<float>(y_offset));
}

void SceneViewport::DrawSettings()
{
    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(420.0f, 220.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("Scene Settings");
    ImGui::Text("GLFW + OpenGL + Dear ImGui + Assimp");
    ImGui::Text("RMB: orbit | MMB: pan | Wheel: zoom | F: fit | Home: reset");
    ImGui::Separator();
    ImGui::Checkbox("Show ImGui demo", &show_demo_window_);
    ImGui::Checkbox("Wireframe", &wireframe_);
    ImGui::Text("Base plate: 100 x 100 mm, grid: 10 mm");
    ImGui::TextUnformatted("Axis length: 120 mm (20% beyond plate)");
    ImGui::Text("Origin: plate corner | X red | Y green | Z blue (up)");
    if (current_model_)
    {
        const glm::vec3 size = current_model_->SizeMm();
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

    Camera& camera = camera_controller_.GetCamera();
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
        camera_controller_.ReleaseCursor();
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
    camera_controller_.Update(window_, ImGui::GetIO().WantCaptureMouse,
                              ImGui::GetIO().WantCaptureKeyboard);
    if (show_demo_window_)
        ImGui::ShowDemoWindow(&show_demo_window_);
    if (show_settings_)
        DrawSettings();

    const Camera& camera = camera_controller_.GetCamera();
    const float aspect = static_cast<float>(display_width) / display_height;
    const float near_plane = std::max(0.01f, camera.Distance() / 1000.0f);
    const float far_plane = std::max(100.0f, camera.Distance() * 4.0f +
                                              camera.SceneRadius() * 2.0f);
    const glm::mat4 projection = glm::perspective(
        glm::radians(camera.Zoom()), aspect, near_plane, far_plane);
    const glm::mat4 view = camera.GetViewMatrix();
    DrawAxisLabels(axis_label_font_, projection * view);
    ImGui::Render();

    glViewport(0, 0, display_width, display_height);
    glClearColor(clear_color_[0], clear_color_[1],
                 clear_color_[2], clear_color_[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (current_model_)
    {
        glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);
        current_model_->Draw(*stl_shader_, view, projection);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    base_plate_->Draw(*plate_shader_, view, projection, camera.Position().z);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
    return true;
}

void SceneViewport::ReleaseCursor()
{
    camera_controller_.ReleaseCursor();
}

bool SceneViewport::LoadStl(const std::string& path, std::string& error,
                            glm::vec3& size_mm)
{
    glfwMakeContextCurrent(window_);
    auto loaded = std::make_unique<StlModel>();
    if (!loaded->Load(path, error))
        return false;
    size_mm = loaded->SizeMm();
    current_model_ = std::move(loaded);
    glm::vec3 model_min(0.0f);
    glm::vec3 model_max(0.0f);
    current_model_->GetWorldBounds(model_min, model_max);
    const float axis_length = BasePlate::kAxisLengthWorld;
    camera_controller_.SetSceneBounds(
        glm::min(model_min, glm::vec3(0.0f, 0.0f, 0.0f)),
        glm::max(model_max, glm::vec3(axis_length)));
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (height > 0)
        camera_controller_.FrameScene(static_cast<float>(width) / height);
    return true;
}

void SceneViewport::ReleaseGraphics()
{
    if (graphics_released_)
        return;
    graphics_released_ = true;
    camera_controller_.ReleaseCursor();
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

    current_model_.reset();
    if (base_plate_)
        base_plate_->Release();
    base_plate_.reset();
    if (stl_shader_)
        glDeleteProgram(stl_shader_->ID);
    if (plate_shader_)
        glDeleteProgram(plate_shader_->ID);
    stl_shader_.reset();
    plate_shader_.reset();
}
