#version 430 core

layout (points) in;
layout (line_strip, max_vertices = 5) out;

in vec2 v_basePos[]; // from vertex shader
uniform mat4 projection;

// Object data from C++
uniform int numCharges;
uniform vec2 objectPositions[200];
uniform float objectCharges[200];

out float magnitude;
out float chargeInfluence;

void main() {
    vec2 basePos = v_basePos[0];

    // Compute superposed electric field vector and charge influence
    vec2 totalField = vec2(0.0);
    chargeInfluence = 0.0;

    for (int i = 0; i < numCharges; i++) {
        vec2 r = basePos - objectPositions[i];
        float d = length(r) + 1e-4; // Avoid division by zero
        float inverseSquare = 1.0 / (d * d);

        // Apply negative charge to flip the direction
        totalField += -objectCharges[i] * normalize(r) * inverseSquare;
        chargeInfluence += objectCharges[i] * inverseSquare;
    }

    magnitude = length(totalField);
    vec2 dir = (magnitude > 0.0) ? normalize(totalField) : vec2(0.0, 0.0);
    
    // Scale arrow based on magnitude
    float arrowLength = clamp(magnitude * 0.2, 0.001, 0.09);
    vec2 tip = basePos + dir * arrowLength;

    // Perpendicular for arrowhead
    vec2 perp = vec2(-dir.y, dir.x);
    float headLength = arrowLength / 3.0;
    float headWidth = arrowLength / 6.0;

    vec2 left = tip - dir * headLength + perp * headWidth;
    vec2 right = tip - dir * headLength - perp * headWidth;

    // Emit geometry (shaft + arrowhead)
    magnitude = length(totalField);
    chargeInfluence = chargeInfluence;

    gl_Position = projection * vec4(basePos, 0.0, 1.0);
    EmitVertex();

    gl_Position = projection * vec4(tip, 0.0, 1.0);
    EmitVertex();

    gl_Position = projection * vec4(left, 0.0, 1.0);
    EmitVertex();

    gl_Position = projection * vec4(tip, 0.0, 1.0);
    EmitVertex();

    gl_Position = projection * vec4(right, 0.0, 1.0);
    EmitVertex();

    EndPrimitive();
}