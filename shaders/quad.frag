#version 430 core
in vec3 fWorldPos;
in vec3 fNormal;
in vec4 fColor;
in vec2 fUV;
flat in int fObjectSlot;

uniform vec3 uCameraPos;
uniform vec3 uLightDir;    // normalized, points FROM light
uniform int uMaterialID;   // -1 = use Object.color
uniform int uDirectionalLightEnabled; // 0 = directional light disabled, 1 = enabled
uniform int uMaterialCount;
uniform int uEmissiveLightCapacity;

struct Material {
    vec4 color;
    float intensity;
    uint type;
    uint _padding0;
    uint _padding1;
};
layout(std430, binding = 10) readonly buffer MaterialBuffer {
    Material materials[];
};

struct EmissiveLight {
    vec4 position;
    vec4 colorIntensity; // rgb = emitted color, a = intensity
    int objectSlot;
    int _padding0;
    int _padding1;
    int _padding2;
};
layout(std430, binding = 11) readonly buffer EmissiveLightBuffer {
    uint emissiveLightCount;
    uint _headerPadding0;
    uint _headerPadding1;
    uint _headerPadding2;
    EmissiveLight emissiveLights[];
};

out vec4 FragColor;

void main() {
    vec3 N = normalize(fNormal);

    float ndl = 0.0;
    float spec = 0.0;

    if (uDirectionalLightEnabled != 0) {
        vec3 V = normalize(uCameraPos - fWorldPos);
        vec3 H = normalize(V - uLightDir);

        ndl = max(dot(N, -uLightDir), 0.0);
        spec = pow(max(dot(N, H), 0.0), 48.0) * 0.25;
    }

    vec4 surfaceColor = fColor;
    vec3 selfEmission = vec3(0.0);
    bool isEmissive = false;

    if (uMaterialID >= 0 && uMaterialID < uMaterialCount) {
        Material material = materials[uMaterialID];
        if (material.type == 0u) {
            // Solid-color material: material RGBA replaces the legacy object color.
            surfaceColor = material.color;
        } else if (material.type == 1u) {
            // Emissive material: the surface emits its own RGB radiance.
            isEmissive = true;
            selfEmission = material.color.rgb * max(material.intensity, 0.0);
            surfaceColor.a = 1.0;
        }
    }

    if (!isEmissive && surfaceColor.a <= 0.0)
        discard;

    // Emissive meshes also act as point lights for other visible surfaces.
    // A simple inverse-distance attenuation is used for this first renderer pass.
    vec3 emissiveIllumination = vec3(0.0);
    uint activeLightCount = min(emissiveLightCount,
                                uint(max(uEmissiveLightCapacity, 0)));
    for (uint i = 0u; i < activeLightCount; ++i) {
        EmissiveLight light = emissiveLights[i];
        if (light.objectSlot == fObjectSlot)
            continue;

        vec3 toLight = light.position.xyz - fWorldPos;
        float distanceSquared = max(dot(toLight, toLight), 0.0001);
        vec3 L = toLight * inversesqrt(distanceSquared);
        float lightNdotL = max(dot(N, L), 0.0);
        float attenuation = max(light.colorIntensity.a, 0.0) /
                            (1.0 + distanceSquared);
        emissiveIllumination += light.colorIntensity.rgb *
                                lightNdotL * attenuation;
    }

    vec3 litColor;
    if (isEmissive) {
        litColor = selfEmission + emissiveIllumination;
    } else {
        litColor = surfaceColor.rgb * (0.25 + 0.75 * ndl)
                 + vec3(spec)
                 + emissiveIllumination;
    }

    FragColor = vec4(litColor, isEmissive ? 1.0 : surfaceColor.a);
}
