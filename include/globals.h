#pragma once
#include <glm/glm.hpp>
#include "camera.h"           // Include full definitions

// Visual Skin Types
enum visualSkinType
{
    SKIN_CIRCLE = 0,      
    SKIN_RECTANGLE = 1,   
    SKIN_POLYGON = 2      
};

struct PhysicsSystem {
    int defaultvisualSkinType = SKIN_CIRCLE;
    float gravity = 9.81f;
    glm::vec2 gravityDir = glm::vec2(0.0f, -1.0f);
    float damping = 0.1f;
    float stiffness = 1.0f;
    float restitution = 0.8f;
    float coupling = 0.0f;
    glm::vec2 externalForce = glm::vec2(0.0f);
    float driveFreq = 1.0f;
    float driveAmp = 0.0f;
    bool showTrails = true;
    bool showPhaseSpace = true;
    bool simulationPaused = false;
    float globalTime = 0.0f;

    void UpdateSystemParameters();
    void ResetTime();
};

// Constants
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
extern int g_width;           // extern = defined elsewhere
extern int g_height;          // extern = defined elsewhere

// Texture paths
extern const char* TEXTURE_PATH1;
extern const char* TEXTURE_PATH2;
extern const char* TEXTURE_PATH_CIRCLE;
extern const char* TEXTURE_PATH_SPRING;
extern const char* TEXTURE_PATH_ROD;
extern const char* TEXTURE_PATH_PENDULUM_BOB;

// Physics constants
const float PHYSICS_DT = 1.0f;

// Global instances (declarations only - defined in globals.cpp)
extern Camera g_camera;
extern PhysicsSystem g_physics;

// Mouse input globals
extern bool g_mouseIsDown;
extern glm::vec2 g_lastMousePos;
extern glm::vec2 g_worldMousePos;
extern int g_draggedObjectIndex;
extern float g_dragForceStrength;

extern glm::vec2 g_simulationViewportPos;
extern glm::vec2 g_simulationViewportSize;

// Visual skin type names
extern const char* visualSkinTypeNames[];
extern const char* GetVisualSkinTypeName(int type);