#ifndef BASE_PLATE_H
#define BASE_PLATE_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

#include "camera.h"
#include "shader.h"

// Scene scale: one OpenGL world unit represents 10 mm.
class BasePlate
{
public:
    static constexpr int kSizeMm = 100;
    static constexpr int kGridStepMm = 10;
    static constexpr float kMmPerWorldUnit = 10.0f;
    static constexpr float kSizeWorld = kSizeMm / kMmPerWorldUnit;
    static constexpr float kHalfSize = kSizeWorld * 0.5f;
    static constexpr float kAxisLengthWorld = 1.2f * kSizeWorld; // 120 mm
    static constexpr float kZAxisHeight = kAxisLengthWorld;
    static constexpr float kHorizontalAxisZ = 0.065f;

    BasePlate()
    {
        std::vector<float> faces;
        const glm::vec3 top_color(0.18f, 0.19f, 0.21f);
        const float size = kSizeWorld;

        // The plate lies in the XY plane, with its origin at the (0, 0) corner.
        AddTriangle(faces, {0.0f, 0.0f, 0.0f}, {size, 0.0f, 0.0f},
                    {size, size, 0.0f}, top_color);
        AddTriangle(faces, {0.0f, 0.0f, 0.0f}, {size, size, 0.0f},
                    {0.0f, size, 0.0f}, top_color);
        face_vertex_count_ = static_cast<GLsizei>(faces.size() / 6);
        Upload(faces, face_vao_, face_vbo_);

        std::vector<float> lines;
        const glm::vec3 grid_color(0.29f, 0.31f, 0.34f);
        const glm::vec3 edge_color(0.54f, 0.57f, 0.61f);
        const float line_z = 0.003f;
        for (int mm = kGridStepMm; mm < kSizeMm; mm += kGridStepMm)
        {
            const float position = mm / kMmPerWorldUnit;
            AddLine(lines, {0.0f, position, line_z},
                    {size, position, line_z}, grid_color);
            AddLine(lines, {position, 0.0f, line_z},
                    {position, size, line_z}, grid_color);
        }

        // Short half-step marks along two edges make the 5 mm scale readable.
        for (int mm = 5; mm < kSizeMm; mm += 5)
        {
            if (mm % kGridStepMm == 0)
                continue;
            const float position = mm / kMmPerWorldUnit;
            AddLine(lines, {position, size, line_z},
                    {position, size - 0.18f, line_z}, edge_color);
            AddLine(lines, {size, position, line_z},
                    {size - 0.18f, position, line_z}, edge_color);
        }
        AddLine(lines, {0.0f, 0.0f, line_z}, {size, 0.0f, line_z}, edge_color);
        AddLine(lines, {size, 0.0f, line_z}, {size, size, line_z}, edge_color);
        AddLine(lines, {size, size, line_z}, {0.0f, size, line_z}, edge_color);
        AddLine(lines, {0.0f, size, line_z}, {0.0f, 0.0f, line_z}, edge_color);
        line_vertex_count_ = static_cast<GLsizei>(lines.size() / 6);
        Upload(lines, line_vao_, line_vbo_);

        // The horizontal shafts skim the plate; their arrowheads start beyond
        // its edges. The Z axis rises from the same corner.
        std::vector<float> axes;
        AddAxis(axes, {0.0f, 0.0f, kHorizontalAxisZ},
                {kAxisLengthWorld, 0.0f, kHorizontalAxisZ}, kXColor);
        AddAxis(axes, {0.0f, 0.0f, kHorizontalAxisZ},
                {0.0f, kAxisLengthWorld, kHorizontalAxisZ}, kYColor);
        AddAxis(axes, {0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, kZAxisHeight}, kZColor);
        axis_vertex_count_ = static_cast<GLsizei>(axes.size() / 6);
        Upload(axes, axis_vao_, axis_vbo_);
    }

    void Draw(Shader& shader, const glm::mat4& view,
              const glm::mat4& projection, const Camera& camera) const
    {
        const bool viewed_from_below = camera.Position().z <= 0.0f;
        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        shader.setFloat("alpha", 1.0f);
        shader.setFloat("zOffset", 0.0f);

        glBindVertexArray(axis_vao_);
        const GLsizei vertices_per_axis = axis_vertex_count_ / 3;
        const glm::mat3 axis_directions(1.0f);
        for (int axis = 0; axis < 3; ++axis)
            if (camera.IsAxisVisible(axis_directions[axis]))
                glDrawArrays(GL_TRIANGLES, axis * vertices_per_axis, vertices_per_axis);

        shader.setFloat("alpha", viewed_from_below ? 0.5f : 1.0f);

        if (viewed_from_below)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }

        glBindVertexArray(face_vao_);
        glDrawArrays(GL_TRIANGLES, 0, face_vertex_count_);

        // Place the scale just below the plane when viewed from underneath.
        shader.setFloat("zOffset", viewed_from_below ? -0.006f : 0.0f);
        glBindVertexArray(line_vao_);
        glDrawArrays(GL_LINES, 0, line_vertex_count_);
        glBindVertexArray(0);

