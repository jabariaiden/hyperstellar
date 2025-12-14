#include "renderer.h"
#include "renderer_internals.h"
#include "globals.h"
#include "objects.h"
#include "vectorfield.h"
#include "axis.h"
#include "framebuffer.h"
#include "shader_utils.h"
#include "utils.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// Define all shared variables here
GLuint programField = 0;
GLuint programAxis = 0;
GLuint programQuad = 0;
GLuint programUpdate = 0;
Framebuffer::Framebuffer* framebuffer = nullptr;
Framebuffer::Framebuffer* framebuffer2 = nullptr;
GLuint texNeg = 0;
GLuint texPos = 0;
GLuint texCircle = 0;
GLuint texSpring = 0;
GLuint texRod = 0;
GLuint texPendulumBob = 0;
int inputIndex = 0;
int outputIndex = 1;

bool Renderer::Initialize() {
    programField = CreateProgram(
        ReadTextFile("shaders/vertexShader.vert").c_str(),
        ReadTextFile("shaders/geoShader.geom").c_str(),
        ReadTextFile("shaders/fragShader.frag").c_str());
        
    programAxis = CreateProgram(
        ReadTextFile("shaders/axis.vert").c_str(),
        nullptr,
        ReadTextFile("shaders/axis.frag").c_str());

    if (!programField || !programAxis) {
        std::cerr << "Failed to create shader programs\n";
        return false;
    }

    framebuffer = new Framebuffer::Framebuffer(SCR_WIDTH, SCR_HEIGHT);
    framebuffer2 = new Framebuffer::Framebuffer(SCR_WIDTH, SCR_HEIGHT);

    VectorField::Generate(20.0f, 0.1f);
    VectorField::CreateGL();
    Axis::Init();

    if (!Objects::Init()) {
        std::cerr << "Objects init failed\n";
        return false;
    }

    programQuad = Objects::GetQuadProgram();
    programUpdate = Objects::GetComputeProgram();

    Renderer::SetupTextures();

    Objects::SetDefaultObjectType(g_physics.defaultvisualSkinType);
    Objects::SetSystemParameters(g_physics.gravity, g_physics.damping, g_physics.stiffness);

    return true;
}

void Renderer::Shutdown() {
    Renderer::CleanupTextures();

    delete framebuffer;
    delete framebuffer2;

    if (programField) glDeleteProgram(programField);
    if (programAxis) glDeleteProgram(programAxis);
}