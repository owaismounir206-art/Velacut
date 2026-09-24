#version 440
// Video preview quad. position.xy: clip-space corner (GL convention), position.zw: texture coordinate.
layout(location = 0) in vec4 position;
layout(location = 0) out vec2 v_uv;
layout(std140, binding = 0) uniform Params {
    mat4 mvp;
};
void main()
{
    v_uv = position.zw;
    gl_Position = mvp * vec4(position.xy, 0.0, 1.0);
}
