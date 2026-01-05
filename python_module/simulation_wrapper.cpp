#include "simulation_wrapper.h"
#include "../include/objects.h"
#include "../include/parser.h"
#include "../include/constraints.h"
#include "../include/async_shader_loader.h"
#include "../include/axis.h"
#include "../include/camera.h"
#include "../include/globals.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <sstream>
#include <utility> 

namespace
{
    bool g_axisInitialized = false;   // Tracks axis system initialization
    GLuint g_axisShaderProgram = 0;   // Shader program for rendering axes/grid
}

// Create shader program for rendering coordinate axes and grid
GLuint CreateAxisShader()
{
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec3 aColor;
        layout (location = 2) in float aWidth;
        
        uniform mat4 uProjView;
        
        out vec3 Color;
        
        void main() {
            gl_Position = uProjView * vec4(aPos, 0.0, 1.0);
            Color = aColor;
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec3 Color;
        out vec4 FragColor;
        
        void main() {
            FragColor = vec4(Color, 1.0);
        }
    )";

    // Compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "Axis Vertex Shader Compilation Failed: " << infoLog << std::endl;
        return 0;
    }

    // Compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "Axis Fragment Shader Compilation Failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        return 0;
    }

    // Link shader program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "Axis Shader Program Linking Failed: " << infoLog << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    // Clean up shader objects
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

// Constructor: Initialize simulation with optional graphics
SimulationWrapper::SimulationWrapper(bool headless, int width, int height, std::string title, bool enable_grid)
    : m_headless(headless), m_initialized(false), m_paused(false),
    m_window(nullptr), m_currentBuffer(0), m_title(title),
    m_width(width), m_height(height), m_simulationTime(0.0f),
    m_enable_grid(enable_grid)
{
    try
    {
        // Initialize global settings
        g_width = width;
        g_height = height;
        g_simulationViewportSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));

        // Choose initialization mode
        if (headless)
        {
            if (!init_headless())
                throw std::runtime_error("Failed to initialize headless OpenGL context");
        }
        else
        {
            if (!init_windowed(width, height, title))
                throw std::runtime_error("Failed to initialize windowed context");
        }
    }
    catch (const std::exception& e)
    {
        // Clean up on failure
        cleanup();
        throw;
    }
}

// Destructor: Clean up resources
SimulationWrapper::~SimulationWrapper()
{
    cleanup();
}

// Initialize headless mode (no window, for batch processing)
bool SimulationWrapper::init_headless()
{
    // Initialize GLFW
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Configure OpenGL context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    // Create hidden window
    m_window = glfwCreateWindow(640, 480, "Headless", nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create headless window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    // Load OpenGL functions
    if (!gladLoaderLoadGL())
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    glFlush();
    glFinish();

    // Clear any OpenGL errors
    while (glGetError() != GL_NO_ERROR);

    // Verify OpenGL context
    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR || major == 0)
    {
        std::cerr << "OpenGL context verification failed (error: " << err << ")" << std::endl;
        return false;
    }

    // Initialize object system
    if (!Objects::Init(m_window))
    {
        std::cerr << "Failed to initialize object system" << std::endl;
        return false;
    }

    m_initialized = true;
    return true;
}

// Window resize callback
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    SimulationWrapper* sim = static_cast<SimulationWrapper*>(glfwGetWindowUserPointer(window));
    if (sim)
    {
        // Update global viewport settings
        g_width = width;
        g_height = height;
        g_simulationViewportSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
        glViewport(0, 0, width, height);
    }
}

// Initialize windowed mode (with graphics)
bool SimulationWrapper::init_windowed(int width, int height, const std::string& title)
{
    // Initialize GLFW
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Configure OpenGL context
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create window
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Set up window context and callbacks
    glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));
    glfwSetWindowUserPointer(static_cast<GLFWwindow*>(m_window), this);
    glfwSetFramebufferSizeCallback(static_cast<GLFWwindow*>(m_window), framebuffer_size_callback);
    glfwSwapInterval(1);  // Enable vsync

    // Show and focus window
    glfwShowWindow(static_cast<GLFWwindow*>(m_window));
    glfwFocusWindow(static_cast<GLFWwindow*>(m_window));

    // Process initial events
    for (int i = 0; i < 5; i++)
        glfwPollEvents();

    // Load OpenGL functions
    if (!gladLoaderLoadGL())
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    glFlush();
    glFinish();

    // Clear any OpenGL errors
    while (glGetError() != GL_NO_ERROR);

    // Verify OpenGL context
    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR || major == 0)
    {
        std::cerr << "OpenGL context verification failed (error: " << err << ")" << std::endl;
        return false;
    }

    // Reset camera to default state
    g_camera.Reset();

    // Initialize axis/grid system if enabled
    if (m_enable_grid)
    {
        if (!g_axisInitialized)
        {
            Axis::Init();
            g_axisInitialized = true;

            // Configure axis appearance
            Axis::Style& style = Axis::GetStyle();
            style.majorGridColor = glm::vec3(0.4f, 0.4f, 0.6f);
            style.minorGridColor = glm::vec3(0.25f, 0.25f, 0.35f);
            style.subMinorGridColor = glm::vec3(0.15f, 0.15f, 0.25f);
            style.axisColor = glm::vec3(1.0f, 1.0f, 1.0f);

            style.majorGridWidth = 1.5f;
            style.minorGridWidth = 1.0f;
            style.subMinorGridWidth = 0.5f;
            style.axisWidth = 2.0f;

            style.showMajorGrid = true;
            style.showMinorGrid = true;
            style.showSubMinorGrid = false;
            style.smoothZoom = false;
            style.fadeLines = false;

            style.minorDivisions = 5.0f;
            style.subMinorDivisions = 5.0f;
        }

        // Create axis shader
        g_axisShaderProgram = CreateAxisShader();
        if (g_axisShaderProgram == 0)
        {
            std::cerr << "Failed to create axis shader!" << std::endl;
            return false;
        }
    }

    // Initialize object system
    if (!Objects::Init(m_window))
    {
        std::cerr << "Failed to initialize object system" << std::endl;
        return false;
    }

    m_initialized = true;
    return true;
}

