// 防止 Mesh、Vertex 和 Texture 在同一个编译单元中被重复定义。
#ifndef MESH_H
// 定义头文件保护宏；model.h 和其他动画示例都依赖这个名字。
#define MESH_H

// 引入 OpenGL 类型、常量以及缓冲区/纹理操作函数。
#include <glad/glad.h>

// 引入 GLM 的向量类型。
#include <glm/glm.hpp>
// 引入 GLM 矩阵变换支持；该公共头文件与其他教程保持兼容。
#include <glm/gtc/matrix_transform.hpp>

// Mesh::Draw 需要通过 Shader.ID 查询 sampler uniform 的位置。
#include "shader.h"

// string 保存纹理类型和资源路径。
#include <string>
// vector 保存顶点、索引和纹理集合。
#include <vector>
// 沿用教程代码风格，使下方可以直接使用 string 和 vector。
using namespace std;

// 每个顶点最多关联 4 根骨骼；骨骼动画示例会使用这些字段。
#define MAX_BONE_INFLUENCE 4

// Vertex 描述 GPU 顶点缓冲区中一个顶点的全部属性。
struct Vertex
{
    // 模型空间中的顶点位置，对应顶点属性 0。
    glm::vec3 Position;
    // 顶点法线方向，对应顶点属性 1。
    glm::vec3 Normal;
    // 二维纹理坐标，对应顶点属性 2。
    glm::vec2 TexCoords;
    // 切线方向，对应顶点属性 3。
    glm::vec3 Tangent;
    // 副切线方向，对应顶点属性 4。
    glm::vec3 Bitangent;
    // 会影响该顶点的骨骼编号，对应整数顶点属性 5。
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    // 每根骨骼对该顶点的影响权重，对应浮点顶点属性 6。
    float m_Weights[MAX_BONE_INFLUENCE];
};

// Texture 保存一张已经创建的 OpenGL 纹理及其材质元数据。
struct Texture
{
    // OpenGL 生成的纹理对象编号。
    unsigned int id;
    // 纹理用途，例如 texture_diffuse 或 texture_specular。
    string type;
    // 材质文件中记录的原始纹理路径，用于去重缓存。
    string path;
};

// Mesh 封装一个可独立绘制的网格及其 GPU 缓冲区。
class Mesh
{
public:
    // CPU 端保留的网格顶点数组。
    vector<Vertex> vertices;
    // CPU 端保留的索引数组；每三个索引组成一个三角形。
    vector<unsigned int> indices;
    // 当前网格需要绑定的全部材质纹理。
    vector<Texture> textures;
    // 顶点数组对象，记录顶点属性布局和关联的缓冲区。
    unsigned int VAO;

    // 使用顶点、索引和纹理数据创建网格对象。
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures)
    {
        // 把调用方传入的顶点数组保存到成员变量。
        this->vertices = vertices;
        // 把调用方传入的索引数组保存到成员变量。
        this->indices = indices;
        // 把调用方传入的纹理列表保存到成员变量。
        this->textures = textures;

        // 数据已经齐备，创建 VAO/VBO/EBO 并设置顶点属性指针。
        setupMesh();
    }

    // 使用传入的 Shader 绘制当前网格。
    void Draw(Shader& shader)
    {
        // 为每种材质纹理维护从 1 开始的编号，用于拼接 sampler 名称。
        unsigned int diffuseNr = 1;
        // 维护镜面反射纹理编号。
        unsigned int specularNr = 1;
        // 维护法线纹理编号。
        unsigned int normalNr = 1;
        // 维护高度纹理编号。
        unsigned int heightNr = 1;

        // 依次处理当前网格的所有纹理。
        for (unsigned int i = 0; i < textures.size(); ++i)
        {
            // 先激活纹理单元 i，再把当前纹理绑定到该单元。
            glActiveTexture(GL_TEXTURE0 + i);

            // number 保存同一类型纹理名称末尾的 N，例如 texture_diffuse1。
            string number;
            // 取出纹理类型名称，例如 texture_diffuse。
            string name = textures[i].type;
            // 漫反射纹理依次命名为 texture_diffuse1、texture_diffuse2……。
            if (name == "texture_diffuse")
                number = std::to_string(diffuseNr++);
            // 镜面反射纹理依次命名为 texture_specular1、texture_specular2……。
            else if (name == "texture_specular")
                number = std::to_string(specularNr++);
            // 法线纹理依次命名为 texture_normal1、texture_normal2……。
            else if (name == "texture_normal")
                number = std::to_string(normalNr++);
            // 高度纹理依次命名为 texture_height1、texture_height2……。
            else if (name == "texture_height")
                number = std::to_string(heightNr++);

            // 根据“类型 + 编号”找到着色器 sampler，并告诉它使用纹理单元 i。
            glUniform1i(
                glGetUniformLocation(shader.ID, (name + number).c_str()),
                i
            );
            // 把纹理对象绑定到当前活动纹理单元的二维纹理目标。
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }

        // 绑定当前网格的 VAO，恢复之前 setupMesh 记录的属性布局。
        glBindVertexArray(VAO);
        // 根据索引数组绘制三角形；索引数量就是 indices 的元素数量。
        glDrawElements(
            GL_TRIANGLES,
            static_cast<unsigned int>(indices.size()),
            GL_UNSIGNED_INT,
            0
        );
        // 绘制结束后解除 VAO，避免后续操作误修改当前网格状态。
        glBindVertexArray(0);

        // 将活动纹理单元恢复为第 0 个，减少对后续绘制状态的影响。
        glActiveTexture(GL_TEXTURE0);
    }

