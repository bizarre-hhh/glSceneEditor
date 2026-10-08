#ifndef VIEW_CUBE_H
#define VIEW_CUBE_H

#include <array>
#include <optional>
#include <glm/vec2.hpp>

#include "camera_controller.h"

// A screen-space cube projected with the scene camera's actual rotation.
// It has no graphics resources and remains independent of camera pan/zoom.
class ViewCube
{
public:
    using Face = CameraController::StandardView;
    struct Layout
    {
        glm::vec2 minimum{0.0f};
        glm::vec2 size{0.0f};
        glm::vec2 center{0.0f};
        glm::vec2 home_minimum{0.0f};
        glm::vec2 home_size{0.0f};
        float scale = 0.0f;
        float half_edge = 0.0f;
    };
    struct ProjectedFace
    {
        Face face = Face::Front;
        std::array<glm::vec2, 4> corners{};
        glm::vec2 center{0.0f};
        float facing = 0.0f;
        bool visible = false;
    };
    using Faces = std::array<ProjectedFace, 6>;

    static Layout GetLayout(const glm::vec2& display_size, float dpi_scale);
    static bool Contains(const glm::vec2& point, const Layout& layout);
    static Faces Project(const Camera& camera, const Layout& layout);
    static std::optional<Face> HitTest(const Faces& faces, const glm::vec2& point);
    static void Draw(CameraController& controller, float dpi_scale);
};

#endif
