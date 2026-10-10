#ifndef STL_MODEL_H
#define STL_MODEL_H

#include "model_transform.h"

#include <glad/glad.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "base_plate.h"
#include "shader.h"

// STL coordinates are interpreted as millimeters. The model is centered on
// the plate in X/Y, and its lowest Z point is placed on the plate (Z = 0).
class StlModel
{
public:
    using LocalTransform = ModelTransform;

    void SetPlacementCenterMm(const glm::vec2& center_mm) { placement_center_mm_ = center_mm; }
    glm::vec2 PlacementCenterMm() const { return placement_center_mm_; }

    const LocalTransform& Transform() const { return local_transform_; }
    void SetTransform(const LocalTransform& transform) { local_transform_ = transform; }

    void SetTranslationXY(const glm::vec2& translation_mm)
    {
        if (std::isfinite(translation_mm.x) && std::isfinite(translation_mm.y))
        {
            local_transform_.translation_mm.x = translation_mm.x;
            local_transform_.translation_mm.y = translation_mm.y;
        }
    }

    // World position uses the model's bounding-box center as its reference.
    glm::vec3 WorldPositionMm() const
    {
        const glm::vec3 center = (bounds_min_ + bounds_max_) * 0.5f;
        return glm::vec3(ModelMatrix() * glm::vec4(center, 1.0f)) *
               BasePlate::kMmPerWorldUnit;
    }

    bool LoadFromMemory(const char* data, std::size_t size, std::string& error)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFileFromMemory(
            data, size,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_PreTransformVertices |
            aiProcess_ValidateDataStructure, "stl");
        if (!scene || !scene->HasMeshes())
        {
            error = importer.GetErrorString();
            if (error.empty())
                error = "No mesh found in the STL file.";
            return false;
        }

        std::vector<float> vertices;
        bounds_min_ = glm::vec3(std::numeric_limits<float>::max());
        bounds_max_ = glm::vec3(std::numeric_limits<float>::lowest());
        for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index)
        {
            const aiMesh* mesh = scene->mMeshes[mesh_index];
            for (unsigned int face_index = 0; face_index < mesh->mNumFaces; ++face_index)
            {
                const aiFace& face = mesh->mFaces[face_index];
                if (face.mNumIndices != 3)
                    continue;

                for (unsigned int corner = 0; corner < 3; ++corner)
                {
                    const unsigned int index = face.mIndices[corner];
                    const aiVector3D& position = mesh->mVertices[index];
                    const aiVector3D& normal = mesh->mNormals[index];
                    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                        !std::isfinite(position.z) || !std::isfinite(normal.x) ||
                        !std::isfinite(normal.y) || !std::isfinite(normal.z))
                    {
                        error = "The STL contains non-finite coordinates or normals.";
                        return false;
                    }
                    const glm::vec3 point(position.x, position.y, position.z);
                    bounds_min_ = glm::min(bounds_min_, point);
                    bounds_max_ = glm::max(bounds_max_, point);
                    vertices.insert(vertices.end(), {
                        position.x, position.y, position.z,
                        normal.x, normal.y, normal.z
                    });
                }
            }
        }

        if (vertices.empty())
        {
            error = "The STL file contains no triangles.";
            return false;
        }

        vertex_count_ = static_cast<GLsizei>(vertices.size() / 6);
        pick_vertices_.clear();
        pick_vertices_.reserve(static_cast<std::size_t>(vertex_count_));
        for (std::size_t i = 0; i < vertices.size(); i += 6)
            pick_vertices_.emplace_back(vertices[i], vertices[i + 1], vertices[i + 2]);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
                     vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        return true;
    }

    void Draw(Shader& shader, const glm::mat4& view,
              const glm::mat4& projection, bool selected = false) const
    {
        shader.use();
        shader.setMat4("model", ModelMatrix());
        shader.setBool("selected", selected);
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
        glBindVertexArray(0);
    }

    // Test the camera's near-to-far segment against the actual mesh. Use the
    // same placement transform as drawing, including millimeters and Z offset.
    bool IntersectsSegment(const glm::vec3& start, const glm::vec3& end,
                           glm::vec3* hit_point = nullptr) const
    {
        glm::vec3 minimum(0.0f);
        glm::vec3 maximum(0.0f);
        GetWorldBounds(minimum, maximum);
        const glm::vec3 segment = end - start;
        float enter = 0.0f;
        float leave = 1.0f;
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

        const glm::mat4 inverse_model = glm::inverse(ModelMatrix());
        const glm::vec3 local_start(inverse_model * glm::vec4(start, 1.0f));
        const glm::vec3 local_end(inverse_model * glm::vec4(end, 1.0f));
        const glm::vec3 local_segment = local_end - local_start;
        const float length = glm::length(local_segment);
        if (length <= 0.0f)
            return false;
        const glm::vec3 direction = local_segment / length;
        constexpr float tolerance = 1.0e-6f;
        float nearest_distance = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i + 2 < pick_vertices_.size(); i += 3)
        {
            const glm::vec3& vertex = pick_vertices_[i];
            const glm::vec3 edge1 = pick_vertices_[i + 1] - vertex;
            const glm::vec3 edge2 = pick_vertices_[i + 2] - vertex;
            const glm::vec3 cross = glm::cross(direction, edge2);
            const float determinant = glm::dot(edge1, cross);
            const float scale = glm::length(edge1) * glm::length(edge2);
            if (std::abs(determinant) <= 1.0e-7f * scale)
                continue;

            const float inverse_determinant = 1.0f / determinant;
            const glm::vec3 offset = local_start - vertex;
            const float u = glm::dot(offset, cross) * inverse_determinant;
            if (u < -tolerance || u > 1.0f + tolerance)
                continue;
            const glm::vec3 perpendicular = glm::cross(offset, edge1);
            const float v = glm::dot(direction, perpendicular) * inverse_determinant;
            if (v < -tolerance || u + v > 1.0f + tolerance)
                continue;
            const float distance = glm::dot(edge2, perpendicular) * inverse_determinant;
            if (distance >= 0.0f && distance <= length * (1.0f + tolerance))
            {
                if (hit_point == nullptr)
                    return true;
                nearest_distance = std::min(nearest_distance, distance);
            }
        }
        if (nearest_distance == std::numeric_limits<float>::max())
            return false;
        *hit_point = glm::vec3(ModelMatrix() *
            glm::vec4(local_start + direction * nearest_distance, 1.0f));
        return true;
    }

    glm::vec3 SizeMm() const { return bounds_max_ - bounds_min_; }
    std::size_t TriangleCount() const
    {
        return static_cast<std::size_t>(vertex_count_) / 3;
    }

    void GetWorldBounds(glm::vec3& minimum, glm::vec3& maximum) const
    {
        const glm::mat4 matrix = ModelMatrix();
        minimum = glm::vec3(std::numeric_limits<float>::max());
        maximum = glm::vec3(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 point(corner & 1 ? bounds_max_.x : bounds_min_.x,
                                  corner & 2 ? bounds_max_.y : bounds_min_.y,
                                  corner & 4 ? bounds_max_.z : bounds_min_.z);
            const glm::vec3 world(matrix * glm::vec4(point, 1.0f));
            minimum = glm::min(minimum, world);
            maximum = glm::max(maximum, world);
        }
    }

    ~StlModel()
    {
        if (vbo_ != 0)
            glDeleteBuffers(1, &vbo_);
        if (vao_ != 0)
            glDeleteVertexArrays(1, &vao_);
    }

    StlModel() = default;
    StlModel(const StlModel&) = delete;
    StlModel& operator=(const StlModel&) = delete;

