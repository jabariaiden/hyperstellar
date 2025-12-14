#pragma once
#include <glm/glm.hpp>

// Forward declaration
struct GLFWwindow;

class InputHandler {
public:
    static void Initialize();
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void ProcessInput(GLFWwindow* window, float deltaTime);
    
private:
    static void HandleMouseDrag();
    static void HandleCameraInput(GLFWwindow* window, float deltaTime);
};