// Throw exception if simulation is not initialized
void SimulationWrapper::ensure_initialized() const
{
    if (!m_initialized)
        throw std::runtime_error("Simulation not initialized");
}

// Process user input (keyboard, mouse)
void SimulationWrapper::process_input()
{
    if (!m_headless && m_window)
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window);
        glfwMakeContextCurrent(glfwWindow);

        // Handle ESC key to close window
        static bool escWasPressed = false;
        bool escIsPressed = (glfwGetKey(glfwWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS);

        if (escIsPressed && !escWasPressed)
            glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
        escWasPressed = escIsPressed;

        // Process camera controls
        g_camera.ProcessInput(glfwWindow, 0.016f);
    }
}

// Check if window should close
bool SimulationWrapper::should_close() const
{
    if (!m_headless && m_window)
        return glfwWindowShouldClose(static_cast<GLFWwindow*>(m_window));
    return false;
}

// ============================================================================
// NEW: COLLISION PARAMETER MANAGEMENT
// ============================================================================

void SimulationWrapper::set_collision_parameters(bool enable_warm_start, int max_contact_iterations)
{
    ensure_initialized();

    if (max_contact_iterations < 1 || max_contact_iterations > 20)
        throw std::runtime_error("max_contact_iterations must be between 1 and 20");

    Objects::SetCollisionParameters(enable_warm_start, max_contact_iterations);
}

std::pair<bool, int> SimulationWrapper::get_collision_parameters() const
{
    ensure_initialized();

    bool enable_warm_start;
    int max_contact_iterations;
    Objects::GetCollisionParameters(enable_warm_start, max_contact_iterations);

    return std::make_pair(enable_warm_start, max_contact_iterations);
}

// ============================================================================
// Update simulation physics
// ============================================================================
void SimulationWrapper::update(float dt)
{
    ensure_initialized();
    if (m_paused) return;

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    const float FIXED_STEP = 0.001f;
    static float accumulator = 0.0f;
    accumulator += dt;

    int stepCount = 0;
    const int MAX_STEPS_PER_FRAME = 20;

    while (accumulator >= FIXED_STEP && stepCount < MAX_STEPS_PER_FRAME)
    {
        m_simulationTime += FIXED_STEP;

        while (glGetError() != GL_NO_ERROR);

        GLuint computeProgram = Objects::GetComputeProgram();
        if (computeProgram && Objects::IsComputeShaderReady())
        {
            glUseProgram(computeProgram);

            // Fetch uniform locations
            GLint dtLoc = glGetUniformLocation(computeProgram, "uDt");
            GLint timeLoc = glGetUniformLocation(computeProgram, "uTime");
            GLint kLoc = glGetUniformLocation(computeProgram, "k");
            GLint bLoc = glGetUniformLocation(computeProgram, "b");
            GLint gLoc = glGetUniformLocation(computeProgram, "g");
            GLint gravityDirLoc = glGetUniformLocation(computeProgram, "uGravityDir");
            GLint restitutionLoc = glGetUniformLocation(computeProgram, "uRestitution");
            GLint couplingLoc = glGetUniformLocation(computeProgram, "uCoupling");
            GLint externalForceLoc = glGetUniformLocation(computeProgram, "uExternalForce");
            GLint driveFreqLoc = glGetUniformLocation(computeProgram, "uDriveFreq");
            GLint driveAmpLoc = glGetUniformLocation(computeProgram, "uDriveAmp");
            GLint equationModeLoc = glGetUniformLocation(computeProgram, "uEquationMode");
            GLint numObjectsLoc = glGetUniformLocation(computeProgram, "uNumObjects");

            // NEW: Get collision parameter uniforms
            GLint enableWarmStartLoc = glGetUniformLocation(computeProgram, "uEnableWarmStart");
            GLint maxContactIterationsLoc = glGetUniformLocation(computeProgram, "uMaxContactIterations");

            // Apply Uniforms
            if (dtLoc != -1) glUniform1f(dtLoc, FIXED_STEP);
            if (timeLoc != -1) glUniform1f(timeLoc, m_simulationTime);
            if (kLoc != -1) glUniform1f(kLoc, 1.0f);
            if (bLoc != -1) glUniform1f(bLoc, 0.1f);
            if (gLoc != -1) glUniform1f(gLoc, 9.81f);
            if (gravityDirLoc != -1) glUniform2f(gravityDirLoc, 0.0f, -1.0f);
            if (restitutionLoc != -1) glUniform1f(restitutionLoc, 0.7f);
            if (couplingLoc != -1) glUniform1f(couplingLoc, 1.0f);
            if (externalForceLoc != -1) glUniform2f(externalForceLoc, 0.0f, 0.0f);
            if (driveFreqLoc != -1) glUniform1f(driveFreqLoc, 1.0f);
            if (driveAmpLoc != -1) glUniform1f(driveAmpLoc, 0.0f);
            if (equationModeLoc != -1) glUniform1i(equationModeLoc, 0);
            if (numObjectsLoc != -1) glUniform1i(numObjectsLoc, Objects::GetNumObjects());

            // NEW: Apply collision parameters
            auto collision_params = get_collision_parameters();
            if (enableWarmStartLoc != -1) glUniform1i(enableWarmStartLoc, collision_params.first ? 1 : 0);
            if (maxContactIterationsLoc != -1) glUniform1i(maxContactIterationsLoc, collision_params.second);

            // Run Compute Shader
            Objects::Update(m_currentBuffer, 1 - m_currentBuffer);
            m_currentBuffer = 1 - m_currentBuffer;

            glUseProgram(0);
        }

        accumulator -= FIXED_STEP;
        stepCount++;
    }

    // Error checking
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        std::cerr << "OpenGL error in sub-stepping loop: " << err << std::endl;
}

