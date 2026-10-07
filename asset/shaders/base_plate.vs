#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;

out vec3 vertexColor;

uniform mat4 view;
uniform mat4 projection;
uniform float zOffset;

void main()
{
    vertexColor = aColor;
    gl_Position = projection * view * vec4(aPosition + vec3(0.0, 0.0, zOffset), 1.0);
}
