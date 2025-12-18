#version 430 core

layout(points) in;
layout(triangle_strip, max_vertices = 64) out;

in vec2 vPosition[];
in vec2 vVelocity[];
in float vMass[];
in float vCharge[];
in int vVisualSkinType[];
in vec4 vVisualData[];  // x=rotation, y=angular_vel, z=width/radius, w=height/sides
in vec4 vColor[];

out vec2 fragTexCoord;
out vec4 fragColor;
out float fragSpeed;

uniform mat4 uProjection;
uniform mat4 uView;

const int SKIN_CIRCLE = 0;
const int SKIN_RECTANGLE = 1;
const int SKIN_POLYGON = 2;
const float PI = 3.14159265359;

void emitCircle(vec2 center, float radius, vec4 color, float speed) {
    mat4 MVP = uProjection * uView;
    
    // Bottom-left
    gl_Position = MVP * vec4(center + vec2(-radius, -radius), 0.0, 1.0);
    fragTexCoord = vec2(-1.0, -1.0);
    fragColor = color;
    fragSpeed = speed;
    EmitVertex();
    
    // Top-left
    gl_Position = MVP * vec4(center + vec2(-radius, radius), 0.0, 1.0);
    fragTexCoord = vec2(-1.0, 1.0);
    fragColor = color;
    fragSpeed = speed;
    EmitVertex();
    
    // Bottom-right
    gl_Position = MVP * vec4(center + vec2(radius, -radius), 0.0, 1.0);
    fragTexCoord = vec2(1.0, -1.0);
    fragColor = color;
    fragSpeed = speed;
    EmitVertex();
    
    // Top-right
    gl_Position = MVP * vec4(center + vec2(radius, radius), 0.0, 1.0);
    fragTexCoord = vec2(1.0, 1.0);
    fragColor = color;
    fragSpeed = speed;
    EmitVertex();
    
    EndPrimitive();
}

void emitRectangle(vec2 center, float width, float height, float rotation, vec4 color, float speed) {
    mat4 MVP = uProjection * uView;
    
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    float hw = width * 0.5;
    float hh = height * 0.5;
    
    vec2 corners[4];
    corners[0] = vec2(-hw, -hh);
    corners[1] = vec2( hw, -hh);
    corners[2] = vec2(-hw,  hh);
    corners[3] = vec2( hw,  hh);
    
    for (int i = 0; i < 4; i++) {
        vec2 rotated = vec2(
            corners[i].x * cosR - corners[i].y * sinR,
            corners[i].x * sinR + corners[i].y * cosR
        );
        gl_Position = MVP * vec4(center + rotated, 0.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = color;
        fragSpeed = speed;
        EmitVertex();
    }
    EndPrimitive();
}

void emitPolygon(vec2 center, float radius, int numSides, float rotation, vec4 color, float speed) {
    mat4 MVP = uProjection * uView;
    numSides = clamp(numSides, 3, 20);
    
    vec2 pts[21];
    for (int i = 0; i < numSides; i++) {
        float angle = rotation + (2.0 * PI * float(i) / float(numSides));
        pts[i] = center + vec2(cos(angle), sin(angle)) * radius;
    }
    
    for (int i = 0; i < numSides; i++) {
        gl_Position = MVP * vec4(pts[i], 0.0, 1.0);
        fragColor = color;
        fragSpeed = speed;
        fragTexCoord = vec2(0.0);
        EmitVertex();
        
        gl_Position = MVP * vec4(center, 0.0, 1.0);
        fragColor = color;
        fragSpeed = speed;
        fragTexCoord = vec2(0.0);
        EmitVertex();
    }
    
    gl_Position = MVP * vec4(pts[0], 0.0, 1.0);
    fragColor = color;
    fragSpeed = speed;
    fragTexCoord = vec2(0.0);
    EmitVertex();
    
    EndPrimitive();
}

void main() {
    vec2 pos = vPosition[0];
    vec2 vel = vVelocity[0];
    float speed = length(vel);
    int skinType = vVisualSkinType[0];
    
    // NEW LAYOUT: width/height in x/y, rotation/angular_vel in z/w
    float param_x = vVisualData[0].x;       // width or radius
    float param_y = vVisualData[0].y;       // height or numSides
    float rotation = vVisualData[0].z;      // ROTATION NOW IN Z
    float angular_vel = vVisualData[0].w;   // ANGULAR_VEL NOW IN W
    
    vec4 color = vColor[0];
    
    // Fallback to charge-based color if white
    if (length(color.rgb - vec3(1.0)) < 0.01) {
        float charge = vCharge[0];
        if (charge > 0.0) color = vec4(1.0, 0.2, 0.2, 1.0);
        else if (charge < 0.0) color = vec4(0.2, 0.2, 1.0, 1.0);
        else color = vec4(0.8, 0.4, 0.1, 1.0);
    }
    
    if (skinType == SKIN_CIRCLE) {
        float radius = (param_x < 0.01) ? 0.3 : param_x;  //   x = radius
        emitCircle(pos, radius, color, speed);
    }
    else if (skinType == SKIN_RECTANGLE) {
        float width = (param_x < 0.01) ? 0.5 : param_x;   //   x = width
        float height = (param_y < 0.01) ? 0.3 : param_y;  //   y = height
        emitRectangle(pos, width, height, rotation, color, speed);
    }
    else if (skinType == SKIN_POLYGON) {
        float radius = (param_x < 0.01) ? 0.3 : param_x;          //   x = radius
        int numSides = (int(param_y) < 3) ? 6 : int(param_y);     //   y = numSides
        emitPolygon(pos, radius, numSides, rotation, color, speed);
    }
}