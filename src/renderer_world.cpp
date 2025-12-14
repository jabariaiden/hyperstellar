#include "renderer.h"
#include "renderer_internals.h"
#include "globals.h"
#include "objects.h"
#include "axis.h"
#include "text_renderer.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream> 

static TextRenderer* textRenderer = nullptr;
static bool axisInitialized = false;

void Renderer::RenderWorldView() {
    // Initialize systems once
    if (textRenderer == nullptr) {
        try {
            textRenderer = new TextRenderer("C:/Users/user/jabariaiden-ProjStellar-main/bin/fonts/Roboto-VariableFont_wdth,wght.ttf", 24);
            std::cout << "TextRenderer initialized successfully" << std::endl;
        } catch (...) {
            std::cerr << "Failed to initialize TextRenderer" << std::endl;
        }
    }
    
    if (!axisInitialized) {
        Axis::Init();
        axisInitialized = true;
        
        // Configure Desmos-like grid style
        Axis::Style& style = Axis::GetStyle();
        style.majorGridColor = glm::vec3(0.4f, 0.4f, 0.6f);    // Blue-ish major lines
        style.minorGridColor = glm::vec3(0.25f, 0.25f, 0.35f); // Darker minor lines  
        style.subMinorGridColor = glm::vec3(0.15f, 0.15f, 0.25f); // Even darker sub-minor
        style.axisColor = glm::vec3(1.0f, 1.0f, 1.0f);        // White axes
        
        style.majorGridWidth = 1.5f;
        style.minorGridWidth = 1.0f;
        style.subMinorGridWidth = 0.5f;
        style.axisWidth = 2.0f;
        
        style.showMajorGrid = true;
        style.showMinorGrid = true;
        style.showSubMinorGrid = true;  // Show detailed grid
        style.smoothZoom = true;
        style.fadeLines = true;
        
        style.minorDivisions = 5.0f;
        style.subMinorDivisions = 5.0f;
        
        std::cout << "Axis system initialized with Desmos-style settings" << std::endl;
    }
    
    framebuffer->Bind();
    glViewport(0, 0, (int)g_simulationViewportSize.x, (int)g_simulationViewportSize.y);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Set up OpenGL state for high-quality line rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    
    glm::mat4 projectionWorld = g_camera.GetProjectionMatrix(
        g_simulationViewportSize.x, g_simulationViewportSize.y);
    glm::mat4 viewWorld = glm::translate(glm::mat4(1.0f), 
        glm::vec3(-g_camera.position, 0.0f));
    glm::mat4 projView = projectionWorld * viewWorld;
    
    // ----- Objects FIRST (behind grid) -----
    GLuint objectProgram = Objects::GetQuadProgram();
    glUseProgram(objectProgram);
    GLint projLoc = glGetUniformLocation(objectProgram, "uProjection");
    GLint viewLoc = glGetUniformLocation(objectProgram, "uView");
    if (projLoc != -1)
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projectionWorld));
    if (viewLoc != -1)
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(viewWorld));
    Objects::Draw(inputIndex);
    
    // ----- Axis / grid SECOND (on top of objects) -----
    Axis::Update(g_camera, g_simulationViewportSize.x, g_simulationViewportSize.y);
    
    // Validate axis shader before drawing
    GLint axisProgramLinked;
    glGetProgramiv(programAxis, GL_LINK_STATUS, &axisProgramLinked);
    if (axisProgramLinked == GL_TRUE) {
        glUseProgram(programAxis);
        Axis::Draw(programAxis, projView);
    } else {
        std::cerr << "Axis shader program not properly linked!" << std::endl;
    }
    
    // ----- Grid Labels LAST (on top of everything) -----
    if (textRenderer != nullptr) {
        Axis::DrawLabels(*textRenderer, g_camera, 
                         g_simulationViewportSize.x, g_simulationViewportSize.y);
    }
    
    framebuffer->Unbind();
    glViewport(0, 0, g_width, g_height);
    
    // Reset OpenGL state if needed
    glDisable(GL_LINE_SMOOTH);
}

// Cleanup function to call on application shutdown
void Renderer::CleanupRenderer() {
    if (axisInitialized) {
        Axis::Cleanup();
        axisInitialized = false;
        std::cout << "Axis system cleaned up" << std::endl;
    }
    
    if (textRenderer != nullptr) {
        delete textRenderer;
        textRenderer = nullptr;
        std::cout << "TextRenderer cleaned up" << std::endl;
    }
}