// ============================================================================
// Render simulation to screen
// ============================================================================
void SimulationWrapper::render()
{
    if (m_headless || !m_window) return;

    glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    int w, h;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_window), &w, &h);

    if (w <= 0 || h <= 0) return;

    // Set up viewport and clear screen
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Calculate camera matrices
    glm::mat4 projection = g_camera.GetProjectionMatrix(w, h);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-g_camera.position, 0.0f));
    glm::mat4 projView = projection * view;

    while (glGetError() != GL_NO_ERROR);

    // Only render axis if grid is enabled
    if (m_enable_grid && g_axisInitialized && g_axisShaderProgram)
    {
        glUseProgram(g_axisShaderProgram);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) return;

        Axis::Update(g_camera, w, h);
        Axis::Draw(g_axisShaderProgram, projView);
        glUseProgram(0);
    }

    // Render physics objects
    GLuint objectProgram = Objects::GetQuadProgram();
    if (objectProgram && Objects::IsQuadShaderReady())
    {
        glUseProgram(objectProgram);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
        {
            glUseProgram(0);
            return;
        }

        // Pass camera matrices to shader
        GLint projLoc = glGetUniformLocation(objectProgram, "uProjection");
        GLint viewLoc = glGetUniformLocation(objectProgram, "uView");

        if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

        Objects::Draw(m_currentBuffer);
        glUseProgram(0);
    }

    // Swap buffers and poll events
    glfwSwapBuffers(static_cast<GLFWwindow*>(m_window));
    glfwPollEvents();
}

// ============================================================================
// COLLISION MANAGEMENT FUNCTIONS
// ============================================================================

// Enable or disable collisions for a specific object
void SimulationWrapper::set_collision_enabled(int index, bool enabled)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    Objects::SetCollisionEnabled(index, enabled);
}

// Set collision shape for an object
void SimulationWrapper::set_collision_shape(int index, PyCollisionShape shape)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    // Convert Python shape enum to C++ shape type
    CollisionShape cppShape;
    switch (shape)
    {
    case PyCollisionShape::NONE:
        cppShape = static_cast<CollisionShape>(0);  // COLLISION_NONE
        break;
    case PyCollisionShape::CIRCLE:
        cppShape = static_cast<CollisionShape>(1);  // COLLISION_CIRCLE
        break;
    case PyCollisionShape::AABB:
        cppShape = static_cast<CollisionShape>(2);  // COLLISION_AABB
        break;
    case PyCollisionShape::POLYGON:
        cppShape = static_cast<CollisionShape>(3);  // COLLISION_POLYGON
        break;
    default:
        cppShape = static_cast<CollisionShape>(0);  // COLLISION_NONE
    }

    Objects::SetCollisionShape(index, cppShape);
}

// Set collision material properties (restitution and friction)
void SimulationWrapper::set_collision_properties(int index, float restitution, float friction)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    // Validate input ranges
    if (restitution < 0.0f || restitution > 1.0f)
        throw std::runtime_error("Restitution must be between 0.0 and 1.0");

    if (friction < 0.0f || friction > 1.0f)
        throw std::runtime_error("Friction must be between 0.0 and 1.0");

    Objects::SetCollisionProperties(index, restitution, friction);
}

// Get current collision configuration for an object
CollisionConfig SimulationWrapper::get_collision_config(int index)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    // Retrieve collision properties from object system
    CollisionProperties props = Objects::GetCollisionProperties(index);

    // Convert to Python-friendly configuration
    CollisionConfig config;
    config.enabled = (props.enabled == 1);
    config.restitution = props.restitution;
    config.friction = props.friction;

    // Convert shape type to Python enum
    switch (props.shapeType)
    {
    case COLLISION_NONE:
        config.shape = PyCollisionShape::NONE;
        break;
    case COLLISION_CIRCLE:
        config.shape = PyCollisionShape::CIRCLE;
        break;
    case COLLISION_AABB:
        config.shape = PyCollisionShape::AABB;
        break;
    case COLLISION_POLYGON:
        config.shape = PyCollisionShape::POLYGON;
        break;
    default:
        config.shape = PyCollisionShape::NONE;
    }

    return config;
}

