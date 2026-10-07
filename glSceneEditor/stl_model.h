#ifndef STL_MODEL_H
#define STL_MODEL_H

#include <glad/glad.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    bool Load(const std::string& path, std::string& error)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_PreTransformVertices |
            aiProcess_ValidateDataStructure);
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
              const glm::mat4& projection) const
    {
        const float world_units_per_mm = 1.0f / BasePlate::kMmPerWorldUnit;
        const glm::vec3 center = (bounds_min_ + bounds_max_) * 0.5f;
        const glm::vec3 offset(
            BasePlate::kHalfSize - center.x * world_units_per_mm,
            BasePlate::kHalfSize - center.y * world_units_per_mm,
            -bounds_min_.z * world_units_per_mm);
        glm::mat4 model(1.0f);
        model = glm::translate(model, offset);
        model = glm::scale(model, glm::vec3(world_units_per_mm));

        shader.use();
        shader.setMat4("model", model);
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
        glBindVertexArray(0);
    }

    glm::vec3 SizeMm() const { return bounds_max_ - bounds_min_; }

    void GetWorldBounds(glm::vec3& minimum, glm::vec3& maximum) const
    {
        const glm::vec3 size = SizeMm() / BasePlate::kMmPerWorldUnit;
        minimum = glm::vec3(BasePlate::kHalfSize - size.x * 0.5f,
                            BasePlate::kHalfSize - size.y * 0.5f, 0.0f);
        maximum = glm::vec3(BasePlate::kHalfSize + size.x * 0.5f,
                            BasePlate::kHalfSize + size.y * 0.5f, size.z);
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
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertex_count_ = 0;
    glm::vec3 bounds_min_{0.0f};
    glm::vec3 bounds_max_{0.0f};
};

#endif