        if (viewed_from_below)
        {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    // Call while the OpenGL context is still current.
    void Release()
    {
        glDeleteBuffers(1, &face_vbo_);
        glDeleteBuffers(1, &line_vbo_);
        glDeleteBuffers(1, &axis_vbo_);
        glDeleteVertexArrays(1, &face_vao_);
        glDeleteVertexArrays(1, &line_vao_);
        glDeleteVertexArrays(1, &axis_vao_);
    }

    BasePlate(const BasePlate&) = delete;
    BasePlate& operator=(const BasePlate&) = delete;

private:
    static constexpr float kAxisShaftRadius = 0.055f;
    static constexpr float kAxisConeRadius = 0.16f;
    static constexpr float kAxisConeLength = 0.48f;
    static constexpr int kAxisSides = 24;
    inline static const glm::vec3 kXColor{0.96f, 0.20f, 0.22f};
    inline static const glm::vec3 kYColor{0.18f, 0.80f, 0.30f};
    inline static const glm::vec3 kZColor{0.25f, 0.51f, 0.98f};

    static glm::vec3 AxisRadial(const glm::vec3& direction, float angle)
    {
        const glm::vec3 first = direction.z > 0.5f
                                    ? glm::vec3(1.0f, 0.0f, 0.0f)
                                    : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 second = glm::cross(direction, first);
        return first * std::cos(angle) + second * std::sin(angle);
    }

    static glm::vec3 LitAxisColor(const glm::vec3& color,
                                  const glm::vec3& normal)
    {
        const glm::vec3 light = glm::normalize(glm::vec3(-0.4f, -0.5f, 1.0f));
        const float brightness = glm::clamp(
            0.72f + 0.28f * glm::dot(normal, light), 0.54f, 1.0f);
        return color * brightness;
    }

    static void AddAxis(std::vector<float>& data, const glm::vec3& start,
                        const glm::vec3& tip, const glm::vec3& color)
    {
        constexpr float kTwoPi = 6.28318530718f;
        const glm::vec3 direction = glm::normalize(tip - start);
        const glm::vec3 cone_base = tip - direction * kAxisConeLength;

        for (int side = 0; side < kAxisSides; ++side)
        {
            const float angle0 = kTwoPi * side / kAxisSides;
            const float angle1 = kTwoPi * (side + 1) / kAxisSides;
            const glm::vec3 radial0 = AxisRadial(direction, angle0);
            const glm::vec3 radial1 = AxisRadial(direction, angle1);
            const glm::vec3 normal = glm::normalize(radial0 + radial1);
            const glm::vec3 shaft_color = LitAxisColor(color, normal);
            const glm::vec3 p0 = start + radial0 * kAxisShaftRadius;
            const glm::vec3 p1 = start + radial1 * kAxisShaftRadius;
            const glm::vec3 p2 = cone_base + radial1 * kAxisShaftRadius;
            const glm::vec3 p3 = cone_base + radial0 * kAxisShaftRadius;
            AddTriangle(data, p0, p1, p2, shaft_color);
            AddTriangle(data, p0, p2, p3, shaft_color);

            const glm::vec3 cone0 = cone_base + radial0 * kAxisConeRadius;
            const glm::vec3 cone1 = cone_base + radial1 * kAxisConeRadius;
            const glm::vec3 cone_normal = glm::normalize(
                normal * kAxisConeLength + direction * kAxisConeRadius);
            AddTriangle(data, cone0, cone1, tip,
                        LitAxisColor(color, cone_normal));

            // Darker end caps separate the arrowhead from the thin shaft.
            AddTriangle(data, cone_base, cone1, cone0, color * 0.69f);
            AddTriangle(data, start, p0, p1, color * 0.69f);
        }
    }

    static void AddVertex(std::vector<float>& data, const glm::vec3& position,
                          const glm::vec3& color)
    {
        data.insert(data.end(), {position.x, position.y, position.z,
                                 color.r, color.g, color.b});
    }

    static void AddTriangle(std::vector<float>& data, const glm::vec3& a,
                            const glm::vec3& b, const glm::vec3& c,
                            const glm::vec3& color)
    {
        AddVertex(data, a, color);
        AddVertex(data, b, color);
        AddVertex(data, c, color);
    }

    static void AddLine(std::vector<float>& data, const glm::vec3& a,
                        const glm::vec3& b, const glm::vec3& color)
    {
        AddVertex(data, a, color);
        AddVertex(data, b, color);
    }

    static void Upload(const std::vector<float>& data, GLuint& vao, GLuint& vbo)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float),
                     data.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    GLuint face_vao_ = 0;
    GLuint face_vbo_ = 0;
    GLuint line_vao_ = 0;
    GLuint line_vbo_ = 0;
    GLuint axis_vao_ = 0;
    GLuint axis_vbo_ = 0;
    GLsizei face_vertex_count_ = 0;
    GLsizei line_vertex_count_ = 0;
    GLsizei axis_vertex_count_ = 0;
};

#endif