// Enable or disable collisions between two specific objects
void SimulationWrapper::enable_collision_between(int obj1, int obj2, bool enable)
{
    ensure_initialized();

    if (obj1 < 0 || obj1 >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object1 index");

    if (obj2 < 0 || obj2 >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object2 index");

    Objects::EnableCollisionBetween(obj1, obj2, enable);
}

// Check if collisions are enabled for an object
bool SimulationWrapper::is_collision_enabled(int index)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    return Objects::IsCollisionEnabled(index);
}

// ============================================================================
// OBJECT MANAGEMENT FUNCTIONS
// ============================================================================

// Add a new physics object to the simulation
int SimulationWrapper::add_object(
    float x, float y, float vx, float vy,
    float mass, float charge,
    float rotation, float angular_velocity,
    PySkinType skin,
    float size,
    float width, float height,
    float r, float g, float b, float a,
    int polygon_sides)
{
    ensure_initialized();

    if (Objects::GetNumObjects() >= Objects::MAX_OBJECTS)
        throw std::runtime_error("Maximum object limit reached");

    // Step 1: Create the object with CORRECT values in local memory FIRST
    Object newObject;
    newObject.position = glm::vec2(x, y);
    newObject.velocity = glm::vec2(vx, vy);
    newObject.mass = mass;
    newObject.charge = charge;
    newObject.visualSkinType = static_cast<int>(skin);
    newObject.collisionShapeType = 0; // COLLISION_NONE (will be auto-assigned)
    newObject.equationID = 0; // Default equation
    newObject._pad1 = 0;
    newObject.color = glm::vec4(r, g, b, a);
    newObject.collisionData = glm::vec4(0.0f);

    // Set visualData based on skin type - WITH INTELLIGENT DEFAULTS
    switch (skin)
    {
    case PySkinType::PY_SKIN_CIRCLE:
        // For circles: size = radius, ignore width/height
        newObject.visualData = glm::vec4(size, static_cast<float>(polygon_sides), rotation, angular_velocity);
        newObject.color = glm::vec4(r, g, b, a);
        break;
    case PySkinType::PY_SKIN_RECTANGLE:
        // For rectangles: use width/height directly, ignore size
        newObject.visualData = glm::vec4(width, height, rotation, angular_velocity);
        newObject.color = glm::vec4(r, g, b, a);
        break;
    case PySkinType::PY_SKIN_POLYGON:
        // Clamp polygon sides to valid range
        int minPolySides = 3;
        int maxPolySides = 12;
        polygon_sides = (polygon_sides < minPolySides) ? minPolySides :
            ((polygon_sides > maxPolySides) ? maxPolySides : polygon_sides);
        // For polygons: size = radius, polygon_sides = number of sides, ignore width/height
        newObject.visualData = glm::vec4(size, static_cast<float>(polygon_sides), rotation, angular_velocity);
        newObject.color = glm::vec4(r, g, b, a);
        break;
    }

    // Step 2: Allocate slot in Objects system
    Objects::AddObject();
    int objectID = Objects::GetNumObjects() - 1;

    // Step 3: IMMEDIATELY overwrite with correct values
    Objects::UpdateObjectCPU(objectID, newObject);

    // Step 4: Auto-assign collision shape based on visual skin
    PyCollisionShape collisionShape;
    switch (skin)
    {
    case PySkinType::PY_SKIN_CIRCLE:
        collisionShape = PyCollisionShape::CIRCLE;
        break;
    case PySkinType::PY_SKIN_RECTANGLE:
        collisionShape = PyCollisionShape::AABB;
        break;
    case PySkinType::PY_SKIN_POLYGON:
        collisionShape = PyCollisionShape::POLYGON;
        break;
    default:
        collisionShape = PyCollisionShape::NONE;
    }

    // Apply collision configuration
    set_collision_shape(objectID, collisionShape);
    set_collision_enabled(objectID, true);
    set_collision_properties(objectID, 0.7f, 0.3f); // Default values

    // Step 5: CRITICAL - Force GPU to finish all pending operations
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFlush();

    return objectID;
}

// Update properties of an existing object
void SimulationWrapper::update_object(
    int index,
    float x, float y,
    float vx, float vy,
    float mass, float charge,
    float rotation, float angular_velocity,
    float width, float height,
    float r, float g, float b, float a)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
    {
        Object& p = objects[index];
        int skinType = p.visualSkinType;

        // Update basic properties
        p.position = glm::vec2(x, y);
        p.velocity = glm::vec2(vx, vy);
        p.mass = mass;
        p.charge = charge;
        p.color = glm::vec4(r, g, b, a);

        // Update visualData based on current skin type
        if (skinType == 0)  // CIRCLE
        {
            // For circles: width parameter is radius
            p.visualData.x = width;  // radius
            p.visualData.z = rotation;
            p.visualData.w = angular_velocity;
        }
        else if (skinType == 1)  // RECTANGLE
        {
            p.visualData.x = width;
            p.visualData.y = height;
            p.visualData.z = rotation;
            p.visualData.w = angular_velocity;
        }
        else if (skinType == 2)  // POLYGON
        {
            // For polygons: width parameter is radius, preserve sides
            p.visualData.x = width;  // radius
            // Keep existing polygon sides (visualData.y)
            p.visualData.z = rotation;
            p.visualData.w = angular_velocity;
        }

        Objects::UpdateObjectCPU(index, p);
    }
}

// Remove an object from the simulation
void SimulationWrapper::remove_object(int index)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    Objects::RemoveObject(index);
}

// Get current number of objects in simulation
int SimulationWrapper::object_count() const
{
    ensure_initialized();
    return Objects::GetNumObjects();
}

// Get complete state of a specific object
ObjectState SimulationWrapper::get_object(int index) const
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index >= static_cast<int>(objects.size()))
        throw std::runtime_error("Object data corrupted");

    const Object& p = objects[index];
    ObjectState state;

    // Extract object properties
    state.x = p.position.x;
    state.y = p.position.y;
    state.vx = p.velocity.x;
    state.vy = p.velocity.y;
    state.mass = p.mass;
    state.charge = p.charge;
    state.rotation = p.visualData.z;
    state.angular_velocity = p.visualData.w;
    state.skin_type = p.visualSkinType;

    // Extract visual properties based on skin type
    if (p.visualSkinType == 0)  // CIRCLE
    {
        state.radius = p.visualData.x;
        state.width = p.visualData.x * 2.0f;  // diameter
        state.height = p.visualData.x * 2.0f; // diameter
        state.polygon_sides = 0;
    }
    else if (p.visualSkinType == 1)  // RECTANGLE
    {
        state.width = p.visualData.x;
        state.height = p.visualData.y;
        state.radius = 0.0f;
        state.polygon_sides = 0;
    }
    else if (p.visualSkinType == 2)  // POLYGON
    {
        state.radius = p.visualData.x;
        state.width = p.visualData.x * 2.0f;  // diameter
        state.height = p.visualData.x * 2.0f; // diameter
        state.polygon_sides = static_cast<int>(p.visualData.y);
    }

    // Extract color
    state.r = p.color.r;
    state.g = p.color.g;
    state.b = p.color.b;
    state.a = p.color.a;

    return state;
}

// Set rotation angle of an object
void SimulationWrapper::set_rotation(int index, float rotation)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
    {
        Object& p = objects[index];
        p.visualData.z = rotation;
        Objects::UpdateObjectCPU(index, p);
    }
}

