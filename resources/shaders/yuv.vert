#version 440

layout(binding = 3) uniform ubo {
    mat4 mvp;
} _25;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) in vec2 texcoord;
layout(location = 0) in vec2 position;

void main()
{
    vTexCoord = texcoord;
    gl_Position = _25.mvp * vec4(position, 0.0, 1.0);
}
