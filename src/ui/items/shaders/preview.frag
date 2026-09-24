#version 440
// The video is shown exactly as decoded: no theme tint, no color conversion (SPEC §4).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D frame;
void main()
{
    fragColor = vec4(texture(frame, v_uv).rgb, 1.0);
}
