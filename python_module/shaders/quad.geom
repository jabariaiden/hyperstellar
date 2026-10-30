#version 430 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec3 vWorldPos[];
in vec3 vNormal[];
in vec4 vColor[];
in vec2 vUV[];

out vec3 fWorldPos;
out vec3 fNormal;
out vec4 fColor;
out vec2 fUV;

// Forwards triangle vertices, all geometry is generated CPU-side
void main() {
    for (int i = 0; i < 3; ++i) {
        fWorldPos = vWorldPos[i];
        fNormal   = vNormal[i];
        fColor    = vColor[i];
        fUV       = vUV[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}