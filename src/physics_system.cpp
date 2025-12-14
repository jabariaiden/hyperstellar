#include "physics_system.h"
#include "objects.h"

void PhysicsSystem::UpdateSystemParameters() {
    Objects::SetSystemParameters(gravity, damping, stiffness);
}

void PhysicsSystem::ResetTime() {
    globalTime = 0.0f;
}