// Batch retrieval of multiple objects' states
std::vector<BatchGetData> SimulationWrapper::batch_get(const std::vector<int>& indices) const {
    ensure_initialized();

    std::vector<BatchGetData> results;
    results.reserve(indices.size());

    if (indices.empty()) {
        return results;
    }

    // Fetch ALL objects at once (more efficient than per-object fetches)
    std::vector<Object> allObjects;
    Objects::FetchToCPU(m_currentBuffer, allObjects);

    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(allObjects.size())) {
            throw std::runtime_error("Invalid object index in batch_get: " + std::to_string(index));
        }

        const Object& p = allObjects[index];
        BatchGetData data;

        // Extract object properties
        data.x = p.position.x;
        data.y = p.position.y;
        data.vx = p.velocity.x;
        data.vy = p.velocity.y;
        data.mass = p.mass;
        data.charge = p.charge;
        data.rotation = p.visualData.z;
        data.angular_velocity = p.visualData.w;
        data.skin_type = p.visualSkinType;

        // Extract visual properties based on skin type
        if (p.visualSkinType == 0) { // CIRCLE
            data.radius = p.visualData.x;
            data.width = p.visualData.x * 2.0f;
            data.height = p.visualData.x * 2.0f;
            data.polygon_sides = 0;
        }
        else if (p.visualSkinType == 1) { // RECTANGLE
            data.width = p.visualData.x;
            data.height = p.visualData.y;
            data.radius = 0.0f;
            data.polygon_sides = 0;
        }
        else if (p.visualSkinType == 2) { // POLYGON
            data.radius = p.visualData.x;
            data.width = p.visualData.x * 2.0f;
            data.height = p.visualData.x * 2.0f;
            data.polygon_sides = static_cast<int>(p.visualData.y);
        }

        // Extract color
        data.r = p.color.r;
        data.g = p.color.g;
        data.b = p.color.b;
        data.a = p.color.a;

        results.push_back(data);
    }

    return results;
}

// Batch update of multiple objects' properties
void SimulationWrapper::batch_update(const std::vector<BatchUpdateData>& updates) {
    ensure_initialized();

    if (updates.empty()) {
        return;
    }

    // Fetch current objects
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    // Apply all updates
    for (const auto& update : updates) {
        int index = update.index;

        if (index < 0 || index >= static_cast<int>(objects.size())) {
            throw std::runtime_error("Invalid object index in batch_update: " + std::to_string(index));
        }

        Object& p = objects[index];
        int skinType = p.visualSkinType;

        // Update basic properties
        p.position = glm::vec2(update.x, update.y);
        p.velocity = glm::vec2(update.vx, update.vy);
        p.mass = update.mass;
        p.charge = update.charge;
        p.color = glm::vec4(update.r, update.g, update.b, update.a);

        // Update visualData based on current skin type
        if (skinType == 0) { // CIRCLE
            p.visualData.x = update.width;  // radius
            p.visualData.z = update.rotation;
            p.visualData.w = update.angular_velocity;
        }
        else if (skinType == 1) { // RECTANGLE
            p.visualData.x = update.width;
            p.visualData.y = update.height;
            p.visualData.z = update.rotation;
            p.visualData.w = update.angular_velocity;
        }
        else if (skinType == 2) { // POLYGON
            p.visualData.x = update.width;  // radius
            // Keep existing polygon sides (don't update from BatchUpdateData)
            p.visualData.z = update.rotation;
            p.visualData.w = update.angular_velocity;
        }

        // Update the object in GPU memory
        Objects::UpdateObjectCPU(index, p);
    }

    // Ensure GPU synchronization
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFlush();
}

// Set angular velocity of an object
void SimulationWrapper::set_angular_velocity(int index, float angular_velocity)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
    {
        Object& p = objects[index];
        p.visualData.w = angular_velocity;
        Objects::UpdateObjectCPU(index, p);
    }
}

// Set dimensions of an object (width/height for rectangles, radius for circles/polygons)
void SimulationWrapper::set_dimensions(int index, float width, float height)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
    {
        Object& p = objects[index];
        int skinType = p.visualSkinType;

        if (skinType == 1)  // RECTANGLE
        {
            p.visualData.x = width;
            p.visualData.y = height;
            Objects::UpdateObjectCPU(index, p);
        }
        else if (skinType == 0 || skinType == 2)  // CIRCLE or POLYGON
        {
            // For circles/polygons, width = radius
            p.visualData.x = width;  // radius
            Objects::UpdateObjectCPU(index, p);
        }
        else
        {
            throw std::runtime_error("set_dimensions only works for CIRCLE, RECTANGLE, or POLYGON objects");
        }
    }
}

// Set radius of an object (for circles, polygons, or convert rectangles to squares)
void SimulationWrapper::set_radius(int index, float radius)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
    {
        Object& p = objects[index];
        int skinType = p.visualSkinType;

        if (skinType == 0 || skinType == 2)  // CIRCLE or POLYGON
        {
            p.visualData.x = radius;
            Objects::UpdateObjectCPU(index, p);
        }
        else if (skinType == 1)  // RECTANGLE
        {
            // For rectangles, set width/height to 2*radius to make it square
            p.visualData.x = radius * 2.0f;  // width
            p.visualData.y = radius * 2.0f;  // height
            Objects::UpdateObjectCPU(index, p);
        }
        else
        {
            throw std::runtime_error("set_radius works for CIRCLE, RECTANGLE, or POLYGON objects");
        }
    }
}

// Get current rotation angle of an object
float SimulationWrapper::get_rotation(int index) const
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
        return objects[index].visualData.z;

    return 0.0f;
}

// Get current angular velocity of an object
float SimulationWrapper::get_angular_velocity(int index) const
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    if (index < static_cast<int>(objects.size()))
        return objects[index].visualData.w;

    return 0.0f;
}

// ============================================================================
// EQUATION AND CONSTRAINT FUNCTIONS
// ============================================================================

// Set physics equation for an object
void SimulationWrapper::set_equation(int object_index, const std::string& equation_string)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    try
    {
        // Parse and apply equation
        ParserContext context;
        ParsedEquation eq = ParseEquation(equation_string, context);
        Objects::SetEquation(equation_string, eq, object_index);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Equation parsing failed: " + std::string(e.what()));
    }
}

// Add distance constraint between two objects
void SimulationWrapper::add_distance_constraint(int object_index, const DistanceConstraint& constraint)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    if (constraint.target_object < 0 || constraint.target_object >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid target object");

    if (object_index == constraint.target_object)
        throw std::runtime_error("Cannot create distance constraint to self");

    if (constraint.rest_length <= 0.0f)
        throw std::runtime_error("Distance constraint must have positive rest length");

    Constraint c;
    c.type = CONSTRAINT_DISTANCE;
    c.targetObjectID = constraint.target_object;
    c.param1 = constraint.rest_length;      // Only distance parameter
    c.param2 = 0.0f;                        // Unused
    c.param3 = 0.0f;                        // Unused
    c.param4 = 0.0f;                        // Unused

    Objects::AddConstraint(object_index, c);
}

