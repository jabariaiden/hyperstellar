#include "camera.h"
#include "globals.h"
#include <GLFW/glfw3.h>  // Add this include
#include <imgui.h>

glm::mat4 Camera::GetProjectionMatrix(float viewportWidth, float viewportHeight) const {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return glm::mat4(1.0f);
    }

    float aspect = viewportWidth / viewportHeight;
    float halfHeight = zoom;
    float halfWidth = halfHeight * aspect;

    return glm::ortho(
        position.x - halfWidth,
        position.x + halfWidth,
        position.y - halfHeight,
        position.y + halfHeight,
        -1.0f, 1.0f);
}

glm::vec2 Camera::ScreenToWorld(glm::vec2 screenPos, glm::vec2 viewportSize) const {
    if (viewportSize.x <= 0 || viewportSize.y <= 0) {
        return glm::vec2(0.0f);
    }

    glm::vec2 normalized = screenPos / viewportSize;
    glm::vec2 ndc = (normalized * 2.0f) - 1.0f;
    ndc.y = -ndc.y;

    float aspect = viewportSize.x / viewportSize.y;
    float halfHeight = zoom;
    float halfWidth = halfHeight * aspect;

    glm::vec2 worldPos;
    worldPos.x = position.x + ndc.x * halfWidth;
    worldPos.y = position.y + ndc.y * halfHeight;

    return worldPos;
}

void Camera::ProcessInput(GLFWwindow* window, float deltaTime) {
    // SAFETY: Check if ImGui is initialized before using it
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx != nullptr) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            return;
        }
    }

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        position.y += moveSpeed * zoom * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        position.y -= moveSpeed * zoom * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        position.x -= moveSpeed * zoom * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        position.x += moveSpeed * zoom * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        zoom *= 1.0f + (zoomSpeed * deltaTime);
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        zoom /= 1.0f + (zoomSpeed * deltaTime);

    zoom = std::clamp(zoom, 0.1f, 100.0f);
}
void Camera::Reset() {
    zoom = 2.0f;
    position = glm::vec2(0.0f, 0.0f);
}