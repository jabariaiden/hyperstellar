#pragma once
#include <glm/glm.hpp>
#include "common_definitions.h"

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