#version 430 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in float aWidth;

uniform mat4 uProjView;

out vec3 color;

void main() {
    gl_Position = uProjView * vec4(aPos, 0.0, 1.0);
    color = aColor; // Pass color to fragment shader
}