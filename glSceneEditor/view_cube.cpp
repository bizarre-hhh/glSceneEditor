#include "view_cube.h"

#include <algorithm>
#include <cmath>
#include <glm/mat3x3.hpp>
#include "imgui.h"

namespace
{
struct FaceDefinition
{
    ViewCube::Face face;
    glm::vec3 normal;
    glm::vec3 horizontal;
    glm::vec3 vertical;
    const char* label;
    ImVec4 color;
};

const std::array<FaceDefinition, 6> kFaces = {{
    {ViewCube::Face::Front,  {0, -1, 0}, {1, 0, 0},  {0, 0, 1}, "Front",  {0.78f, 0.87f, 0.80f, 1}},
    {ViewCube::Face::Back,   {0, 1, 0},  {-1, 0, 0}, {0, 0, 1}, "Back",   {0.78f, 0.87f, 0.80f, 1}},
    {ViewCube::Face::Left,   {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, "Left",   {0.91f, 0.79f, 0.77f, 1}},
    {ViewCube::Face::Right,  {1, 0, 0},  {0, 1, 0},  {0, 0, 1}, "Right",  {0.91f, 0.79f, 0.77f, 1}},
    {ViewCube::Face::Top,    {0, 0, 1},  {1, 0, 0},  {0, 1, 0}, "Top",    {0.78f, 0.85f, 0.95f, 1}},
    {ViewCube::Face::Bottom, {0, 0, -1}, {1, 0, 0},  {0, -1, 0}, "Bottom", {0.78f, 0.85f, 0.95f, 1}},
}};

ImVec2 ToImVec(const glm::vec2& point)
{
    return ImVec2(point.x, point.y);
}

const FaceDefinition& Definition(ViewCube::Face face)
{
    const auto found = std::find_if(kFaces.begin(), kFaces.end(),
                                  [face](const auto& item) { return item.face == face; });
    return *found;
}
} // namespace

ViewCube::Layout ViewCube::GetLayout(const glm::vec2& display_size, float dpi_scale)
{
    Layout layout;
    layout.scale = std::min({dpi_scale, display_size.x / 174.0f, display_size.y / 192.0f});
    if (layout.scale <= 0.0f)
        return layout;
    const float margin = 14.0f * layout.scale;
    layout.size = glm::vec2(146.0f, 164.0f) * layout.scale;
    layout.minimum = glm::vec2(margin, display_size.y - margin - layout.size.y);
    layout.center = layout.minimum + glm::vec2(73.0f, 76.0f) * layout.scale;
    layout.home_minimum = layout.minimum + glm::vec2(41.0f, 132.0f) * layout.scale;
    layout.home_size = glm::vec2(64.0f, 24.0f) * layout.scale;
    layout.half_edge = 30.0f * layout.scale;
    return layout;
}

bool ViewCube::Contains(const glm::vec2& point, const Layout& layout)
{
    return layout.scale > 0.0f && point.x >= layout.minimum.x && point.y >= layout.minimum.y &&
           point.x < layout.minimum.x + layout.size.x && point.y < layout.minimum.y + layout.size.y;
}

ViewCube::Faces ViewCube::Project(const Camera& camera, const Layout& layout)
{
    Faces result{};
    const glm::mat3 rotation(camera.GetViewMatrix());
    for (std::size_t i = 0; i < kFaces.size(); ++i)
    {
        const auto& definition = kFaces[i];
        auto& face = result[i];
        face.face = definition.face;
        face.facing = (rotation * definition.normal).z;
        face.visible = layout.scale > 0.0f && face.facing > 0.015f;
        // Clockwise screen-space winding for ImGui's convex polygon fill.
        const glm::vec2 signs[] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
        for (int corner = 0; corner < 4; ++corner)
        {
            const glm::vec3 point = rotation * (definition.normal +
                definition.horizontal * signs[corner].x + definition.vertical * signs[corner].y);
            face.corners[corner] = layout.center + glm::vec2(point.x, -point.y) * layout.half_edge;
        }
        const glm::vec3 center = rotation * definition.normal;
        face.center = layout.center + glm::vec2(center.x, -center.y) * layout.half_edge;
    }
    return result;
}

std::optional<ViewCube::Face> ViewCube::HitTest(const Faces& faces, const glm::vec2& point)
{
    for (const auto& face : faces)
    {
        if (!face.visible)
            continue;
        bool inside = true;
        for (int corner = 0; corner < 4; ++corner)
        {
            const glm::vec2 edge = face.corners[(corner + 1) % 4] - face.corners[corner];
            const glm::vec2 offset = point - face.corners[corner];
            if (edge.x * offset.y - edge.y * offset.x < -0.01f)
            {
                inside = false;
                break;
            }
        }
        if (inside)
            return face.face;
    }
    return std::nullopt;
}

void ViewCube::Draw(CameraController& controller, float dpi_scale)
{
    const ImGuiIO& io = ImGui::GetIO();
    const Layout layout = GetLayout(glm::vec2(io.DisplaySize.x, io.DisplaySize.y), dpi_scale);
    if (layout.scale <= 0.0f)
        return;
    ImGui::SetNextWindowPos(ToImVec(layout.minimum), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ToImVec(layout.size), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * layout.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.97f, 0.98f, 0.99f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.70f, 0.75f, 0.82f, 0.85f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;
    ImGui::Begin("View Cube##orientation", nullptr, flags);
    ImGui::InvisibleButton("##cube",
                          ImVec2(layout.size.x, layout.home_minimum.y - layout.minimum.y),
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        // ImGui keeps this drag active when the pointer leaves the cube. Use
        // the scene camera's orbit operation, without also running its native drag.
        controller.ReleaseCursor();
        if (!ImGui::IsItemActivated())
            controller.GetCamera().Orbit(io.MouseDelta.x, io.MouseDelta.y);
    }
    Faces faces = Project(controller.GetCamera(), layout);
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    auto hovered = ImGui::IsItemHovered() ? HitTest(faces, mouse) : std::nullopt;
    if (hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        controller.SetStandardView(*hovered);
        faces = Project(controller.GetCamera(), layout);
        hovered = HitTest(faces, mouse);
    }
    ImGui::SetCursorScreenPos(ToImVec(layout.home_minimum));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * layout.scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.84f, 0.89f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.82f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.61f, 0.74f, 0.92f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.16f, 0.20f, 0.26f, 1.0f));
    if (ImGui::Button("Home##cube", ToImVec(layout.home_size)))
    {
        controller.ResetView(io.DisplaySize.x / io.DisplaySize.y);
        faces = Project(controller.GetCamera(), layout);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float font_size = 12.0f * layout.scale;
    const ImU32 text_color = IM_COL32(42, 52, 66, 255);
    draw_list->AddText(font, font_size,
                      ToImVec(layout.minimum + glm::vec2(12, 10) * layout.scale),
                      IM_COL32(80, 92, 108, 255), "VIEW");
    for (const auto& face : faces)
    {
        if (!face.visible)
            continue;
        const auto& definition = Definition(face.face);
        const bool hot = hovered && *hovered == face.face;
        ImVec4 color = definition.color;
        const float shade = 0.76f + 0.24f * face.facing;
        color.x *= shade;
        color.y *= shade;
        color.z *= shade;
        if (face.facing > 0.999f)
            color = ImVec4(0.66f, 0.80f, 0.98f, 1.0f);
        if (hot)
            color = ImVec4(1.0f, 0.78f, 0.40f, 1.0f);
        ImVec2 corners[4];
        for (int i = 0; i < 4; ++i)
            corners[i] = ToImVec(face.corners[i]);
        draw_list->AddConvexPolyFilled(corners, 4, ImGui::ColorConvertFloat4ToU32(color));
        draw_list->AddPolyline(corners, 4, IM_COL32(62, 75, 91, 255), ImDrawFlags_Closed,
                               1.5f * layout.scale);
        if (face.facing > 0.2f)
        {
            const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, definition.label);
            draw_list->AddText(font, font_size,
                              ImVec2(face.center.x - text_size.x * 0.5f, face.center.y - text_size.y * 0.5f),
                              text_color, definition.label);
        }
    }
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
