#include "globals.h"

// Define all global variables here (this is where they actually live in memory)
int g_width = SCR_WIDTH;
int g_height = SCR_HEIGHT;

// Texture paths (can keep these for future texture loading if needed)
const char* TEXTURE_PATH1 = "include/assets/Negatively_charged.png";
const char* TEXTURE_PATH2 = "include/assets/Positively_charged.png";
const char* TEXTURE_PATH_CIRCLE = "include/assets/circle_with_dots.png";
const char* TEXTURE_PATH_SPRING = "include/assets/spring_mass.png";
const char* TEXTURE_PATH_ROD = "include/assets/white_rectangle.png";
const char* TEXTURE_PATH_PENDULUM_BOB = "include/assets/pendulum_bob.png";

Camera g_camera;
PhysicsSystem g_physics;

bool g_mouseIsDown = false;
glm::vec2 g_lastMousePos = glm::vec2(0.0f);
glm::vec2 g_worldMousePos = glm::vec2(0.0f);
int g_draggedObjectIndex = -1;
float g_dragForceStrength = 100.0f;

glm::vec2 g_simulationViewportPos = glm::vec2(0.0f);
glm::vec2 g_simulationViewportSize = glm::vec2(640.0f, 480.0f);

// NEW: Visual skin type names
const char* visualSkinTypeNames[] = {
    "Circle", 
    "Rectangle", 
    "Polygon"
};

const char* GetVisualSkinTypeName(int type) {
    switch (type) {
        case SKIN_CIRCLE: return "Circle";
        case SKIN_RECTANGLE: return "Rectangle";
        case SKIN_POLYGON: return "Polygon";
        default: return "Unknown";
    }
}