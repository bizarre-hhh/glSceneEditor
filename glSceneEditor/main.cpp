// Qt MainWindow hosting the existing GLFW + OpenGL + Dear ImGui scene.

#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <windows.h>
#include <imm.h>

// GLAD must be included before GLFW so it provides the OpenGL declarations.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "camera.h"
#include "model.h"
#include "shader.h"

namespace
{
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 800;

Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float last_mouse_x = kWindowWidth * 0.5f;
float last_mouse_y = kWindowHeight * 0.5f;
float delta_time = 0.0f;
float last_frame = 0.0f;
bool first_mouse = true;
bool camera_control = false;
HIMC previous_ime_context = nullptr;

void GlfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

std::filesystem::path GetExecutableDirectory()
{
    wchar_t executable_path[MAX_PATH]{};
    const DWORD path_length = GetModuleFileNameW(
        nullptr,
        executable_path,
        ARRAYSIZE(executable_path));
    if (path_length == 0)
        return std::filesystem::current_path();
    return std::filesystem::path(
        std::wstring(executable_path, path_length)).parent_path();
}

bool IsPhysicalKeyDown(int virtual_key)
{
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

void ClipCursorToClient(HWND window)
{
    RECT client_rect{};
    if (!GetClientRect(window, &client_rect))
        return;

    POINT top_left{ client_rect.left, client_rect.top };
    POINT bottom_right{ client_rect.right, client_rect.bottom };
    if (!ClientToScreen(window, &top_left) ||
        !ClientToScreen(window, &bottom_right))
    {
        return;
    }

    const RECT screen_rect{
        top_left.x,
        top_left.y,
        bottom_right.x,
        bottom_right.y
    };
    ClipCursor(&screen_rect);
}

void ProcessInput(GLFWwindow* window)
{
    if (IsPhysicalKeyDown(VK_ESCAPE))
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    const HWND native_window = glfwGetWin32Window(window);
    const HWND host_window = GetAncestor(native_window, GA_ROOT);

    POINT cursor_position{};
    RECT scene_rect{};
    const bool has_cursor_position = GetCursorPos(&cursor_position) != FALSE;
    const HWND window_under_cursor = has_cursor_position
        ? WindowFromPoint(cursor_position)
        : nullptr;
    const HWND cursor_root = window_under_cursor != nullptr
        ? GetAncestor(window_under_cursor, GA_ROOT)
        : nullptr;
    const bool cursor_over_scene =
        cursor_root == host_window &&
        GetWindowRect(native_window, &scene_rect) &&
        PtInRect(&scene_rect, cursor_position);
    const bool right_button_down =
        cursor_over_scene && IsPhysicalKeyDown(VK_RBUTTON);

    if (right_button_down && !camera_control)
    {
        camera_control = true;
        first_mouse = true;
        ClipCursorToClient(native_window);
    }
    else if (!right_button_down && camera_control)
    {
        camera_control = false;
        ClipCursor(nullptr);
    }

    // Poll cursor position directly while RMB is held. A GLFW disabled-cursor
    // mode cannot reliably capture relative motion from a Qt embedded HWND.
    if (camera_control)
    {
        POINT current_position{};
        if (GetCursorPos(&current_position))
        {
            const float mouse_x = static_cast<float>(current_position.x);
            const float mouse_y = static_cast<float>(current_position.y);
            if (first_mouse)
            {
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
                first_mouse = false;
            }
            else
            {
                camera.ProcessMouseMovement(
                    mouse_x - last_mouse_x,
                    last_mouse_y - mouse_y);
                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
            }
        }
    }

    // Use Win32 asynchronous keyboard state so the Chinese IME cannot turn
    // these movement keys into text composition events.
    if (IsPhysicalKeyDown('W'))
        camera.ProcessKeyboard(FORWARD, delta_time);
    if (IsPhysicalKeyDown('S'))
        camera.ProcessKeyboard(BACKWARD, delta_time);
    if (IsPhysicalKeyDown('A'))
        camera.ProcessKeyboard(LEFT, delta_time);
    if (IsPhysicalKeyDown('D'))
        camera.ProcessKeyboard(RIGHT, delta_time);
}

void FramebufferSizeCallback(GLFWwindow*, int width, int height)
{
    glViewport(0, 0, width, height);
}



void ScrollCallback(GLFWwindow*, double, double y_offset)
{
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
        return;
    camera.ProcessMouseScroll(static_cast<float>(y_offset));
}
} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
        return 1;