private:
    glm::mat4 ModelMatrix() const
    {
        const float world_units_per_mm = 1.0f / BasePlate::kMmPerWorldUnit;
        const glm::vec3 center = (bounds_min_ + bounds_max_) * 0.5f;
        const glm::vec3 offset(
            placement_center_mm_.x * world_units_per_mm - center.x * world_units_per_mm,
            placement_center_mm_.y * world_units_per_mm - center.y * world_units_per_mm,
            -bounds_min_.z * world_units_per_mm);
        const glm::mat4 placement = glm::scale(
            glm::translate(glm::mat4(1.0f), offset), glm::vec3(world_units_per_mm));
        const glm::vec3 pivot(placement_center_mm_.x * world_units_per_mm,
                              placement_center_mm_.y * world_units_per_mm,
                              SizeMm().z * world_units_per_mm * 0.5f);
        // Local transforms are relative to the imported placement, about the
        // model center. Physical scale excludes the mm-to-world conversion.
        glm::mat4 local = glm::translate(glm::mat4(1.0f), pivot);
        local = glm::rotate(local, glm::radians(local_transform_.rotation_degrees.x), {1, 0, 0});
        local = glm::rotate(local, glm::radians(local_transform_.rotation_degrees.y), {0, 1, 0});
        local = glm::rotate(local, glm::radians(local_transform_.rotation_degrees.z), {0, 0, 1});
        local = glm::translate(local, local_transform_.translation_mm * world_units_per_mm);
        local = glm::scale(local, local_transform_.scale);
        local = glm::translate(local, -pivot);
        return local * placement;
    }

    glm::vec2 placement_center_mm_{PlateDimensions::kDefaultMm * 0.5f};
    LocalTransform local_transform_;
    std::vector<glm::vec3> pick_vertices_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertex_count_ = 0;
    glm::vec3 bounds_min_{0.0f};
    glm::vec3 bounds_max_{0.0f};
};

#endif
