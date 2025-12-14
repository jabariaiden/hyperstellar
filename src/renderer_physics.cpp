#include "renderer.h"
#include "renderer_internals.h"
#include "globals.h"
#include "objects.h"
#include <glad/glad.h>

void Renderer::UpdatePhysics(float deltaTime, float fakeDeltaTime) {
    if (!g_physics.simulationPaused && Objects::IsComputeShaderReady()) {
        // Get the compute program directly each time (don't rely on cached programUpdate)
        GLuint computeProgram = Objects::GetComputeProgram();
        
        // Set all uniforms on the ACTUAL compute shader
        glUseProgram(computeProgram);
        
        // Set all physics uniforms
        glUniform1f(glGetUniformLocation(computeProgram, "uDt"), fakeDeltaTime);
        glUniform1f(glGetUniformLocation(computeProgram, "uTime"), g_physics.globalTime);
        glUniform1f(glGetUniformLocation(computeProgram, "k"), g_physics.stiffness);
        glUniform1f(glGetUniformLocation(computeProgram, "b"), g_physics.damping);
        glUniform1f(glGetUniformLocation(computeProgram, "g"), g_physics.gravity);
        glUniform2f(glGetUniformLocation(computeProgram, "uGravityDir"), g_physics.gravityDir.x, g_physics.gravityDir.y);
        glUniform1f(glGetUniformLocation(computeProgram, "uRestitution"), g_physics.restitution);
        glUniform1f(glGetUniformLocation(computeProgram, "uCoupling"), g_physics.coupling);
        glUniform2f(glGetUniformLocation(computeProgram, "uExternalForce"), g_physics.externalForce.x, g_physics.externalForce.y);
        glUniform1f(glGetUniformLocation(computeProgram, "uDriveFreq"), g_physics.driveFreq);
        glUniform1f(glGetUniformLocation(computeProgram, "uDriveAmp"), g_physics.driveAmp);
        glUniform1i(glGetUniformLocation(computeProgram, "uEquationMode"), 0);
        glUniform1i(glGetUniformLocation(computeProgram, "uNumObjects"), Objects::GetNumObjects());

        // Now call Objects::Update - it will use the same compute program
        Objects::Update(inputIndex, outputIndex);
        Objects::DebugCheckComputeExecution();
        
        glUseProgram(0);
    }
}