#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// Forward declaration
struct GLFWwindow;

struct Camera {
    float zoom = 2.0f;
    glm::vec2 position = glm::vec2(0.0f, 0.0f);
    float moveSpeed = 5.0f;
    float zoomSpeed = 2.0f;

    glm::mat4 GetProjectionMatrix(float viewportWidth, float viewportHeight) const;
    glm::vec2 ScreenToWorld(glm::vec2 screenPos, glm::vec2 viewportSize) const;
    void ProcessInput(GLFWwindow* window, float deltaTime);
    void Reset();
};