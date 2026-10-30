#version 430 core
in vec3 fWorldPos;
in vec3 fNormal;
in vec4 fColor;
in vec2 fUV;

uniform vec3 uCameraPos;
uniform vec3 uLightDir;    // normalized, points FROM light

out vec4 FragColor;

void main() {
    vec3 N = normalize(fNormal);
    vec3 V = normalize(uCameraPos - fWorldPos);
    vec3 H = normalize(V - uLightDir);

    float ndl  = max(dot(N, -uLightDir), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.25;

    vec3 lit = fColor.rgb * (0.25 + 0.75 * ndl) + vec3(spec);
    FragColor = vec4(lit, fColor.a);
}