private:
    // VBO 保存顶点属性数据，EBO 保存索引数据。
    unsigned int VBO, EBO;

    // 创建网格的 GPU 缓冲区，并描述 Vertex 结构的内存布局。
    void setupMesh()
    {
        // 创建一个 VAO、一个顶点缓冲区和一个索引缓冲区。
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        // 后续顶点属性和缓冲区绑定关系都记录到这个 VAO 中。
        glBindVertexArray(VAO);

        // 绑定顶点缓冲区，准备上传 Vertex 数组。
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        // Vertex 是标准布局结构体，成员按声明顺序连续排列，可整体上传。
        glBufferData(
            GL_ARRAY_BUFFER,
            vertices.size() * sizeof(Vertex),
            &vertices[0],
            GL_STATIC_DRAW
        );

        // 绑定索引缓冲区，准备上传三角形索引。
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        // 上传所有 unsigned int 索引，并标记为不会频繁修改的数据。
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            indices.size() * sizeof(unsigned int),
            &indices[0],
            GL_STATIC_DRAW
        );

        // 开启顶点位置属性 0。
        glEnableVertexAttribArray(0);
        // 从 Vertex 起始地址读取 3 个 float 作为 Position。
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

        // 开启顶点法线属性 1。
        glEnableVertexAttribArray(1);
        // 使用 Normal 成员相对于结构体起点的偏移读取法线。
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, Normal)
        );

        // 开启纹理坐标属性 2。
        glEnableVertexAttribArray(2);
        // 使用 TexCoords 成员的偏移读取两个 float。
        glVertexAttribPointer(
            2,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, TexCoords)
        );

        // 开启切线属性 3。
        glEnableVertexAttribArray(3);
        // 使用 Tangent 成员的偏移读取三个 float。
        glVertexAttribPointer(
            3,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, Tangent)
        );

        // 开启副切线属性 4。
        glEnableVertexAttribArray(4);
        // 使用 Bitangent 成员的偏移读取三个 float。
        glVertexAttribPointer(
            4,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, Bitangent)
        );

        // 开启骨骼编号属性 5；整数属性必须使用 glVertexAttribIPointer。
        glEnableVertexAttribArray(5);
        // 从 m_BoneIDs 偏移处读取四个有符号整数。
        glVertexAttribIPointer(
            5,
            4,
            GL_INT,
            sizeof(Vertex),
            (void*)offsetof(Vertex, m_BoneIDs)
        );

        // 开启骨骼权重属性 6。
        glEnableVertexAttribArray(6);
        // 从 m_Weights 偏移处读取四个 float 权重。
        glVertexAttribPointer(
            6,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex),
            (void*)offsetof(Vertex, m_Weights)
        );

        // 解除 VAO 绑定，结束网格初始化阶段。
        glBindVertexArray(0);
    }
};

// 结束 MESH_H 头文件保护条件。
#endif