// Add boundary (box) constraint to an object
void SimulationWrapper::add_boundary_constraint(int object_index, const BoundaryConstraint& constraint)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    if (constraint.max_x <= constraint.min_x || constraint.max_y <= constraint.min_y)
        throw std::runtime_error("Invalid boundary: max must be greater than min");

    Constraint c;
    c.type = CONSTRAINT_BOUNDARY;
    c.targetObjectID = -1;
    c.param1 = constraint.min_x;
    c.param2 = constraint.max_x;
    c.param3 = constraint.min_y;
    c.param4 = constraint.max_y;

    Objects::AddConstraint(object_index, c);
}

// Clear all constraints from an object
void SimulationWrapper::clear_constraints(int object_index)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    Objects::ClearConstraints(object_index);
}

// Clear all constraints from all objects
void SimulationWrapper::clear_all_constraints()
{
    ensure_initialized();
    Objects::ClearAllConstraints();
}

// ============================================================================
// SYSTEM PARAMETER FUNCTIONS
// ============================================================================

// Set global physics parameters
void SimulationWrapper::set_parameter(const std::string& name, float value)
{
    ensure_initialized();

    if (name == "gravity" || name == "g")
        Objects::SetSystemParameters(value, 0.1f, 1.0f);
    else if (name == "damping" || name == "b")
        Objects::SetSystemParameters(9.81f, value, 1.0f);
    else if (name == "stiffness" || name == "k")
        Objects::SetSystemParameters(9.81f, 0.1f, value);
    else
        throw std::runtime_error("Unknown parameter: " + name);
}

// Get current value of a global physics parameter
float SimulationWrapper::get_parameter(const std::string& name) const
{
    ensure_initialized();

    if (name == "gravity" || name == "g") return 9.81f;
    if (name == "damping" || name == "b") return 0.1f;
    if (name == "stiffness" || name == "k") return 1.0f;

    throw std::runtime_error("Unknown parameter: " + name);
}

// ============================================================================
// FILE I/O FUNCTIONS
// ============================================================================

// Save simulation state to file
void SimulationWrapper::save_to_file(
    const std::string& filename,
    const std::string& title,
    const std::string& author,
    const std::string& description)
{
    ensure_initialized();

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    // Open file for writing
    FILE* file = fopen(filename.c_str(), "w");
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    // Write header information
    fprintf(file, "# Simulation State File\n");
    fprintf(file, "# Created with ProjStellar\n");
    if (!title.empty()) fprintf(file, "# Title: %s\n", title.c_str());
    if (!author.empty()) fprintf(file, "# Author: %s\n", author.c_str());
    if (!description.empty()) fprintf(file, "# Description: %s\n", description.c_str());
    fprintf(file, "# Version: 1.0\n\n");

    // Save system parameters
    fprintf(file, "[SYSTEM_PARAMETERS]\n");
    fprintf(file, "gravity = %.6f\n", get_parameter("gravity"));
    fprintf(file, "damping = %.6f\n", get_parameter("damping"));
    fprintf(file, "stiffness = %.6f\n\n", get_parameter("stiffness"));

    // Save camera state
    fprintf(file, "[CAMERA]\n");
    fprintf(file, "position = %.6f %.6f\n", g_camera.position.x, g_camera.position.y);
    fprintf(file, "zoom = %.6f\n\n", g_camera.zoom);

    // Save objects
    fprintf(file, "[OBJECTS]\n");
    int numObjects = object_count();
    fprintf(file, "count = %d\n\n", numObjects);

    std::vector<Object> objects;
    try {
        Objects::FetchToCPU(m_currentBuffer, objects);
    }
    catch (const std::exception& e) {
        fclose(file);
        throw std::runtime_error("Failed to fetch object data: " + std::string(e.what()));
    }

    // Write each object's state
    for (int i = 0; i < numObjects && i < static_cast<int>(objects.size()); ++i)
    {
        const Object& p = objects[i];

        fprintf(file, "OBJECT %d\n", i);
        fprintf(file, "position = %.6f %.6f\n", p.position.x, p.position.y);
        fprintf(file, "velocity = %.6f %.6f\n", p.velocity.x, p.velocity.y);
        fprintf(file, "mass = %.6f\n", p.mass);
        fprintf(file, "charge = %.6f\n", p.charge);
        fprintf(file, "skin_type = %d\n", p.visualSkinType);
        fprintf(file, "color = %.6f %.6f %.6f %.6f\n", p.color.r, p.color.g, p.color.b, p.color.a);
        fprintf(file, "rotation = %.6f\n", p.visualData.z);
        fprintf(file, "angular_velocity = %.6f\n", p.visualData.w);

        // Save shape-specific properties
        if (p.visualSkinType == 0)
            fprintf(file, "radius = %.6f\n", p.visualData.x);
        else if (p.visualSkinType == 1)
        {
            fprintf(file, "width = %.6f\n", p.visualData.x);
            fprintf(file, "height = %.6f\n", p.visualData.y);
        }
        else if (p.visualSkinType == 2)
        {
            fprintf(file, "radius = %.6f\n", p.visualData.x);
            fprintf(file, "sides = %d\n", static_cast<int>(p.visualData.y));
        }

        fprintf(file, "\n");
    }

    fclose(file);
}

