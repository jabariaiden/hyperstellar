#version 430 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

struct Object {
    vec4 position;
    vec4 velocity;
    vec4 orientation;
    vec4 spin;
    vec4 visualData;
    vec4 collisionData;
    vec4 color;
    float mass;
    float charge;
    int visualSkinType;
    int collisionShapeType;
    int equationID;
    int scriptID;
    int meshID;
    int _pad[5];
};
layout(std430, binding=0) readonly buffer Objects { Object objects[]; };
// Indirection buffer: instanced draws look up scattered object slots by index
layout(std430, binding=12) readonly buffer DrawIndex { int drawIndex[]; };

uniform mat4 uProjection;
uniform mat4 uView;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vColor;
out vec2 vUV;

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 u = q.xyz; float s = q.w;
    return 2.0 * dot(u, v) * u + (s*s - dot(u,u)) * v + 2.0 * s * cross(u, v);
}

void main() {
    int slot = drawIndex[gl_InstanceID];
    Object inst = objects[slot];

    vec3 worldPos = inst.position.xyz + quat_rotate(inst.orientation, aPos);
    vec3 worldNrm = quat_rotate(inst.orientation, aNormal);

    vWorldPos = worldPos;
    vNormal   = worldNrm;
    vColor    = inst.color;
    vUV       = aUV;

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}