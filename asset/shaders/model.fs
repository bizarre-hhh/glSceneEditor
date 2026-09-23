// 指定使用 GLSL 3.30 核心模式，与顶点着色器版本一致。
#version 330 core

// 声明当前片元最终写入颜色缓冲区的 RGBA 颜色。
out vec4 FragColor;

// 接收光栅化阶段根据三个顶点插值得到的纹理坐标。
in vec2 TexCoords;

// 模型网格绘制时会把第一张漫反射纹理绑定到这个采样器。
uniform sampler2D texture_diffuse1;

// 每个被光栅化且通过测试的片元执行一次入口函数。
void main()
{
    // 在漫反射纹理的 TexCoords 位置采样，并把结果作为片元颜色输出。
    FragColor = texture(texture_diffuse1, TexCoords);
}
