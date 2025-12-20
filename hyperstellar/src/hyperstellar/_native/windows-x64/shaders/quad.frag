#version 430 core

in vec2 fragTexCoord;
in vec4 fragColor;
in float fragSpeed;

out vec4 FragColor;

void main() {
    // Circle if texcoord is non-zero (from circle emission)
    bool isCircle = fragTexCoord != vec2(0.0);

    if (isCircle) {
        float dist = length(fragTexCoord);

        // HARD EDGE: Discard everything outside the exact circle boundary
        if (dist > 1.0) discard;

        // No smoothstep - just use the original color with full opacity
        FragColor = fragColor;

    } else {
        // Rectangle & polygon (texcoord = 0) - use color as-is
        FragColor = fragColor;
    }
}