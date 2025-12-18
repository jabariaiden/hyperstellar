#version 430 core

in float magnitude;
in float chargeInfluence; 

out vec4 FragColor;

void main() {
    vec3 lowMagnitudeColor = vec3(0.0, 0.5, 1.0);  // A vibrant blue
    vec3 midMagnitudeColor = vec3(1.0, 1.0, 0.0);  // A pure green
    vec3 highMagnitudeColor = vec3(1.0, 0.5, 0.0); // An intense orange

    float normalizedMagnitude = clamp(pow(magnitude, .9) * .1, 0.0, 1.0);

    vec3 finalColor;

    if (normalizedMagnitude <= 0.5) {
        float t = normalizedMagnitude * 2.0;
        finalColor = mix(lowMagnitudeColor, midMagnitudeColor, t);
    } 
    else {
        float t = (normalizedMagnitude - 0.5) * 2.0;
        finalColor = mix(midMagnitudeColor, highMagnitudeColor, t);
    }

    FragColor = vec4(finalColor, 1.0);
}