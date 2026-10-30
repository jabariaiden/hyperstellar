#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// Forward declaration
struct GLFWwindow;

struct Camera
{
    float zoom = 2.0f;
    glm::vec2 position = glm::vec2(0.0f, 0.0f);
    float moveSpeed = 5.0f;
    float zoomSpeed = 2.0f;

    glm::mat4 GetProjectionMatrix(float viewportWidth, float viewportHeight) const;
    glm::vec2 ScreenToWorld(glm::vec2 screenPos, glm::vec2 viewportSize) const;
    void ProcessInput(GLFWwindow *window, float deltaTime);
    void Reset();
};


struct Camera3D
{
    glm::vec3 eye    = {0.0f, 0.0f, 20.0f};
    glm::vec3 target = {0.0f, 0.0f,  0.0f};
    glm::vec3 up     = {0.0f, 1.0f,  0.0f};

    bool use_ortho = true; // starts ortho so classic 2D demos look right
    float zoom = 20.0f;    // ortho half-height
    float fov = 50.0f;     // perspective only
    float near_z = 0.01f;
    float far_z = 5000.0f;

    glm::mat4 View() const;
    glm::mat4 Projection(int w, int h) const;
    glm::mat4 ViewProj(int w, int h) const;
    float ApparentHalfHeight() const;

    void LookAt(glm::vec3 e, glm::vec3 t);
    void LookAt(glm::vec3 e, glm::vec3 t, glm::vec3 u);
    void Orbit(float dyaw, float dpitch);
    void Pan(float dx, float dy);
    void Dolly(float delta);
    void Reset();
};

extern Camera g_camera;     // untouched — other systems keep using this
extern Camera3D g_camera3d; // new render camera for this system