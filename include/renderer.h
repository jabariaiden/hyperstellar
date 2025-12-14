#pragma once
#include <glm/glm.hpp>

class Renderer {
public:
    static bool Initialize();
    static void Shutdown();
    static void RenderFrame();
    static void UpdateViewport();
    static void CleanupRenderer();
    
    static void UpdatePhysics(float deltaTime, float fakeDeltaTime);
    static void SwapBuffers();
    
    static bool IsSimulationPaused();
    static void SetSimulationPaused(bool paused);
    
    static unsigned int GetMainFramebufferTexture();
    static unsigned int GetPhaseSpaceFramebufferTexture();
    __declspec(dllexport) static int GetCurrentObjectBuffer();

    
    
private:
    static void RenderWorldView();
    static void RenderPhaseSpace();
    static void SetupTextures();
    static void CleanupTextures();
};
