#version 440

layout(binding = 0) uniform sampler2D texY;
layout(binding = 1) uniform sampler2D texU;
layout(binding = 2) uniform sampler2D texV;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    float y = texture(texY, vTexCoord).r;
    float u = texture(texU, vTexCoord).r - 0.5;
    float v = texture(texV, vTexCoord).r - 0.5;

    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;

    fragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
