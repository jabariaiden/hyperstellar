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

        // Mask outside circle
        if (dist > 1.0) discard;

        // Smooth edge with anti-aliasing
        float alpha = 1.0 - smoothstep(0.8, 1.0, dist);
        FragColor = vec4(fragColor.rgb, fragColor.a * alpha);

    } else {
        // Rectangle & polygon (texcoord = 0) - use color as-is
        FragColor = fragColor;
    }
}