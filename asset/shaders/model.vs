// 指定使用 GLSL 3.30 核心模式，与程序创建的 OpenGL 3.3 上下文一致。
#version 330 core

// 从顶点数组属性 0 读取模型局部空间中的顶点位置。
layout (location = 0) in vec3 aPos;
// 从顶点数组属性 1 读取顶点法线；本着色器暂未参与光照计算。
layout (location = 1) in vec3 aNormal;
// 从顶点数组属性 2 读取二维纹理坐标。
layout (location = 2) in vec2 aTexCoords;

// 输出插值前的纹理坐标，光栅化阶段会为每个片元自动插值。
out vec2 TexCoords;

// 模型矩阵：把顶点从模型局部空间变换到世界空间。
uniform mat4 model;
// 观察矩阵：把世界空间坐标变换到相机观察空间。
uniform mat4 view;
// 投影矩阵：把观察空间坐标变换到裁剪空间。
uniform mat4 projection;

// 每个顶点执行一次的着色器入口函数。
void main()
{
    // 将模型自带的纹理坐标原样传递给片段着色器。
    TexCoords = aTexCoords;
    // 按“模型 → 观察 → 投影”的顺序变换顶点，并写入裁剪空间位置。
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
