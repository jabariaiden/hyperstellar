#pragma once
#include <glm/glm.hpp>
#include "camera.h"           // Include full definitions
#include "physics_system.h"   // Include full definitions
#include "common_definitions.h"

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