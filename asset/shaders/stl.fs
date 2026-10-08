#version 330 core

in vec3 worldNormal;
out vec4 FragColor;

uniform bool selected;

void main()
{
    vec3 normal = normalize(worldNormal);
    vec3 lightDirection = normalize(vec3(0.4, 0.6, 1.0));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 baseColor = selected ? vec3(1.0, 0.55, 0.10) : vec3(0.72, 0.76, 0.82);
    vec3 color = baseColor * (0.38 + 0.62 * diffuse);
    FragColor = vec4(color, 1.0);
}
