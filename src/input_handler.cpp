#include "input_handler.h"
#include "globals.h"          
#include "objects.h"
#include "camera.h"
#include "physics_system.h"
#include "common_definitions.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <vector>

namespace Renderer {
    int GetCurrentObjectBuffer();  // Forward declaration
}

void InputHandler::Initialize() {
    g_mouseIsDown = false;
    g_lastMousePos = glm::vec2(0.0f);
    g_worldMousePos = glm::vec2(0.0f);
    g_draggedObjectIndex = -1;
    g_dragForceStrength = 100.0f;
}

void InputHandler::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    g_width = width;
    g_height = height;
}

void InputHandler::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_mouseIsDown = true;

            std::vector<Object> currentCpuObjects;
            Objects::FetchToCPU(Renderer::GetCurrentObjectBuffer(), currentCpuObjects);

            g_draggedObjectIndex = -1;
            float min_dist_sq = 1e9;

            for (size_t i = 0; i < currentCpuObjects.size(); ++i) {
                glm::vec2 object_pos = currentCpuObjects[i].position;
                float dist_sq = glm::dot(object_pos - g_worldMousePos, 
                                        object_pos - g_worldMousePos);

                // Pick radius based on skin type
                float pick_radius = 0.2f; // Default

                if (currentCpuObjects[i].visualSkinType == SKIN_CIRCLE) {
                    // visualData.x = radius
                    pick_radius = currentCpuObjects[i].visualData.x * 1.2f;
                } 
                else if (currentCpuObjects[i].visualSkinType == SKIN_RECTANGLE) {
                    // visualData: (width, height, rotation, alpha)
                    float width = currentCpuObjects[i].visualData.x;
                    float height = currentCpuObjects[i].visualData.y;
                    pick_radius = glm::length(glm::vec2(width, height)) * 0.7f;
                }
                else if (currentCpuObjects[i].visualSkinType == SKIN_POLYGON) {
                    // visualData.x = radius
                    pick_radius = currentCpuObjects[i].visualData.x * 1.2f;
                }

                if (dist_sq < (pick_radius * pick_radius)) {
                    if (dist_sq < min_dist_sq) {
                        min_dist_sq = dist_sq;
                        g_draggedObjectIndex = i;
                    }
                }
            }

            if (g_draggedObjectIndex != -1) {
                std::cout << "[Drag] Started dragging object " 
                          << g_draggedObjectIndex << std::endl;
                g_physics.simulationPaused = true;
            }
        } 
        else if (action == GLFW_RELEASE) {
            g_mouseIsDown = false;
            g_draggedObjectIndex = -1;
        }
    }
}

void InputHandler::CursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }

    g_lastMousePos = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
}

void InputHandler::ProcessInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    HandleCameraInput(window, deltaTime);
    // HandleMouseDrag is called from main loop
}

void InputHandler::HandleCameraInput(GLFWwindow* window, float deltaTime) {
    g_camera.ProcessInput(window, deltaTime);
}

void InputHandler::HandleMouseDrag() {
    // This is now handled in the main loop
}