#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 vTexCoord;

layout(binding = 3, std140) uniform UniformBlock {
    mat4 mvp;
    vec4 params; // x=亮度偏移  y=对比度  z=饱和度  w=保留
} ubo;

void main()
{
    vTexCoord = texcoord;
    gl_Position = ubo.mvp * vec4(position, 0.0, 1.0);
}