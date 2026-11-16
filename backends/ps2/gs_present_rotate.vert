#version 450

layout(location = 0) out vec2 uv;

void main()
{
    int corner = gl_VertexIndex % 3;
    vec2 p = vec2(corner == 2 ? 2.0 : 0.0, corner == 1 ? 2.0 : 0.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);

    // firstVertex advances by three for each clockwise quarter-turn.
    switch (gl_VertexIndex / 3) {
    case 1: uv = vec2(p.y, 1.0 - p.x); break;
    case 2: uv = 1.0 - p; break;
    case 3: uv = vec2(1.0 - p.y, p.x); break;
    default: uv = p; break;
    }
}
