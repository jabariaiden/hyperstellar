#version 430 core

// These should match your VAO setup from objects.cpp
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aVelocity;
layout(location = 2) in float aMass;
layout(location = 3) in float aCharge;
layout(location = 4) in int aVisualSkinType;    
layout(location = 5) in vec4 aVisualData;       
layout(location = 6) in vec4 aColor;            
layout(location = 7) in int aEquationID;

out vec2 vPosition;
out vec2 vVelocity;
out float vMass;
out float vCharge;
out int vVisualSkinType;    
out vec4 vVisualData;       
out vec4 vColor;            
// NEW: Add rotation and angular velocity as separate outputs if needed
out float vRotation;
out float vAngularVelocity;

void main() {
    vPosition = aPosition;
    vVelocity = aVelocity;
    vMass = aMass;
    vCharge = aCharge;
    vVisualSkinType = aVisualSkinType;
    vVisualData = aVisualData;
    vColor = aColor;
    
    // Extract rotation and angular velocity from visualData
    vRotation = aVisualData.x;
    vAngularVelocity = aVisualData.y;
    
    gl_Position = vec4(aPosition, 0.0, 1.0);
    gl_PointSize = 10.0; // For point rendering fallback
}