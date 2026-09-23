// 防止模型加载类型和函数在同一编译单元中被重复定义。
#ifndef MODEL_H
// 定义头文件保护宏。
#define MODEL_H

// 引入纹理创建、绑定、上传等 OpenGL 函数和常量。
#include <glad/glad.h>

// 引入顶点数据使用的 GLM 向量类型。
#include <glm/glm.hpp>
// 引入可能用于模型变换的 GLM 矩阵函数。
#include <glm/gtc/matrix_transform.hpp>
// 引入 stb_image 的图片解码接口。
#include <stb_image.h>
// 引入 Assimp 的模型文件导入器。
#include <assimp/Importer.hpp>
// 引入 Assimp 场景、节点、网格和材质等数据结构。
#include <assimp/scene.h>
// 引入 Assimp 读取模型时可使用的后处理标志。
#include <assimp/postprocess.h>

// 引入 LearnOpenGL 的网格类以及 Vertex、Texture 数据结构。
#include "mesh.h"
// 引入 LearnOpenGL 的着色器类型，供 Mesh::Draw 接口使用。
#include "shader.h"

// std::string 用于保存模型目录、纹理路径和纹理类型名。
#include <string>
// 保留文件流支持，兼容 LearnOpenGL 模型辅助代码的使用方式。
#include <fstream>
// 保留字符串流支持，便于后续扩展文本解析逻辑。
#include <sstream>
// std::cout 用于输出 Assimp 与纹理加载错误。
#include <iostream>
// std::strcmp 用于比较已加载纹理和当前材质纹理的路径。
#include <cstring>
// std::map 供 LearnOpenGL 模型加载代码的扩展版本使用。
#include <map>
// std::vector 用于保存顶点、索引、纹理和网格集合。
#include <vector>

// 沿用教程原始写法，使下方代码可以直接写 string 和 vector。
using namespace std;

// 提前声明纹理加载函数，因为 Model 成员函数会在其定义之前调用它。
unsigned int TextureFromFile(const char* path,const string& directory,bool gamma = false);

// Model 表示一个由若干 Mesh 组成、可一次加载并整体绘制的三维模型。
class Model
{
public:
    // 缓存已经上传过的纹理，避免同一路径被不同网格重复加载到 GPU。
    vector<Texture> textures_loaded;
    // 保存从 Assimp 场景树中提取出的全部可绘制网格。
    vector<Mesh> meshes;
    // 保存模型文件所在目录，用于解析材质中的相对纹理路径。
    string directory;
    // 保存是否进行 Gamma 校正的选项；本基础示例尚未使用它改变内部格式。
    bool gammaCorrection;

    // 构造模型时立即读取给定文件，并可选择是否启用 Gamma 校正。
    Model(const string& path, bool gamma = false)
        // 将调用方的 Gamma 选项保存到成员变量。
        : gammaCorrection(gamma)
    {
        // 解析模型并生成 meshes 与 textures_loaded。
        loadModel(path);
    }