// Load simulation state from file
void SimulationWrapper::load_from_file(const std::string& filename)
{
    ensure_initialized();

    std::ifstream file(filename);
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    // Clear existing simulation
    reset();
    clear_all_constraints();
    while (object_count() > 0)
        remove_object(0);

    std::string line;
    std::string current_section;
    int current_object_id = -1;
    Object current_object;
    PySkinType current_skin = PySkinType::PY_SKIN_CIRCLE;

    try
    {
        while (getline(file, line))
        {
            if (line.empty() || (line.size() > 0 && line[0] == '#'))
                continue;

            // Parse section headers
            if (line.size() > 0 && line[0] == '[' && line[line.size() - 1] == ']')
            {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

            // Parse system parameters
            if (current_section == "SYSTEM_PARAMETERS")
            {
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = line.substr(0, eq_pos);
                    std::string value = line.substr(eq_pos + 1);

                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);

                    if (key == "gravity") set_parameter("gravity", std::stof(value));
                    else if (key == "damping") set_parameter("damping", std::stof(value));
                    else if (key == "stiffness") set_parameter("stiffness", std::stof(value));
                }
            }
            // Parse camera state
            else if (current_section == "CAMERA")
            {
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = line.substr(0, eq_pos);
                    std::string value = line.substr(eq_pos + 1);

                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);

                    if (key == "position")
                    {
                        size_t space_pos = value.find(' ');
                        if (space_pos != std::string::npos)
                        {
                            g_camera.position.x = std::stof(value.substr(0, space_pos));
                            g_camera.position.y = std::stof(value.substr(space_pos + 1));
                        }
                    }
                    else if (key == "zoom")
                        g_camera.zoom = std::stof(value);
                }
            }
            // Parse object data
            else if (current_section == "OBJECTS")
            {
                if (line.find("OBJECT") == 0)
                {
                    // Create previous object before starting new one
                    if (current_object_id >= 0)
                    {
                        int pid = add_object(
                            current_object.position.x, current_object.position.y,
                            current_object.velocity.x, current_object.velocity.y,
                            current_object.mass, current_object.charge,
                            current_object.visualData.z, current_object.visualData.w,
                            current_skin,
                            (current_skin == PySkinType::PY_SKIN_CIRCLE || current_skin == PySkinType::PY_SKIN_POLYGON) ? current_object.visualData.x : 0.0f,
                            (current_skin == PySkinType::PY_SKIN_RECTANGLE) ? current_object.visualData.x : current_object.visualData.x,
                            (current_skin == PySkinType::PY_SKIN_RECTANGLE) ? current_object.visualData.y : current_object.visualData.x,
                            current_object.color.r, current_object.color.g,
                            current_object.color.b, current_object.color.a,
                            (current_skin == PySkinType::PY_SKIN_POLYGON) ? static_cast<int>(current_object.visualData.y) : 0);

                        if (pid != current_object_id)
                            std::cerr << "Object ID mismatch during load" << std::endl;
                    }

                    // Start new object
                    size_t space_pos = line.find(' ');
                    if (space_pos != std::string::npos)
                    {
                        current_object_id = std::stoi(line.substr(space_pos + 1));
                        current_object = Object();
                        current_skin = PySkinType::PY_SKIN_CIRCLE;
                    }
                }
                else if (current_object_id >= 0)
                {
                    // Parse object properties
                    size_t eq_pos = line.find('=');
                    if (eq_pos != std::string::npos)
                    {
                        std::string key = line.substr(0, eq_pos);
                        std::string value = line.substr(eq_pos + 1);

                        key.erase(0, key.find_first_not_of(" \t"));
                        key.erase(key.find_last_not_of(" \t") + 1);
                        value.erase(0, value.find_first_not_of(" \t"));
                        value.erase(value.find_last_not_of(" \t") + 1);

                        if (key == "position")
                        {
                            size_t space_pos = value.find(' ');
                            if (space_pos != std::string::npos)
                            {
                                current_object.position.x = std::stof(value.substr(0, space_pos));
                                current_object.position.y = std::stof(value.substr(space_pos + 1));
                            }
                        }
                        else if (key == "velocity")
                        {
                            size_t space_pos = value.find(' ');
                            if (space_pos != std::string::npos)
                            {
                                current_object.velocity.x = std::stof(value.substr(0, space_pos));
                                current_object.velocity.y = std::stof(value.substr(space_pos + 1));
                            }
                        }
                        else if (key == "mass")
                            current_object.mass = std::stof(value);
                        else if (key == "charge")
                            current_object.charge = std::stof(value);
                        else if (key == "skin_type")
                        {
                            int skin_type = std::stoi(value);
                            current_object.visualSkinType = skin_type;
                            if (skin_type == 0) current_skin = PySkinType::PY_SKIN_CIRCLE;
                            else if (skin_type == 1) current_skin = PySkinType::PY_SKIN_RECTANGLE;
                            else if (skin_type == 2) current_skin = PySkinType::PY_SKIN_POLYGON;
                        }
                        else if (key == "color")
                        {
                            size_t pos1 = value.find(' ');
                            size_t pos2 = value.find(' ', pos1 + 1);
                            size_t pos3 = value.find(' ', pos2 + 1);
                            if (pos1 != std::string::npos && pos2 != std::string::npos && pos3 != std::string::npos)
                            {
                                current_object.color.r = std::stof(value.substr(0, pos1));
                                current_object.color.g = std::stof(value.substr(pos1 + 1, pos2 - pos1 - 1));
                                current_object.color.b = std::stof(value.substr(pos2 + 1, pos3 - pos2 - 1));
                                current_object.color.a = std::stof(value.substr(pos3 + 1));
                            }
                        }
                        else if (key == "rotation")
                            current_object.visualData.z = std::stof(value);
                        else if (key == "angular_velocity")
                            current_object.visualData.w = std::stof(value);
                        else if (key == "radius")
                            current_object.visualData.x = std::stof(value);
                        else if (key == "width")
                            current_object.visualData.x = std::stof(value);
                        else if (key == "height")
                            current_object.visualData.y = std::stof(value);
                        else if (key == "sides")
                            current_object.visualData.y = std::stof(value);
                    }
                }
            }
        }

        // Create final object
        if (current_object_id >= 0)
        {
            int pid = add_object(
                current_object.position.x, current_object.position.y,
                current_object.velocity.x, current_object.velocity.y,
                current_object.mass, current_object.charge,
                current_object.visualData.z, current_object.visualData.w,
                current_skin,
                (current_skin == PySkinType::PY_SKIN_CIRCLE || current_skin == PySkinType::PY_SKIN_POLYGON) ? current_object.visualData.x : 0.0f,
                (current_skin == PySkinType::PY_SKIN_RECTANGLE) ? current_object.visualData.x : current_object.visualData.x,
                (current_skin == PySkinType::PY_SKIN_RECTANGLE) ? current_object.visualData.y : current_object.visualData.x,
                current_object.color.r, current_object.color.g,
                current_object.color.b, current_object.color.a,
                (current_skin == PySkinType::PY_SKIN_POLYGON) ? static_cast<int>(current_object.visualData.y) : 0);
        }
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Error parsing simulation file: " + std::string(e.what()));
    }

    file.close();
}

