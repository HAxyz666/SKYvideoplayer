#version 440

layout(binding = 0) uniform sampler2D texY;
layout(binding = 1) uniform sampler2D texU;
layout(binding = 2) uniform sampler2D texV;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    float y = texture(texY, vTexCoord).x;
    float u = texture(texU, vTexCoord).x - 0.5;
    float v = texture(texV, vTexCoord).x - 0.5;
    float r = y + (1.401999950408935546875 * v);
    float g = (y - (0.3440000116825103759765625 * u)) - (0.7139999866485595703125 * v);
    float b = y + (1.77199995517730712890625 * u);
    fragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