    // 使用指定着色器依次绘制模型包含的全部网格。
    void Draw(Shader& shader)
    {
        // 遍历 meshes 中的每一个独立网格。
        for (unsigned int i = 0; i < meshes.size(); ++i)
            meshes[i].Draw(shader); // Mesh 负责绑定自己的纹理、VAO 并调用 glDrawElements。
    }

private:
    // 通过 Assimp 读取模型文件，再从场景根节点开始递归提取网格。
    void loadModel(const string& path)
    {
        // Importer 管理 Assimp 导入过程以及导入后场景数据的生命周期。
        Assimp::Importer importer;
        // 读取模型并要求 Assimp 执行三角化、平滑法线、UV 翻转和切线计算。
        const aiScene* scene = importer.ReadFile(
            path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace
        );

        // 场景为空、数据不完整或没有根节点时，说明模型读取失败。
        if (!scene ||
            (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
            !scene->mRootNode)
        {
            // 输出 Assimp 提供的具体错误原因。
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            // 终止本次模型加载，保留一个空的 meshes 集合。
            return;
        }

        // 截取最后一个斜杠以前的部分，得到 OBJ 等模型文件所在目录。
        directory = path.substr(0, path.find_last_of('/'));

        // 从根节点开始深度优先遍历完整场景层级。
        processNode(scene->mRootNode, scene);
    }

    // 处理当前 Assimp 节点引用的网格，然后递归处理它的所有子节点。
    void processNode(aiNode* node, const aiScene* scene)
    {
        // 遍历当前节点记录的全部网格索引。
        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            // 节点只保存索引，真正的 aiMesh 对象统一存放在 scene->mMeshes 中。
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            // 把 Assimp 网格转换为可由 OpenGL 绘制的 Mesh，并加入模型集合。
            meshes.push_back(processMesh(mesh, scene));
        }

        // 遍历当前节点的全部子节点。
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            // 对子节点执行相同逻辑，直至整个场景树处理完毕。
            processNode(node->mChildren[i], scene);
        }
    }

    // 将一个 Assimp aiMesh 转换成 LearnOpenGL Mesh。
    Mesh processMesh(aiMesh* mesh, const aiScene* scene)
    {
        // 收集每个顶点的位置、法线、纹理坐标、切线和副切线。
        vector<Vertex> vertices;
        // 收集组成三角形的顶点索引。
        vector<unsigned int> indices;
        // 收集该网格材质引用的全部纹理。
        vector<Texture> textures;

        // 逐个读取 Assimp 网格中的顶点。
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            // 创建 LearnOpenGL 定义的顶点结构，稍后填充每个属性。
            Vertex vertex;
            // Assimp 和 GLM 向量类型不同，使用临时 GLM 向量逐分量转换。
            glm::vec3 vector;

            // 读取当前顶点位置的 X 分量。
            vector.x = mesh->mVertices[i].x;
            // 读取当前顶点位置的 Y 分量。
            vector.y = mesh->mVertices[i].y;
            // 读取当前顶点位置的 Z 分量。
            vector.z = mesh->mVertices[i].z;
            // 将完整位置向量写入顶点结构。
            vertex.Position = vector;

            // 只有 Assimp 确认网格包含法线时才读取法线数组。
            if (mesh->HasNormals())
            {
                // 读取法线 X 分量。
                vector.x = mesh->mNormals[i].x;
                // 读取法线 Y 分量。
                vector.y = mesh->mNormals[i].y;
                // 读取法线 Z 分量。
                vector.z = mesh->mNormals[i].z;
                // 将完整法线向量写入顶点结构。
                vertex.Normal = vector;
            }

            // mTextureCoords[0] 非空表示模型至少提供了第一组纹理坐标。
            if (mesh->mTextureCoords[0])
            {
                // 纹理坐标只有 U、V 两个分量，因此使用 vec2。
                glm::vec2 texCoords;
                // 读取第一组纹理坐标的 U/X 分量。
                texCoords.x = mesh->mTextureCoords[0][i].x;
                // 读取第一组纹理坐标的 V/Y 分量。
                texCoords.y = mesh->mTextureCoords[0][i].y;
                // 保存当前顶点的纹理坐标。
                vertex.TexCoords = texCoords;

                // 读取 Assimp 计算出的切线 X 分量。
                vector.x = mesh->mTangents[i].x;
                // 读取切线 Y 分量。
                vector.y = mesh->mTangents[i].y;
                // 读取切线 Z 分量。
                vector.z = mesh->mTangents[i].z;
                // 保存切线，供法线贴图等高级效果使用。
                vertex.Tangent = vector;

                // 读取 Assimp 计算出的副切线 X 分量。
                vector.x = mesh->mBitangents[i].x;
                // 读取副切线 Y 分量。
                vector.y = mesh->mBitangents[i].y;
                // 读取副切线 Z 分量。
                vector.z = mesh->mBitangents[i].z;
                // 保存副切线，与法线、切线共同组成 TBN 空间。
                vertex.Bitangent = vector;
            }
            // 模型没有纹理坐标时，提供确定的零值，避免使用未初始化数据。
            else
            {
                // 将 U、V 都设为 0；无纹理模型仍可安全上传顶点数据。
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }

            // 当前顶点的所有可用属性处理完毕，将它加入顶点数组。
            vertices.push_back(vertex);
        }

        // Assimp 已将面三角化；此处逐个读取网格中的面。
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            // 取得当前面的索引列表。
            aiFace face = mesh->mFaces[i];
            // 遍历该面包含的每个顶点索引；三角化后通常正好有三个。
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
                indices.push_back(face.mIndices[j]); // 按原顺序追加到元素索引缓冲。
        }

        // 通过网格的材质索引，从场景材质数组中取出对应材质。
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        // 读取全部漫反射纹理，并采用 texture_diffuseN 命名约定。
        vector<Texture> diffuseMaps = loadMaterialTextures(material,aiTextureType_DIFFUSE,"texture_diffuse");
        // 把漫反射纹理追加到当前网格的纹理集合。
        textures.insert(textures.end(), diffuseMaps.begin(),diffuseMaps.end());

        // 读取全部镜面反射纹理，并采用 texture_specularN 命名约定。
        vector<Texture> specularMaps = loadMaterialTextures(material,aiTextureType_SPECULAR,"texture_specular");
        // 把镜面反射纹理追加到当前网格的纹理集合。
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());

        // 教程约定把 Assimp 的 HEIGHT 类型作为法线纹理读取。
        vector<Texture> normalMaps = loadMaterialTextures(material,aiTextureType_HEIGHT,"texture_normal");
        // 把法线纹理追加到当前网格的纹理集合。
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

        // 教程约定把 Assimp 的 AMBIENT 类型作为高度纹理读取。
        vector<Texture> heightMaps = loadMaterialTextures(material,aiTextureType_AMBIENT,"texture_height");
        // 把高度纹理追加到当前网格的纹理集合。
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());

        // 构造 Mesh；其构造函数会建立 VAO、VBO 和 EBO 并上传这些数据。
        return Mesh(vertices, indices, textures);
    }

    // 读取材质中某一类纹理，同时复用模型范围内已经加载过的纹理。
    vector<Texture> loadMaterialTextures( aiMaterial* material, aiTextureType type, const string& typeName )
    {
        // 保存该材质、该类型最终使用的纹理列表。
        vector<Texture> textures;

        // 遍历材质中 type 类型纹理的全部路径。
        for (unsigned int i = 0; i < material->GetTextureCount(type); ++i)
        {
            // aiString 用于接收 Assimp 返回的纹理相对路径。
            aiString path;
            // 读取第 i 张该类型纹理的路径。
            material->GetTexture(type, i, &path);

            // skip 表示稍后是否应跳过从磁盘重新加载。
            bool skip = false;
            // 在整个模型的纹理缓存中寻找相同路径。
            for (unsigned int j = 0; j < textures_loaded.size(); ++j)
            {
                // strcmp 返回 0 表示两个以 \0 结尾的路径完全相同。
                if (std::strcmp(textures_loaded[j].path.data(), path.C_Str()) == 0)
                {
                    // 复用缓存中的纹理 ID、类型和路径。
                    textures.push_back(textures_loaded[j]);
                    // 标记已经命中缓存，不需要再次调用 stb_image 和 OpenGL。
                    skip = true;
                    // 已找到唯一匹配项，无需继续扫描缓存。
                    break;
                }
            }

            // 只有缓存中不存在该路径时才创建新纹理。
            if (!skip)
            {
                // 创建一个待填充的 LearnOpenGL Texture 结构。
                Texture texture;
                // 解码并上传图片，返回 OpenGL 纹理对象 ID。
                texture.id = TextureFromFile(path.C_Str(), directory);
                // 保存 texture_diffuse、texture_specular 等逻辑类型名。
                texture.type = typeName;
                // 保存模型材质中记录的原始相对路径。
                texture.path = path.C_Str();
                // 当前网格需要使用这张新纹理。
                textures.push_back(texture);
                // 同时写入模型级缓存，供后续网格复用。
                textures_loaded.push_back(texture);
            }
        }

        // 返回当前材质指定类型的完整纹理列表。
        return textures;
    }
};

