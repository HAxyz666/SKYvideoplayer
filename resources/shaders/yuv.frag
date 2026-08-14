#version 440

layout(binding = 0) uniform sampler2D texY;
layout(binding = 1) uniform sampler2D texU;
layout(binding = 2) uniform sampler2D texV;

layout(binding = 3, std140) uniform UniformBlock {
    mat4 mvp;
    vec4 params; // x=亮度偏移  y=对比度  z=饱和度  w=保留
} ubo;

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

    vec3 c = vec3(r, g, b);

    c += ubo.params.x;
    c = (c - 0.5) * ubo.params.y + 0.5;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, ubo.params.z);

    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}