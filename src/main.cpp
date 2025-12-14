#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <sstream>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "utils.h"
#include "shader_utils.h"
#include "vectorfield.h"
#include "axis.h"
#include "objects.h"
#include "framebuffer.h"
#include "parser.h"
#include "debug_helpers.h"
#include "async_shader_loader.h"
#include "globals.h"
#include "camera.h"
#include "physics_system.h"
#include "input_handler.h"
#include "ui_manager.h"
#include "renderer.h"
#include "imgui_styles.h"

int main()
{
    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Classical Physics Simulator", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, InputHandler::FramebufferSizeCallback);
    glfwSetMouseButtonCallback(window, InputHandler::MouseButtonCallback);
    glfwSetCursorPosCallback(window, InputHandler::CursorPositionCallback);

    if (!gladLoaderLoadGL())
    {
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // In your initialization, check GPU capabilities:
    GLint maxSSBOsize, maxComputeWorkGroups, maxShaderStorageBlocks;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSSBOsize);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeWorkGroups);
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &maxShaderStorageBlocks);

    std::cout << "[GPU Limits] Max SSBO Size: " << maxSSBOsize / 1024 / 1024 << " MB" << std::endl;
    std::cout << "[GPU Limits] Max Compute Work Groups: " << maxComputeWorkGroups << std::endl;
    std::cout << "[GPU Limits] Max Shader Storage Blocks: " << maxShaderStorageBlocks << std::endl;

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    
    // FIX: Ensure window backgrounds are opaque
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;    // Make sure alpha is 1.0 (fully opaque)
    style.Colors[ImGuiCol_ChildBg].w = 1.0f;     // Child windows also opaque
    style.Colors[ImGuiCol_PopupBg].w = 1.0f;     // Popups opaque
    
    ImGuiStyles::SetupModernStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");

    // Initialize systems
    InputHandler::Initialize();
    UIManager::Initialize();

    if (!Renderer::Initialize())
    {
        std::cerr << "Renderer initialization failed\n";
        return -1;
    }

    double lastFrameTime = glfwGetTime();
    int frameCounter = 0;

    // Equation state (moved from global static in original)
    static std::vector<std::string> presetEquations = {
        "vx, vy, -k*x/m - g*uGravityDir.x + uExternalForce.x, -k*y/m - g*uGravityDir.y + uExternalForce.y",
        "vx, vy, 0, 0",
        "vx, vy, -k*x/m - b*vx/m, -k*y/m - b*vy/m",
        "vx, vy, -k*x/m, -k*y/m",
        "vx, vy, uExternalForce.x, uExternalForce.y"};
    // static int selectedPreset = 0;
    static std::string userEquation = presetEquations[0];
    static bool equationUpdated = true;
    static bool objectsDataUpdated = false;

    while (!glfwWindowShouldClose(window))
    {
        // FIX 1: Clear the main OpenGL framebuffer FIRST
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);  // Dark background
        glClear(GL_COLOR_BUFFER_BIT);

        // In your main render loop, add:
        Objects::UpdateShaderLoadingStatus();

        if (!Objects::IsComputeShaderReady())
        {
            // Show loading screen
            float progress = Objects::GetShaderLoadProgress();
            std::string status = Objects::GetShaderLoadStatusMessage();
            std::cout << "\r[Loading] " << status << " " << (int)(progress * 100) << "%" << std::flush;
        }
        else
        {
            static bool firstReadyLog = true;
            if (firstReadyLog)
            {
                std::cout << "\n[Loading] ✅ Compute shader is ready!" << std::endl;
                firstReadyLog = false;
            }
        }

        double currentFrameTime = glfwGetTime();
        float fakeDeltaTime = static_cast<float>((currentFrameTime - lastFrameTime) * PHYSICS_DT);
        float deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

        if (!Renderer::IsSimulationPaused())
        {
            g_physics.globalTime += deltaTime;
        }

        InputHandler::ProcessInput(window, deltaTime);

        // Handle space bar pause
        static bool spaceWasPressed = false;
        bool spaceIsPressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceIsPressed && !spaceWasPressed && !io.WantCaptureKeyboard)
        {
            Renderer::SetSimulationPaused(!Renderer::IsSimulationPaused());
        }
        spaceWasPressed = spaceIsPressed;

        // Handle equation updates
        if (equationUpdated)
        {
            // Equation update logic here
            equationUpdated = false;
        }

        // Handle mouse dragging
        if (g_mouseIsDown && g_draggedObjectIndex != -1)
        {
            // Mouse drag logic here
            objectsDataUpdated = true;
        }

        // Update physics
        Renderer::UpdatePhysics(deltaTime, fakeDeltaTime);

        // FIX 2: Render graphics BEFORE ImGui
        Renderer::RenderFrame();

        // Then render UI on top
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        UIManager::RenderFileDialogs();

        UIManager::RenderMainUI();
        UIManager::RenderControlPanel();
        UIManager::RenderSimulationView();

        if (g_physics.showPhaseSpace)
        {
            UIManager::RenderPhaseSpaceView();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!Renderer::IsSimulationPaused())
        {
            Renderer::SwapBuffers();
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
        frameCounter++;

        static int debugFrame = 0;
        if (debugFrame % 60 == 0)
        { // Every second
            std::cout << "=== FRAME " << debugFrame << " ===" << std::endl;
            std::cout << "Paused: " << Renderer::IsSimulationPaused() << std::endl;
            std::cout << "Object count: " << Objects::GetNumObjects() << std::endl;

            // Read back object data to see if it's changing
            std::vector<Object> debugObjects;
            Objects::FetchToCPU(0, debugObjects);
            for (size_t i = 0; i < std::min(debugObjects.size(), size_t(5)); ++i)
            {
                std::cout << "Object " << i << " position: (" << debugObjects[i].position.x
                          << ", " << debugObjects[i].position.y << ")" << std::endl;
            }
            std::cout << "\n DIAGNOSTIC POINT 4: During Simulation (frame " << debugFrame << ")\n";
            Objects::RunFullDiagnostic();
        }
        debugFrame++;
    }
    Renderer::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    Objects::Cleanup();
    VectorField::Cleanup();
    Axis::Cleanup();

    glfwTerminate();
    return 0;
}