// 从模型目录读取一张图片并创建对应的 OpenGL 2D 纹理对象。
unsigned int TextureFromFile(const char* path,const string& directory,bool gamma)
{
    // 本基础版本尚未根据 gamma 切换内部格式，显式忽略该参数。
    (void)gamma;
    // 把 Assimp 返回的 C 字符串路径转换为 std::string。
    string filename = string(path);
    // 在相对纹理路径前拼接模型文件所在目录。
    filename = directory + '/' + filename;

    // 保存 OpenGL 即将创建的纹理对象编号。
    unsigned int textureID;
    // 让 OpenGL 生成一个唯一的 2D 纹理对象 ID。
    glGenTextures(1, &textureID);

    // width 和 height 接收图片像素尺寸，nrComponents 接收每像素通道数。
    int width, height, nrComponents;
    // 让 stb_image 解码图片；最后一个 0 表示保留图片原始通道数。
    unsigned char* data = stbi_load(filename.c_str(),&width,&height,&nrComponents,0);

    // data 非空说明图片读取和解码成功。
    if (data)
    {
        // 根据图片通道数选择 OpenGL 像素格式。
        GLenum format;
        // 单通道图片通常按灰度/红色通道上传。
        if (nrComponents == 1)
            format = GL_RED;
        // 三通道图片按 RGB 上传。
        else if (nrComponents == 3)
            format = GL_RGB;
        // 四通道图片包含 Alpha，按 RGBA 上传。
        else if (nrComponents == 4)
            format = GL_RGBA;

        // 将新纹理绑定到当前活动纹理单元的 GL_TEXTURE_2D 目标。
        glBindTexture(GL_TEXTURE_2D, textureID);
        // 把 CPU 图片数据上传为纹理的第 0 级；内部格式与源格式保持一致。
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            format,
            width,
            height,
            0,
            format,
            GL_UNSIGNED_BYTE,
            data
        );
        // 自动根据第 0 级纹理生成后续所有缩小级别。
        glGenerateMipmap(GL_TEXTURE_2D);

        // 当 U 坐标超出 [0,1] 时重复纹理图案。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        // 当 V 坐标超出 [0,1] 时同样重复纹理图案。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        // 缩小时在相邻 mipmap 级别与各自纹素之间做线性插值。
        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR_MIPMAP_LINEAR
        );
        // 放大时对附近纹素做线性插值。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // 图片已经上传到 GPU，释放 stb_image 在 CPU 端申请的内存。
        stbi_image_free(data);
    }
    // data 为空说明文件不存在、格式不支持或图片损坏。
    else
    {
        // 输出材质中记录的原始纹理路径，便于定位资源问题。
        std::cout << "Texture failed to load at path: " << path << std::endl;
        // 对空指针调用 stbi_image_free 是安全的，并保持统一清理流程。
        stbi_image_free(data);
    }

    // 返回纹理对象 ID，由 Texture 结构保存并在 Mesh::Draw 中绑定。
    return textureID;
}

// 结束 MODEL_H 头文件保护条件。
#endif