// ============================================================================
// SIMULATION CONTROL FUNCTIONS
// ============================================================================

// Pause or resume simulation
void SimulationWrapper::set_paused(bool paused)
{
    m_paused = paused;
}

// Check if simulation is paused
bool SimulationWrapper::is_paused() const
{
    return m_paused;
}

// Reset simulation to initial state
void SimulationWrapper::reset()
{
    ensure_initialized();
    Objects::ResetToInitialConditions();
    m_currentBuffer = 0;
}

// Run batch simulations (for parameter studies, optimization, etc.)
void SimulationWrapper::run_batch(
    const std::vector<BatchConfig>& configs,
    std::function<void(int, const std::vector<ObjectState>&)> callback)
{
    ensure_initialized();

    if (!m_headless)
        throw std::runtime_error("Batch mode only available in headless mode");

    // Run each configuration
    for (size_t i = 0; i < configs.size(); ++i)
    {
        const auto& config = configs[i];

        // Reset simulation
        reset();
        clear_all_constraints();

        // Remove any existing objects
        while (object_count() > 0)
            remove_object(0);

        // Create objects from configuration
        for (const auto& pconfig : config.objects)
        {
            int pid = add_object(
                pconfig.x, pconfig.y,
                pconfig.vx, pconfig.vy,
                pconfig.mass, pconfig.charge,
                pconfig.rotation, pconfig.angular_velocity,
                pconfig.skin,
                pconfig.size,
                pconfig.width, pconfig.height,
                pconfig.r, pconfig.g, pconfig.b, pconfig.a,
                pconfig.polygon_sides);

            // Apply equations and constraints
            if (!pconfig.equation.empty())
                set_equation(pid, pconfig.equation);

            for (const auto& constraint : pconfig.constraints)
            {
                if (constraint.type == 0)
                {
                    DistanceConstraint dc;
                    dc.target_object = constraint.target;
                    dc.rest_length = constraint.param1;
                    add_distance_constraint(pid, dc);
                }
                else if (constraint.type == 1)
                {
                    BoundaryConstraint bc;
                    bc.min_x = constraint.param1;
                    bc.max_x = constraint.param2;
                    bc.min_y = constraint.param3;
                    bc.max_y = constraint.param4;
                    add_boundary_constraint(pid, bc);
                }
            }
        }

        // Run simulation for specified duration
        int steps = static_cast<int>(config.duration / config.dt);
        std::vector<ObjectState> results;

        for (int step = 0; step < steps; ++step)
        {
            update(config.dt);

            // Capture final state
            if (step == steps - 1)
            {
                results.clear();
                for (int p = 0; p < object_count(); ++p)
                    results.push_back(get_object(p));

                // Call callback with results
                if (callback)
                    callback(static_cast<int>(i), results);
            }
        }

        // Save results to file if specified
        if (!config.output_file.empty())
            save_results(config.output_file, results);
    }
}

// Save simulation results to CSV file
void SimulationWrapper::save_results(
    const std::string& filename,
    const std::vector<ObjectState>& states)
{
    std::ofstream file(filename);
    if (!file)
        throw std::runtime_error("Failed to open output file: " + filename);

    // Write CSV header
    file << "object_id,x,y,vx,vy,mass,charge,rotation,angular_velocity,"
        << "width,height,radius,polygon_sides,skin_type,r,g,b,a\n";

    // Write each object's state
    for (size_t i = 0; i < states.size(); ++i)
    {
        const auto& s = states[i];
        file << i << ","
            << s.x << "," << s.y << ","
            << s.vx << "," << s.vy << ","
            << s.mass << "," << s.charge << ","
            << s.rotation << "," << s.angular_velocity << ","
            << s.width << "," << s.height << ","
            << s.radius << "," << s.polygon_sides << ","
            << s.skin_type << ","
            << s.r << "," << s.g << "," << s.b << "," << s.a << "\n";
    }
}

// ============================================================================
// SHADER MANAGEMENT FUNCTIONS
// ============================================================================

// Update shader loading status (for async shader compilation)
void SimulationWrapper::update_shader_loading()
{
    ensure_initialized();

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    Objects::UpdateShaderLoadingStatus();

    if (!m_headless && m_window)
        glfwPollEvents();

    glFlush();
    glFinish();
}

// Check if all shaders are loaded and ready
bool SimulationWrapper::are_all_shaders_ready() const
{
    ensure_initialized();
    return Objects::AreAllShadersReady();
}

// Get overall shader loading progress (0.0 to 1.0)
float SimulationWrapper::get_shader_load_progress() const
{
    ensure_initialized();
    return Objects::GetShaderLoadProgress();
}

// Get human-readable shader loading status
std::string SimulationWrapper::get_shader_load_status() const
{
    ensure_initialized();
    return Objects::GetShaderLoadStatusMessage();
}

// ============================================================================
// CLEANUP FUNCTION
// ============================================================================

// Clean up all simulation resources
void SimulationWrapper::cleanup()
{
    if (!m_initialized) return;

    // Clean up axis system if initialized
    if (g_axisInitialized)
    {
        Axis::Cleanup();
        g_axisInitialized = false;
    }

    // Clean up axis shader
    if (g_axisShaderProgram)
    {
        glDeleteProgram(g_axisShaderProgram);
        g_axisShaderProgram = 0;
    }

    // Clean up object system
    Objects::Cleanup();

    // Destroy window and terminate GLFW
    if (m_window)
    {
        glfwDestroyWindow(static_cast<GLFWwindow*>(m_window));
        m_window = nullptr;
    }

    glfwTerminate();
    m_initialized = false;
}