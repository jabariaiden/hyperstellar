#version 430 core
layout(location = 0) in vec2 basePos;
uniform vec2 chargePos = vec2(0.0, 0.0);
uniform mat4 projection;
out vec2 v_basePos;
out vec2 v_chargePos;
void main()
{
    vec4 hi = projection * vec4(basePos, 0.0, 1.0);
    gl_Position = vec4(basePos, 0.0, 1.0); 
    v_basePos = basePos;
    v_chargePos = chargePos;
}