    // LearnOpenGL's model-loading shaders use GLSL 3.30 core.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    const float content_scale =
        ImGui_ImplGlfw_GetContentScaleForMonitor(primary_monitor);
    GLFWwindow* window = glfwCreateWindow(
        static_cast<int>(kWindowWidth * content_scale),
        static_cast<int>(kWindowHeight * content_scale),
        "glSceneEditor - Backpack Model",
        nullptr,
        nullptr);
    if (window == nullptr)
    {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetScrollCallback(window, ScrollCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        std::cerr << "Failed to initialize GLAD.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Detach the Windows IME from this GLFW window. This prevents Chinese
    // pinyin composition from consuming the camera movement keys.
    HWND native_window = glfwGetWin32Window(window);
    previous_ime_context = ImmAssociateContext(native_window, nullptr);

    glEnable(GL_DEPTH_TEST);
    stbi_set_flip_vertically_on_load(true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
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

    // These calls come after our callbacks so the GLFW backend can chain them.
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    const std::filesystem::path model_resource_directory =
        GetExecutableDirectory() / "resources" / "asset" / "backpack";
    const std::filesystem::path shader_resource_directory =
        GetExecutableDirectory() / "resources" / "shaders";
    const std::string vertex_shader_path =
        (shader_resource_directory / "model.vs").string();
    const std::string fragment_shader_path =
        (shader_resource_directory / "model.fs").string();
    // Use forward slashes here because the copied LearnOpenGL Model helper
    // extracts the model directory with '/'. Windows accepts this path form.
    const std::string model_path =
        (model_resource_directory / "backpack.obj").generic_string();

    Shader model_shader(vertex_shader_path.c_str(), fragment_shader_path.c_str());
    Model backpack(model_path);

    bool show_demo_window = false;
    bool show_test_window = true;
    bool wireframe = false;
    ImVec4 clear_color(0.05f, 0.05f, 0.07f, 1.0f);
    float model_scale = 1.0f;
    glm::vec3 model_position(0.0f, 0.0f, 0.0f);

    const auto renderFrame = [&]()
    {
        glfwMakeContextCurrent(window);
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
        {
            app.quit();
            return;
        }

        const float current_frame = static_cast<float>(glfwGetTime());
        delta_time = current_frame - last_frame;
        last_frame = current_frame;
        ProcessInput(window);
        if (glfwWindowShouldClose(window))
        {
            app.quit();
            return;
        }

        int window_width = 0;
        int window_height = 0;
        int display_width = 0;
        int display_height = 0;
        glfwGetWindowSize(window, &window_width, &window_height);
        glfwGetFramebufferSize(window, &display_width, &display_height);
        if (window_width <= 0 || window_height <= 0 ||
            display_width <= 0 || display_height <= 0)
        {
            return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        if (show_test_window)
        {
            ImGui::SetNextWindowSize(ImVec2(460.0f, 250.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(420.0f, 220.0f),
                ImVec2(FLT_MAX, FLT_MAX));
            ImGui::Begin("Backpack Model", &show_test_window);
            ImGui::Text("GLFW + OpenGL + Dear ImGui + Assimp");
            ImGui::Text("Use WASD to move; hold RMB to rotate the camera.");
            ImGui::Separator();
            ImGui::Checkbox("Show ImGui demo", &show_demo_window);
            ImGui::Checkbox("Wireframe", &wireframe);
            ImGui::SliderFloat("Model scale", &model_scale, 0.1f, 3.0f);
            ImGui::TextUnformatted("Clear color (RGB)");
            ImGui::SliderFloat("Red", &clear_color.x, 0.0f, 1.0f, "R: %.2f");
            ImGui::SliderFloat("Green", &clear_color.y, 0.0f, 1.0f, "G: %.2f");
            ImGui::SliderFloat("Blue", &clear_color.z, 0.0f, 1.0f, "B: %.2f");
            ImGui::SameLine();
            ImGui::ColorButton(
                "Color preview",
                clear_color,
                ImGuiColorEditFlags_NoTooltip,
                ImVec2(52.0f, 52.0f));

            ImGui::InputFloat("FOV", &camera.Zoom, 1.0f, 5.0f, "%.1f");
            camera.SetZoom(camera.Zoom);
            ImGui::InputFloat("Yaw", &camera.Yaw, 1.0f, 5.0f, "%.1f");
            ImGui::InputFloat("Pitch", &camera.Pitch, 1.0f, 5.0f, "%.1f");
            camera.SetRotation(camera.Yaw, camera.Pitch);
            ImGui::InputFloat3(
                "Camera position",
                glm::value_ptr(camera.Position),
                "%.2f");
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::End();
        }
        else
        {
            ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::Begin(
                "Backpack Model Launcher",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("The model panel is hidden.");
            if (ImGui::Button("Open Backpack Model"))
                show_test_window = true;
            ImGui::End();
        }

        ImGui::Render();

        glViewport(0, 0, display_width, display_height);

        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        model_shader.use();
        const float aspect_ratio = display_height > 0
            ? static_cast<float>(display_width) / static_cast<float>(display_height)
            : 1.0f;
        const glm::mat4 projection = glm::perspective(
            glm::radians(camera.Zoom), aspect_ratio, 0.1f, 100.0f);
        const glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 model_matrix(1.0f);
        model_matrix = glm::translate(model_matrix, model_position);
        model_matrix = glm::scale(model_matrix, glm::vec3(model_scale));
        model_shader.setMat4("projection", projection);
        model_shader.setMat4("view", view);
        model_shader.setMat4("model", model_matrix);

        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        backpack.Draw(model_shader);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    };

    int exit_code = 0;
    {
        QMainWindow mainWindow;
        mainWindow.setWindowTitle(QStringLiteral("glSceneEditor"));
        mainWindow.resize(kWindowWidth, kWindowHeight);

        QWindow* glfwWindow = QWindow::fromWinId(
            reinterpret_cast<WId>(native_window));
        if (glfwWindow == nullptr)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            ImmAssociateContext(native_window, previous_ime_context);
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        QWidget* sceneArea = QWidget::createWindowContainer(glfwWindow, &mainWindow);
        sceneArea->setFocusPolicy(Qt::StrongFocus);
        mainWindow.setCentralWidget(sceneArea);

        QMenu* fileMenu = mainWindow.menuBar()->addMenu(QStringLiteral("文件(&F)"));
        QAction* quitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
        QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
        mainWindow.statusBar()->showMessage(QStringLiteral("就绪"));

        QTimer renderTimer;
        renderTimer.setTimerType(Qt::PreciseTimer);
        renderTimer.setInterval(8);
        QObject::connect(&renderTimer, &QTimer::timeout, renderFrame);
        QObject::connect(&app, &QApplication::aboutToQuit, &renderTimer, &QTimer::stop);

        mainWindow.show();
        glfwShowWindow(window);
        renderTimer.start();
        exit_code = app.exec();

        renderTimer.stop();
        ClipCursor(nullptr);
        glfwMakeContextCurrent(window);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    ImmAssociateContext(native_window, previous_ime_context);
    glfwDestroyWindow(window);
    glfwTerminate();
    return exit_code;
}
