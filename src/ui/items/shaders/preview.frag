#version 440
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform Params {
    mat4 mvp;
    int format; // 0 = RGBA, 1 = NV12, 2 = P010
};
layout(binding = 1) uniform sampler2D texY;
layout(binding = 2) uniform sampler2D texUV;

void main()
{
    if (format == 1) {
        // NV12: 8-bit Y + UV semi-planar (BT.709 color conversion in GPU shader)
        float y = texture(texY, v_uv).r;
        vec2 uv = texture(texUV, v_uv).rg;
        float yNorm = (y - (16.0 / 255.0)) * (255.0 / 219.0);
        vec2 chroma = uv - vec2(128.0 / 255.0);
        vec3 rgb;
        rgb.r = yNorm + 1.5748 * chroma.y;
        rgb.g = yNorm - 0.1873 * chroma.x - 0.4681 * chroma.y;
        rgb.b = yNorm + 1.8556 * chroma.x;
        fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
    } else if (format == 2) {
        // P010: 10-bit Y + UV semi-planar (BT.2020/BT.709 color conversion)
        float y = texture(texY, v_uv).r;
        vec2 uv = texture(texUV, v_uv).rg;
        float yNorm = (y - (64.0 / 1023.0)) * (1023.0 / 876.0);
        vec2 chroma = uv - vec2(512.0 / 1023.0);
        vec3 rgb;
        rgb.r = yNorm + 1.4746 * chroma.y;
        rgb.g = yNorm - 0.1645 * chroma.x - 0.5714 * chroma.y;
        rgb.b = yNorm + 1.8814 * chroma.x;
        fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
    } else {
        // 0 = RGBA
        fragColor = vec4(texture(texY, v_uv).rgb, 1.0);
    }
}
