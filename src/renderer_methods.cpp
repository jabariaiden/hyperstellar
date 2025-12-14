#include "renderer.h"
#include "renderer_internals.h"
#include "globals.h"

void Renderer::RenderFrame() {
    UpdateViewport();

    if (g_physics.showPhaseSpace) {
        RenderPhaseSpace();
    }

    RenderWorldView();
}

void Renderer::UpdateViewport() {
    glm::vec2 relativeMousePos = g_lastMousePos - g_simulationViewportPos;
    g_worldMousePos = g_camera.ScreenToWorld(relativeMousePos, g_simulationViewportSize);
}

void Renderer::SwapBuffers() {
    std::swap(inputIndex, outputIndex);
}

bool Renderer::IsSimulationPaused() {
    return g_physics.simulationPaused;
}

void Renderer::SetSimulationPaused(bool paused) {
    g_physics.simulationPaused = paused;
}

unsigned int Renderer::GetMainFramebufferTexture() {
    return framebuffer ? framebuffer->GetTextureId() : 0;
}

unsigned int Renderer::GetPhaseSpaceFramebufferTexture() {
    return framebuffer2 ? framebuffer2->GetTextureId() : 0;
}
int Renderer::GetCurrentObjectBuffer() {
    return outputIndex;  // The buffer we just wrote to
}