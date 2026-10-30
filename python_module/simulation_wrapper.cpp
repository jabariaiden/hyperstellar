#include "simulation_wrapper.h"

#include "../include/objects.h"
#include "../include/gpu_serializer.h"
#include "../include/parser.h"
#include "../include/constraints.h"
#include "../include/async_shader_loader.h"
#include "../include/axis.h"
#include "../include/camera.h"
#include "../include/globals.h"
#include "../include/mesh.h"
#include "script_manager.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <sstream>
#include <utility>
#include <functional> // for std::function
#include <vector>
#include <atomic>

// ----------------------------------------------------------------------------
// OBJECT HANDLE IMPLEMENTATION
// ----------------------------------------------------------------------------

// Static generation counter for stable handles
static std::vector<int> g_objectGenerations(Objects::MAX_OBJECTS, 0);
static std::vector<bool> g_objectSlotsUsed(Objects::MAX_OBJECTS, false);

// Global collision callback storage
static std::function<void(const CollisionEvent &)> g_collisionCallback;

static std::atomic<int> g_activeSimCount{0};
bool AnySimulationInitialized() { return g_activeSimCount.load() > 0; }

namespace
{
    bool g_axisInitialized = false; // Tracks axis system initialization
    GLuint g_axisShaderProgram = 0; // Shader program for rendering axes/grid
}

// Create shader program for rendering coordinate axes and grid
GLuint CreateAxisShader()
{
    const char *vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aColor;
    uniform mat4 uProjView;
    out vec3 Color;
    void main() {
        gl_Position = uProjView * vec4(aPos, 1.0);
        Color = aColor;
    }
)";

    const char *fragmentShaderSource = R"(
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

void SimulationWrapper::set_paint_script(int script_id)
{
    ensure_initialized();
    Objects::SetPaintScript(script_id);
}

static int key_name_to_code(const std::string &name)
{
    // Single letters A-Z
    if (name.size() == 1 && isalpha(name[0]))
    {
        char c = toupper(name[0]);
        if (c >= 'A' && c <= 'Z')
            return GLFW_KEY_A + (c - 'A');
    }
    // Numbers 0-9
    if (name.size() == 1 && isdigit(name[0]))
    {
        return GLFW_KEY_0 + (name[0] - '0');
    }
    // Special keys
    if (name == "Space")
        return GLFW_KEY_SPACE;
    if (name == "Shift")
        return GLFW_KEY_LEFT_SHIFT;
    if (name == "Control")
        return GLFW_KEY_LEFT_CONTROL;
    if (name == "Alt")
        return GLFW_KEY_LEFT_ALT;
    if (name == "Escape")
        return GLFW_KEY_ESCAPE;
    if (name == "Enter")
        return GLFW_KEY_ENTER;
    if (name == "Tab")
        return GLFW_KEY_TAB;
    if (name == "Backspace")
        return GLFW_KEY_BACKSPACE;
    if (name == "Delete")
        return GLFW_KEY_DELETE;
    if (name == "Home")
        return GLFW_KEY_HOME;
    if (name == "End")
        return GLFW_KEY_END;
    if (name == "PageUp")
        return GLFW_KEY_PAGE_UP;
    if (name == "PageDown")
        return GLFW_KEY_PAGE_DOWN;
    if (name == "Insert")
        return GLFW_KEY_INSERT;
    if (name == "Up")
        return GLFW_KEY_UP;
    if (name == "Down")
        return GLFW_KEY_DOWN;
    if (name == "Left")
        return GLFW_KEY_LEFT;
    if (name == "Right")
        return GLFW_KEY_RIGHT;
    // F1-F12
    if (name.size() >= 2 && name[0] == 'F')
    {
        int num = std::stoi(name.substr(1));
        if (num >= 1 && num <= 12)
            return GLFW_KEY_F1 + (num - 1);
    }
    return -1; // unknown
}

// ============================================================================
// Scratchpad
// ============================================================================

int SimulationWrapper::create_scratchpad(size_t size)
{
    ensure_initialized();
    return Objects::CreateScratchpad(size);
}

void SimulationWrapper::destroy_scratchpad(int id)
{
    ensure_initialized();
    Objects::DestroyScratchpad(id);
}

void SimulationWrapper::upload_scratchpad(int id, const std::vector<float> &data)
{
    ensure_initialized();
    Objects::UploadScratchpadData(id, data.data(), data.size());
}

std::vector<float> SimulationWrapper::map_scratchpad(int id)
{
    ensure_initialized();
    if (!Objects::IsValidScratchpad(id))
        return {};
    float *ptr = (float *)Objects::MapScratchpad(id, GL_READ_ONLY);
    if (!ptr)
        return {};
    size_t count = Objects::GetScratchpadSize(id);
    std::vector<float> result(ptr, ptr + count);
    Objects::UnmapScratchpad(id);
    return result;
}

size_t SimulationWrapper::scratchpad_size(int id)
{
    ensure_initialized();
    return Objects::GetScratchpadSize(id);
}

bool SimulationWrapper::is_valid_scratchpad(int id)
{
    ensure_initialized();
    return Objects::IsValidScratchpad(id);
}

// ============================================================================
// Signal Queue
// ============================================================================

void SimulationWrapper::set_signal_queue_capacity(size_t capacity)
{
    ensure_initialized();
    Objects::SetSignalQueueCapacity(capacity);
}

void SimulationWrapper::clear_signal_queue()
{
    ensure_initialized();
    Objects::ClearSignalQueue();
}

size_t SimulationWrapper::get_signal_queue_count()
{
    ensure_initialized();
    return Objects::GetSignalQueueCount();
}

// ============================================================================
// Agent Dispatch
// ============================================================================

void SimulationWrapper::dispatch_agent(int agent_id, bool clear_after)
{
    ensure_initialized();
    Objects::DispatchAgent(agent_id, clear_after);
}

void SimulationWrapper::dispatch_all_agents(bool clear_after)
{
    ensure_initialized();
    auto agent_ids = scriptManager.getAgentIDs();
    for (int id : agent_ids)
    {
        Objects::DispatchAgent(id, false); // don't clear per agent
    }
    if (clear_after)
    {
        Objects::ClearSignalQueue();
    }
}

// ============================================================================
// Agent Registration
// ============================================================================

int SimulationWrapper::register_agent(const std::string &source)
{
    ensure_initialized();
    int id = scriptManager.registerScript(source);
    if (id >= 0)
    {
        scriptManager.markAsAgent(id);
    }
    return id;
}

std::vector<int> SimulationWrapper::get_agent_ids()
{
    ensure_initialized();
    return scriptManager.getAgentIDs();
}

// Constructor: Initialize simulation with optional graphics
SimulationWrapper::SimulationWrapper(bool headless, int width, int height, std::string title, bool enable_grid)
    : m_headless(headless), m_initialized(false), m_paused(false),
      m_window(nullptr), m_currentBuffer(0), m_title(title),
      m_width(width), m_height(height), m_simulationTime(0.0f),
      m_enable_grid(enable_grid),
      m_currentKeys{}, m_previousKeys{}

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
        g_activeSimCount.fetch_add(1);
    }
    catch (const std::exception &e)
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

    glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));

    // Load OpenGL functions
    if (!gladLoaderLoadGL())
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    glFlush();
    glFinish();

    // Clear any OpenGL errors
    while (glGetError() != GL_NO_ERROR)
        ;

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
    Objects::SetScriptManager(&scriptManager);
    scriptManager.setAsGlobal();

    m_initialized = true;
    return true;
}

// Window resize callback
void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    SimulationWrapper *sim = static_cast<SimulationWrapper *>(glfwGetWindowUserPointer(window));
    if (sim)
    {
        // Update global viewport settings
        g_width = width;
        g_height = height;
        g_simulationViewportSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
        glViewport(0, 0, width, height);
    }
}

void SimulationWrapper::set_paint_resolution(int width, int height)
{
    Objects::SetPaintResolution(width, height);
}
// Initialize windowed mode (with graphics)
bool SimulationWrapper::init_windowed(int width, int height, const std::string &title)
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
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE); // Prevent maximised/fullscreen
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);  // Ensure window decorations

    // Create window
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Force windowed mode and exact dimensions, too lazy to handle all the edge cases of monitor setups and fullscreen modes ;)
    glfwSetWindowMonitor(static_cast<GLFWwindow *>(m_window), nullptr, 100, 100, width, height, GLFW_DONT_CARE);
    glfwSetWindowSize(static_cast<GLFWwindow *>(m_window), width, height);
    glfwSetWindowPos(static_cast<GLFWwindow *>(m_window), 100, 100);
    glfwRestoreWindow(static_cast<GLFWwindow *>(m_window)); // Remove any maximised state

    // Set up window context and callbacks
    glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));
    glfwSetWindowUserPointer(static_cast<GLFWwindow *>(m_window), this);
    glfwSetFramebufferSizeCallback(static_cast<GLFWwindow *>(m_window), framebuffer_size_callback);
    glfwSwapInterval(1); // Enable vsync

    // Show and focus window
    glfwShowWindow(static_cast<GLFWwindow *>(m_window));
    glfwFocusWindow(static_cast<GLFWwindow *>(m_window));

    // Process initial events to let the window settle
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
    while (glGetError() != GL_NO_ERROR)
        ;

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
    g_camera3d.Reset();

    // Initialize axis/grid system if enabled
    if (m_enable_grid)
    {
        if (!g_axisInitialized)
        {
            Axis::Init();
            g_axisInitialized = true;

            // Configure axis appearance
            Axis::Style &style = Axis::GetStyle();
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

    // Initialize object system (includes paint shader and texture, but not resized on window resize)
    if (!Objects::Init(m_window))
    {
        std::cerr << "Failed to initialize object system" << std::endl;
        return false;
    }
    Objects::SetScriptManager(&scriptManager);
    scriptManager.setAsGlobal();

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
        GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(m_window);
        glfwMakeContextCurrent(glfwWindow);

        // Update keyboard state for this frame
        update_keyboard_state();

        // Mouse delta is computed lazily in get_mouse_delta
    }
}

void SimulationWrapper::default_input()
{
    if (m_headless || !m_window)
        return;
    process_input();
    GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(m_window);
    glfwMakeContextCurrent(glfwWindow);

    const float dt = 0.016f;
    const float orbitStep = 0.02f;

    // WASD orbits g_camera3d around its target.
    float dyaw = 0.0f, dpitch = 0.0f;
    if (is_key_pressed(GLFW_KEY_A))
        dyaw -= orbitStep;
    if (is_key_pressed(GLFW_KEY_D))
        dyaw += orbitStep;
    if (is_key_pressed(GLFW_KEY_W))
        dpitch += orbitStep;
    if (is_key_pressed(GLFW_KEY_S))
        dpitch -= orbitStep;
    if (dyaw != 0.0f || dpitch != 0.0f)
        g_camera3d.Orbit(dyaw, dpitch);

    // Q/E dollies the eye along the view direction.
    float dollyStep = g_camera3d.ApparentHalfHeight() * 0.5f * dt * 30.0f;
    if (is_key_pressed(GLFW_KEY_Q))
        g_camera3d.Dolly(-dollyStep);
    if (is_key_pressed(GLFW_KEY_E))
        g_camera3d.Dolly(+dollyStep);

    if (is_key_just_pressed(GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
}

std::pair<float, float> SimulationWrapper::get_mouse_position() const
{
    if (!m_window)
        return {0.0f, 0.0f};
    GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(m_window);
    double x, y;
    glfwGetCursorPos(glfwWindow, &x, &y);

    // Convert to world coordinates
    int w, h;
    glfwGetFramebufferSize(glfwWindow, &w, &h);
    if (w == 0 || h == 0)
        return {0.0f, 0.0f};

    float ndcX = (float)x / w * 2.0f - 1.0f;
    float ndcY = 1.0f - (float)y / h * 2.0f;

    float aspect = (float)w / (float)h;
    float halfWidth = g_camera.zoom * aspect;
    float halfHeight = g_camera.zoom;

    float worldX = g_camera.position.x + ndcX * halfWidth;
    float worldY = g_camera.position.y + ndcY * halfHeight;

    return {worldX, worldY};
}

std::pair<float, float> SimulationWrapper::get_mouse_delta() const
{
    if (!m_window)
        return {0.0f, 0.0f};
    GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(m_window);
    double x, y;
    glfwGetCursorPos(glfwWindow, &x, &y);

    float dx = static_cast<float>(x - m_prevMouseX);
    float dy = static_cast<float>(y - m_prevMouseY);

    // Update stored previous position
    m_prevMouseX = x;
    m_prevMouseY = y;

    return {dx, dy};
}

void SimulationWrapper::update_keyboard_state()
{
    if (!m_window)
        return;
    GLFWwindow *glfwWindow = static_cast<GLFWwindow *>(m_window);

    // Save previous state
    for (int i = 0; i < MAX_KEYS; ++i)
        m_previousKeys[i] = m_currentKeys[i];

    // Read current state
    for (int i = 0; i < MAX_KEYS; ++i)
        m_currentKeys[i] = (glfwGetKey(glfwWindow, i) == GLFW_PRESS);
}

bool SimulationWrapper::is_key_pressed(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return m_currentKeys[key];
}

bool SimulationWrapper::is_key_just_pressed(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return m_currentKeys[key] && !m_previousKeys[key];
}

bool SimulationWrapper::is_key_just_released(int key) const
{
    if (key < 0 || key >= MAX_KEYS)
        return false;
    return !m_currentKeys[key] && m_previousKeys[key];
}

KeyState KeyboardMonitor::get_key_state(const std::string &name) const
{
    int code = key_name_to_code(name);
    if (code == -1)
        return KeyState(false, false);
    bool pressed = m_sim->is_key_pressed(code);
    bool released = m_sim->is_key_just_released(code);
    return KeyState(pressed, released);
}

void SimulationWrapper::set_key_state(int key, bool pressed)
{
    if (key < 0 || key >= MAX_KEYS)
        return;
    m_previousKeys[key] = m_currentKeys[key];
    m_currentKeys[key] = pressed;
}

void SimulationWrapper::set_camera_position(float x, float y)
{
    g_camera.position = glm::vec2(x, y);
}

std::pair<float, float> SimulationWrapper::get_camera_position() const
{
    return {g_camera.position.x, g_camera.position.y};
}

void SimulationWrapper::set_camera_zoom(float zoom)
{
    g_camera.zoom = zoom;
}

float SimulationWrapper::get_camera_zoom() const
{
    return g_camera.zoom;
}

// Check if window should close
bool SimulationWrapper::should_close() const
{
    if (!m_headless && m_window)
        return glfwWindowShouldClose(static_cast<GLFWwindow *>(m_window));
    return false;
}

// ============================================================================
//  COLLISION PARAMETER MANAGEMENT
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
    if (m_paused)
        return;

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));

    const float FIXED_STEP = 0.001f;
    static float accumulator = 0.0f;
    float scaled_dt = dt * m_speed;
    accumulator += scaled_dt; // accumulator accumulates scaled time

    int stepCount = 0;
    const int MAX_STEPS_PER_FRAME = 20;

    while (accumulator >= FIXED_STEP && stepCount < MAX_STEPS_PER_FRAME)
    {
        m_simulationTime += FIXED_STEP;
        g_simulationTime = m_simulationTime;

        while (glGetError() != GL_NO_ERROR)
            ;

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

            //  Get collision parameter uniforms
            GLint enableWarmStartLoc = glGetUniformLocation(computeProgram, "uEnableWarmStart");
            GLint maxContactIterationsLoc = glGetUniformLocation(computeProgram, "uMaxContactIterations");

            // Apply Uniforms
            if (dtLoc != -1)
                glUniform1f(dtLoc, FIXED_STEP);
            if (timeLoc != -1)
                glUniform1f(timeLoc, m_simulationTime);
            if (kLoc != -1)
                glUniform1f(kLoc, m_stiffness);
            if (bLoc != -1)
                glUniform1f(bLoc, m_damping);
            if (gLoc != -1)
                glUniform1f(gLoc, m_gravity);
            if (gravityDirLoc != -1)
                glUniform2f(gravityDirLoc, 0.0f, -1.0f);
            if (restitutionLoc != -1)
                glUniform1f(restitutionLoc, 0.7f);
            if (couplingLoc != -1)
                glUniform1f(couplingLoc, 1.0f);
            if (externalForceLoc != -1)
                glUniform2f(externalForceLoc, 0.0f, 0.0f);
            if (driveFreqLoc != -1)
                glUniform1f(driveFreqLoc, 1.0f);
            if (driveAmpLoc != -1)
                glUniform1f(driveAmpLoc, 0.0f);
            if (equationModeLoc != -1)
                glUniform1i(equationModeLoc, 0);
            if (numObjectsLoc != -1)
                glUniform1i(numObjectsLoc, Objects::GetNumObjects());

            //  Apply collision parameters
            auto collision_params = get_collision_parameters();
            if (enableWarmStartLoc != -1)
                glUniform1i(enableWarmStartLoc, collision_params.first ? 1 : 0);
            if (maxContactIterationsLoc != -1)
                glUniform1i(maxContactIterationsLoc, collision_params.second);

            // Run Compute Shader
            Objects::Update(m_currentBuffer, 1 - m_currentBuffer, FIXED_STEP);
            m_currentBuffer = 1 - m_currentBuffer;

            // Fire collision callbacks by reading the contact buffer directly
            if (g_collisionCallback)
            {
                GLuint contactBuffer = Objects::GetContactBuffer();
                if (contactBuffer)
                {
                    // Define the contact point layout matching the shader (48 bytes)
                    struct ContactPoint
                    {
                        float normal[2];
                        float position[2];
                        float penetration;
                        float accumulatedNormalImpulse;
                        float accumulatedTangentImpulse;
                        int frameCount;
                        int padding[4]; // total 48 bytes
                    };

                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, contactBuffer);
                    GLsizeiptr size = Objects::MAX_OBJECTS * 4 * sizeof(ContactPoint);
                    void *data = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
                    if (data)
                    {
                        ContactPoint *contacts = static_cast<ContactPoint *>(data);
                        int numObjects = Objects::GetNumObjects();
                        for (int obj = 0; obj < numObjects; ++obj)
                        {
                            int base = obj * 4;
                            for (int c = 0; c < 4; ++c)
                            {
                                int idx = base + c;
                                if (idx * sizeof(ContactPoint) >= size)
                                    break;
                                ContactPoint &cp = contacts[idx];
                                if (cp.frameCount > 0)
                                {
                                    CollisionEvent ev;
                                    ev.object_a.slot = obj;
                                    ev.object_a.generation = 0; // generation not stored; you could add a getter
                                    ev.object_b.slot = -1;
                                    ev.object_b.generation = -1;
                                    ev.normal_x = cp.normal[0];
                                    ev.normal_y = cp.normal[1];
                                    ev.contact_x = cp.position[0];
                                    ev.contact_y = cp.position[1];
                                    ev.penetration = cp.penetration;
                                    ev.impulse = cp.accumulatedNormalImpulse + cp.accumulatedTangentImpulse;
                                    g_collisionCallback(ev);
                                }
                            }
                        }
                        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                    }
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
                }
            }

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
//  Painting management
// ============================================================================
void SimulationWrapper::paint(const std::string &equation)
{
    ParserContext ctx;
    ctx.registerVariable("px", ParserContext::DOMAIN_SPATIAL, false);
    ctx.registerVariable("py", ParserContext::DOMAIN_SPATIAL, false);

    ParsedEquation parsed = ParseEquation(equation, ctx);
    GPUSerializedEquation gpu = serializeEquationForGPU(parsed);

    Objects::SetPaintEquation(
        gpu.tokenBuffer_r, gpu.constantBuffer_r,
        gpu.tokenBuffer_g, gpu.constantBuffer_g,
        gpu.tokenBuffer_b, gpu.constantBuffer_b,
        gpu.tokenBuffer_a, gpu.constantBuffer_a);
}
void SimulationWrapper::get_paint_image(std::vector<unsigned char> &jpeg_data, int quality)
{
    ensure_initialized();
    Objects::GetPaintImage(jpeg_data, quality);
}
// ============================================================================
// Render simulation to screen
// ============================================================================
void SimulationWrapper::render()
{
    if (m_headless || !m_window)
        return;
    glfwMakeContextCurrent((GLFWwindow *)m_window);

    int w, h;
    glfwGetFramebufferSize((GLFWwindow *)m_window, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Axis / grid uses the 3D camera's view-projection
    if (m_enable_grid && g_axisShaderProgram)
    {
        Camera gridCam;
        gridCam.position = glm::vec2(g_camera3d.target.x, g_camera3d.target.y);
        gridCam.zoom = g_camera3d.ApparentHalfHeight();

        Axis::Update(gridCam, (float)w, (float)h);

        glm::mat4 VP = g_camera3d.ViewProj(w, h);
        glUseProgram(g_axisShaderProgram);
        GLint pv = glGetUniformLocation(g_axisShaderProgram, "uProjView");
        glUniformMatrix4fv(pv, 1, GL_FALSE, glm::value_ptr(VP));
        Axis::Draw(g_axisShaderProgram, VP);
        glUseProgram(0);
    }

    // Object pass: one draw call per mesh kind
    glm::mat4 P = g_camera3d.Projection(w, h);
    glm::mat4 V = g_camera3d.View();
    glm::vec3 camPos = g_camera3d.eye;
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -0.7f, -0.6f));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    Objects::Draw(m_currentBuffer, P, V, camPos, lightDir);
    glDisable(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    Objects::DispatchPaint(w, h,
                           g_camera3d.target.x, g_camera3d.target.y,
                           g_camera3d.ApparentHalfHeight(),
                           m_currentBuffer);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    glfwSwapBuffers((GLFWwindow *)m_window);
    glfwPollEvents();
}

void SimulationWrapper::get_full_frame(std::vector<unsigned char> &jpeg_data, int quality)
{
    ensure_initialized();

    // Compute projection matrix from camera
    int w = g_width;
    int h = g_height;
    if (g_paintWidth > 0 && g_paintHeight > 0)
    {
        w = g_paintWidth;
        h = g_paintHeight;
    }
    glm::mat4 projection = g_camera.GetProjectionMatrix(w, h);

    // Call the objects system to capture the frame
    Objects::GetFullFrameImage(jpeg_data, quality, m_currentBuffer, projection);
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
    CollisionShape cppShape = static_cast<CollisionShape>(static_cast<int>(shape));
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
    config.shape = static_cast<PyCollisionShape>(props.shapeType);

    return config;
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

// Add a new physics object to the simulation (raw index version)
int SimulationWrapper::add_object_raw(
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

    Object newObject{};
    newObject.position = glm::vec4(x, y, 0.0f, 0.0f);
    newObject.velocity = glm::vec4(vx, vy, 0.0f, 0.0f);
    newObject.mass = mass;
    newObject.charge = charge;
    newObject.color = glm::vec4(r, g, b, a);
    newObject.collisionData = glm::vec4(0.0f);
    newObject.visualSkinType = static_cast<int>(skin);
    newObject.equationID = 0;
    newObject.scriptID = -1;
    memset(newObject._pad, 0, sizeof(newObject._pad));

    // Rotation / angular velocity live in orientation and spin.
    float halfAngle = rotation * 0.5f;
    newObject.orientation = glm::vec4(0.0f, 0.0f, std::sin(halfAngle), std::cos(halfAngle));
    newObject.spin = glm::vec4(0.0f, 0.0f, angular_velocity, 0.0f);

    // Per-skin mesh, visual data, and collision shape.
    PyCollisionShape collisionShape = PyCollisionShape::NONE;
    int meshID = -1;

    switch (skin)
    {
    case PySkinType::PY_SKIN_CIRCLE:
        newObject.visualData = glm::vec4(size, 0.0f, 0.0f, 0.0f);
        collisionShape = PyCollisionShape::CIRCLE;
        meshID = Meshes::RegisterCircle(size, 32);
        break;

    case PySkinType::PY_SKIN_RECTANGLE:
        newObject.visualData = glm::vec4(width, height, 0.0f, 0.0f);
        collisionShape = PyCollisionShape::AABB;
        meshID = Meshes::RegisterSquare(width, height);
        break;

    case PySkinType::PY_SKIN_POLYGON:
        polygon_sides = std::max(3, std::min(12, polygon_sides));
        newObject.visualData = glm::vec4(size, static_cast<float>(polygon_sides), 0.0f, 0.0f);
        collisionShape = PyCollisionShape::POLYGON;
        meshID = Meshes::RegisterPolygon(size, polygon_sides);
        break;
    }

    newObject.meshID = meshID;
    newObject.collisionShapeType = static_cast<int>(collisionShape);

    Objects::AddObject(meshID);
    int objectID = Objects::GetNumObjects() - 1;
    Objects::UpdateObjectCPU(objectID, newObject);

    set_collision_shape(objectID, collisionShape);
    set_collision_enabled(objectID, true);
    set_collision_properties(objectID, 0.7f, 0.3f);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFlush();

    g_objectSlotsUsed[objectID] = true;

    return objectID;
}

// Add a new physics object and return an ObjectHandle
ObjectHandle SimulationWrapper::add_object(
    float x, float y, float vx, float vy,
    float mass, float charge,
    float rotation, float angular_velocity,
    PySkinType skin,
    float size,
    float width, float height,
    float r, float g, float b, float a,
    int polygon_sides)
{
    int slot = add_object_raw(x, y, vx, vy, mass, charge, rotation, angular_velocity,
                              skin, size, width, height, r, g, b, a, polygon_sides);
    return make_handle(slot);
}

// Update properties of an existing object
void SimulationWrapper::update_object(
    int index,
    float x, float y,
    float vx, float vy,
    float mass, float charge,
    float rotation, float angular_velocity,
    float size, float width, float height,
    float r, float g, float b, float a,
    int polygon_sides)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;

    Object &p = objects[index];
    p.position = glm::vec4(x, y, p.position.z, 0.0f);
    p.velocity = glm::vec4(vx, vy, p.velocity.z, 0.0f);
    p.mass = mass;
    p.charge = charge;
    p.color = glm::vec4(r, g, b, a);

    float half = rotation * 0.5f;
    p.orientation = glm::vec4(0.0f, 0.0f, std::sin(half), std::cos(half));
    p.spin = glm::vec4(0.0f, 0.0f, angular_velocity, 0.0f);

    if (p.visualSkinType == -1)
    {
        throw std::runtime_error(
            "update_object() is 2D-only; use the *_3d setters "
            "(set_position_3d, set_velocity_3d, set_orientation_3d, set_spin_3d) "
            "for 3D objects");
    }

    if (p.visualSkinType == 0)
    {
        p.visualData = glm::vec4(size, 0.0f, p.visualData.z, p.visualData.w);
    }
    else if (p.visualSkinType == 1)
    {
        p.visualData = glm::vec4(width, height, p.visualData.z, p.visualData.w);
    }
    else if (p.visualSkinType == 2)
    {
        float sides = (polygon_sides > 0) ? static_cast<float>(polygon_sides)
                                          : p.visualData.y;
        p.visualData = glm::vec4(size, sides, p.visualData.z, p.visualData.w);
    }

    Objects::UpdateObjectCPU(index, p);
}

ObjectHandle SimulationWrapper::add_object_3d(
    float x, float y, float z,
    float vx, float vy, float vz,
    float mass, float charge,
    int meshID,
    glm::vec4 orientation,
    glm::vec3 spin,
    float r, float g, float b, float a,
    PyCollisionShape shape)
{
    ensure_initialized();
    if (Objects::GetNumObjects() >= Objects::MAX_OBJECTS)
        throw std::runtime_error("Maximum object limit reached");
    const Mesh *mesh = Meshes::Get(meshID);
    if (!mesh)
        throw std::runtime_error("Invalid mesh handle");

    float len = glm::length(orientation);
    if (len < 1e-6f)
        orientation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    else
        orientation /= len;

    Object obj;
    obj.position = glm::vec4(x, y, z, 0.0f);
    obj.velocity = glm::vec4(vx, vy, vz, 0.0f);
    obj.orientation = orientation;
    obj.spin = glm::vec4(spin, 0.0f);

    // AABB-derived size for collisions: xyz = full extents, w = bounding-sphere radius.
    glm::vec3 ext = mesh->aabbMax - mesh->aabbMin;
    obj.visualData = glm::vec4(ext, 0.5f * std::max({ext.x, ext.y, ext.z}));

    obj.collisionData = glm::vec4(0.0f);
    obj.color = glm::vec4(r, g, b, a);
    obj.mass = mass;
    obj.charge = charge;
    obj.visualSkinType = -1;
    obj.collisionShapeType = static_cast<int>(shape); // was COLLISION_NONE
    obj.equationID = 0;
    obj.scriptID = -1;
    obj.meshID = meshID;
    memset(obj._pad, 0, sizeof(obj._pad));

    Objects::AddObject(meshID);
    int slot = Objects::GetNumObjects() - 1;
    Objects::UpdateObjectCPU(slot, obj);

    set_collision_shape(slot, shape);
    set_collision_enabled(slot, true);
    set_collision_properties(slot, 0.7f, 0.3f);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFlush();

    g_objectSlotsUsed[slot] = true;
    // Do NOT touch -> g_objectGenerations[slot] = 0;

    return make_handle(slot);
}

void SimulationWrapper::set_mesh_3d(int index, int meshID)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    if (meshID >= 0 && Meshes::Get(meshID) == nullptr)
        throw std::runtime_error("Invalid mesh handle");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;
    objects[index].meshID = meshID;
    Objects::UpdateObjectCPU(index, objects[index]);
}

int SimulationWrapper::get_mesh_3d(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return -1;
    return objects[index].meshID;
}

void SimulationWrapper::set_position_3d(int index, float x, float y, float z)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;
    objects[index].position = glm::vec4(x, y, z, 0.0f);
    Objects::UpdateObjectCPU(index, objects[index]);
}

glm::vec3 SimulationWrapper::get_position_3d(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return glm::vec3(0.0f);
    return glm::vec3(objects[index].position);
}

void SimulationWrapper::set_velocity_3d(int index, float vx, float vy, float vz)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;
    objects[index].velocity = glm::vec4(vx, vy, vz, 0.0f);
    Objects::UpdateObjectCPU(index, objects[index]);
}

glm::vec3 SimulationWrapper::get_velocity_3d(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return glm::vec3(0.0f);
    return glm::vec3(objects[index].velocity);
}

void SimulationWrapper::set_orientation_3d(int index, float qx, float qy, float qz, float qw)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;

    glm::vec4 q(qx, qy, qz, qw);
    float len = glm::length(q);
    if (len < 1e-6f)
        q = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    else
        q /= len;

    objects[index].orientation = q;
    Objects::UpdateObjectCPU(index, objects[index]);
}

glm::vec4 SimulationWrapper::get_orientation_3d(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return glm::vec4(0, 0, 0, 1);
    return objects[index].orientation;
}

void SimulationWrapper::set_spin_3d(int index, float sx, float sy, float sz)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;
    objects[index].spin = glm::vec4(sx, sy, sz, 0.0f);
    Objects::UpdateObjectCPU(index, objects[index]);
}

glm::vec3 SimulationWrapper::get_spin_3d(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return glm::vec3(0.0f);
    return glm::vec3(objects[index].spin);
}

// Remove an object from the simulation (raw index, updates generation)
void SimulationWrapper::remove_object(int index)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    int lastIdx = Objects::GetNumObjects() - 1;
    bool isLast = (index == lastIdx);

    int movedGen = 0;
    if (!isLast)
    {
        movedGen = g_objectGenerations[lastIdx];
    }

    Objects::RemoveObject(index);

    int newCount = Objects::GetNumObjects(); // after removal, newCount = oldCount-1

    if (isLast)
    {
        g_objectGenerations[index]++;
        g_objectSlotsUsed[index] = false;
    }
    else
    {
        g_objectGenerations[index] = movedGen + 1;
        g_objectSlotsUsed[index] = true;
        g_objectGenerations[newCount]++;
        g_objectSlotsUsed[newCount] = false;
    }
}

// Batch removal of objects using handles
void SimulationWrapper::remove_objects(const std::vector<ObjectHandle> &handles)
{
    ensure_initialized();

    std::vector<int> indices;
    indices.reserve(handles.size());
    for (const auto &handle : handles)
        if (is_handle_valid(handle))
            indices.push_back(handle.slot);

    if (indices.empty())
        return;

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::sort(indices.begin(), indices.end(), std::greater<int>());

    for (int idx : indices)
    {
        if (idx < 0 || idx >= Objects::GetNumObjects())
            continue;

        int lastIdx = Objects::GetNumObjects() - 1;
        bool isLast = (idx == lastIdx);

        int oldGen = g_objectGenerations[idx];
        int movedGen = isLast ? 0 : g_objectGenerations[lastIdx];

        Objects::RemoveObject(idx);

        if (isLast)
        {
            g_objectGenerations[idx] = oldGen + 1;
            g_objectSlotsUsed[idx] = false;
        }
        else
        {
            g_objectGenerations[idx] = std::max(oldGen, movedGen) + 1;
            g_objectSlotsUsed[idx] = true;
            g_objectGenerations[lastIdx] = movedGen + 1;
            g_objectSlotsUsed[lastIdx] = false;
        }
    }
}

// Remove object using a handle
void SimulationWrapper::remove_object_handle(const ObjectHandle &handle)
{
    int index = get_slot_index(handle);
    remove_object(index);
}

std::vector<ObjectState> SimulationWrapper::get_all_objects() const
{
    ensure_initialized();
    std::vector<Object> objs;
    Objects::FetchToCPU(m_currentBuffer, objs);
    std::vector<ObjectState> out;
    out.reserve(objs.size());
    for (const auto &p : objs)
        out.push_back(object_to_state(p));
    return out;
}

ObjectState SimulationWrapper::object_to_state(const Object &p) const
{
    ObjectState state{};

    state.x = p.position.x;
    state.y = p.position.y;
    state.z = p.position.z;
    state.vx = p.velocity.x;
    state.vy = p.velocity.y;
    state.vz = p.velocity.z;

    state.mass = p.mass;
    state.charge = p.charge;

    state.qx = p.orientation.x;
    state.qy = p.orientation.y;
    state.qz = p.orientation.z;
    state.qw = p.orientation.w;
    state.spin_x = p.spin.x;
    state.spin_y = p.spin.y;
    state.spin_z = p.spin.z;

    if (p.visualSkinType == -1)
    {
        state.rotation = 0.0f;
        state.angular_velocity = 0.0f;
    }
    else
    {
        state.rotation = 2.0f * std::atan2(p.orientation.z, p.orientation.w);
        state.angular_velocity = p.spin.z;
    }

    state.skin_type = p.visualSkinType;
    state.mesh_id = p.meshID;

    if (p.visualSkinType == 0)
    {
        state.radius = p.visualData.x;
        state.width = p.visualData.x * 2.0f;
        state.height = p.visualData.x * 2.0f;
        state.polygon_sides = 0;
    }
    else if (p.visualSkinType == 1)
    {
        state.width = p.visualData.x;
        state.height = p.visualData.y;
        state.radius = 0.0f;
        state.polygon_sides = 0;
    }
    else if (p.visualSkinType == 2)
    {
        state.radius = p.visualData.x;
        state.width = p.visualData.x * 2.0f;
        state.height = p.visualData.x * 2.0f;
        state.polygon_sides = static_cast<int>(p.visualData.y);
    }

    state.r = p.color.r;
    state.g = p.color.g;
    state.b = p.color.b;
    state.a = p.color.a;

    return state;
}

ObjectState SimulationWrapper::get_object(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        throw std::runtime_error("Object data corrupted");

    return object_to_state(objects[index]);
}

// Follow object by handle
void SimulationWrapper::follow_object(const ObjectHandle &handle, float smoothing)
{
    if (!is_handle_valid(handle))
    {
        throw std::runtime_error("ObjectHandle is invalid (object was removed)");
    }
    follow_object(handle.slot, smoothing);
}

// Clear all objects and reset simulation state
void SimulationWrapper::clear_all()
{
    ensure_initialized();

    // Remove all objects
    while (object_count() > 0)
    {
        remove_object(0);
    }

    // Clear all constraints
    clear_all_constraints();

    // Reset script IDs
    for (int i = 0; i < Objects::MAX_OBJECTS; i++)
    {
        g_objectGenerations[i]++;
        g_objectSlotsUsed[i] = false;
    }
    // Reset simulation time
    m_simulationTime = 0.0f;
    g_simulationTime = 0.0f;

    // Reset camera
    g_camera.Reset();
    g_camera3d.Reset();

    // Reset speed
    m_speed = 1.0f;
}

// Get current number of objects in simulation
int SimulationWrapper::object_count() const
{
    ensure_initialized();
    return Objects::GetNumObjects();
}

// Set rotation angle of an object
void SimulationWrapper::set_rotation(int index, float rotation)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;

    Object &p = objects[index];
    if (p.visualSkinType == -1)
        throw std::runtime_error(
            "set_rotation() is 2D-only; use set_orientation_3d() for 3D objects");

    float h = rotation * 0.5f;
    p.orientation = glm::vec4(0.0f, 0.0f, std::sin(h), std::cos(h));
    Objects::UpdateObjectCPU(index, p);
}

// Batch retrieval of multiple objects' states
std::vector<BatchGetData> SimulationWrapper::batch_get(const std::vector<int> &indices) const
{
    ensure_initialized();

    std::vector<BatchGetData> results;
    results.reserve(indices.size());

    if (indices.empty())
    {
        return results;
    }

    // Fetch ALL objects at once (more efficient than per-object fetches)
    std::vector<Object> allObjects;
    Objects::FetchToCPU(m_currentBuffer, allObjects);

    for (int index : indices)
    {
        if (index < 0 || index >= static_cast<int>(allObjects.size()))
        {
            throw std::runtime_error("Invalid object index in batch_get: " + std::to_string(index));
        }

        const Object &p = allObjects[index];
        BatchGetData data;

        if (p.visualSkinType == -1)
        {
            // 3D object: 2D rotation fields are meaningless, leave at 0
            data.rotation = 0.0f;
            data.angular_velocity = 0.0f;
        }
        else
        {
            data.rotation = 2.0f * std::atan2(p.orientation.z, p.orientation.w);
            data.angular_velocity = p.spin.z;
        }

        // Extract object properties
        data.x = p.position.x;
        data.y = p.position.y;
        data.vx = p.velocity.x;
        data.vy = p.velocity.y;
        data.mass = p.mass;
        data.charge = p.charge;
        data.skin_type = p.visualSkinType;

        // 3D state (populate for all objects; harmless for 2D)
        data.z = p.position.z;
        data.vz = p.velocity.z;
        data.qx = p.orientation.x;
        data.qy = p.orientation.y;
        data.qz = p.orientation.z;
        data.qw = p.orientation.w;
        data.spin_x = p.spin.x;
        data.spin_y = p.spin.y;
        data.spin_z = p.spin.z;
        data.mesh_id = p.meshID;

        // Extract visual properties based on skin type
        if (p.visualSkinType == 0)
        { // CIRCLE
            data.radius = p.visualData.x;
            data.width = p.visualData.x * 2.0f;
            data.height = p.visualData.x * 2.0f;
            data.polygon_sides = 0;
        }
        else if (p.visualSkinType == 1)
        { // RECTANGLE
            data.width = p.visualData.x;
            data.height = p.visualData.y;
            data.radius = 0.0f;
            data.polygon_sides = 0;
        }
        else if (p.visualSkinType == 2)
        { // POLYGON
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
void SimulationWrapper::batch_update(const std::vector<BatchUpdateData> &updates)
{
    ensure_initialized();

    if (updates.empty())
    {
        return;
    }

    // Fetch current objects
    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);

    // Apply all updates
    for (const auto &update : updates)
    {
        int index = update.index;

        if (index < 0 || index >= static_cast<int>(objects.size()))
            throw std::runtime_error(
                "Invalid object index in batch_update: " + std::to_string(index));

        Object &p = objects[index];

        if (p.visualSkinType == -1)
        {
            std::cerr << "[batch_update] skipping 3D object at index " << index
                      << " (BatchUpdateData has no 3D fields; use the *_3d setters)"
                      << std::endl;
            continue;
        }

        p.position = glm::vec4(update.x, update.y, p.position.z, 0.0f);
        p.velocity = glm::vec4(update.vx, update.vy, p.velocity.z, 0.0f);
        p.mass = update.mass;
        p.charge = update.charge;
        p.color = glm::vec4(update.r, update.g, update.b, update.a);

        float half = update.rotation * 0.5f;
        p.orientation = glm::vec4(0.0f, 0.0f, std::sin(half), std::cos(half));
        p.spin = glm::vec4(0.0f, 0.0f, update.angular_velocity, 0.0f);

        if (p.visualSkinType == 0) // CIRCLE
        {
            p.visualData = glm::vec4(update.size, 0.0f, 0.0f, 0.0f);
        }
        else if (p.visualSkinType == 1) // RECTANGLE
        {
            p.visualData = glm::vec4(update.width, update.height, 0.0f, 0.0f);
        }
        else if (p.visualSkinType == 2) // POLYGON
        {
            float sides = (update.polygon_sides > 0)
                              ? static_cast<float>(update.polygon_sides)
                              : p.visualData.y;
            p.visualData = glm::vec4(update.size, sides, 0.0f, 0.0f);
        }

        Objects::UpdateObjectCPU(index, p);
    }

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFlush();
}

// Set angular velocity of an object
float SimulationWrapper::get_angular_velocity(int index) const
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return 0.0f;

    const Object &p = objects[index];
    if (p.visualSkinType == -1)
        throw std::runtime_error(
            "get_angular_velocity() is 2D-only; use get_spin_3d() for 3D objects");
    return p.spin.z;
}

void SimulationWrapper::set_angular_velocity(int index, float angular_velocity)
{
    ensure_initialized();
    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    std::vector<Object> objects;
    Objects::FetchToCPU(m_currentBuffer, objects);
    if (index >= static_cast<int>(objects.size()))
        return;

    Object &p = objects[index];
    if (p.visualSkinType == -1)
        throw std::runtime_error(
            "set_angular_velocity() is 2D-only; use set_spin_3d() for 3D objects");

    p.spin = glm::vec4(p.spin.x, p.spin.y, angular_velocity, 0.0f);
    Objects::UpdateObjectCPU(index, p);
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
        Object &p = objects[index];
        int skinType = p.visualSkinType;

        if (skinType == 1) // RECTANGLE
        {
            p.visualData.x = width;
            p.visualData.y = height;
            Objects::UpdateObjectCPU(index, p);
        }
        else if (skinType == 0 || skinType == 2) // CIRCLE or POLYGON
        {
            // For circles/polygons, width = radius
            p.visualData.x = width; // radius
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
        Object &p = objects[index];
        int skinType = p.visualSkinType;

        if (skinType == 0 || skinType == 2) // CIRCLE or POLYGON
        {
            p.visualData.x = radius;
            Objects::UpdateObjectCPU(index, p);
        }
        else if (skinType == 1) // RECTANGLE
        {
            // For rectangles, set width/height to 2*radius to make it square
            p.visualData.x = radius * 2.0f; // width
            p.visualData.y = radius * 2.0f; // height
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
    if (index >= static_cast<int>(objects.size()))
        return 0.0f;

    const Object &p = objects[index];
    if (p.visualSkinType == -1)
        throw std::runtime_error(
            "get_rotation() is 2D-only; use get_orientation_3d() for 3D objects");
    return 2.0f * std::atan2(p.orientation.z, p.orientation.w);
}

// script JIT system
int SimulationWrapper::register_script(const std::string &source)
{
    return scriptManager.registerScript(source);
}

void SimulationWrapper::set_script(int objectIndex, int scriptId)
{
    ensure_initialized();
    if (objectIndex < 0 || objectIndex >= Objects::GetNumObjects())
    {
        throw std::runtime_error("Invalid object index");
    }
    Objects::SetScriptID(objectIndex, scriptId);
}

// ============================================================================
// EQUATION AND CONSTRAINT FUNCTIONS
// ============================================================================

// Set physics equation for an object
void SimulationWrapper::set_equation(int object_index, const std::string &equation_string)
{
    ensure_initialized();
    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    // Check if this is legacy comma‑separated format (no '=', contains ',')
    bool isLegacy = (equation_string.find('=') == std::string::npos &&
                     equation_string.find(',') != std::string::npos);

    std::string finalEquation = equation_string;
    if (isLegacy)
    {
        // Split by commas
        std::vector<std::string> parts;
        std::stringstream ss(equation_string);
        std::string part;
        while (std::getline(ss, part, ','))
        {
            // Trim whitespace
            part.erase(0, part.find_first_not_of(" \t\n\r"));
            part.erase(part.find_last_not_of(" \t\n\r") + 1);
            parts.push_back(part);
        }
        // Pad to at least 7 parts (ax, ay, angular, r, g, b, a)
        while (parts.size() < 7)
            parts.push_back("0");

        // Map to new‑style assignments
        std::string newStyle;
        newStyle += "ax = " + parts[0] + ";\n";
        newStyle += "ay = " + parts[1] + ";\n";
        newStyle += "angular = " + parts[2] + ";\n";
        newStyle += "color.r = " + parts[3] + ";\n";
        newStyle += "color.g = " + parts[4] + ";\n";
        newStyle += "color.b = " + parts[5] + ";\n";
        newStyle += "color.a = " + parts[6] + ";\n";
        finalEquation = newStyle;
    }

    try
    {
        ParserContext context;
        // Register px, py if needed (not required here)
        ParsedEquation eq = ParseEquation(finalEquation, context);
        Objects::SetEquation(finalEquation, eq, object_index);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Equation parsing failed: " + std::string(e.what()));
    }
}

// Add distance constraint between two objects
void SimulationWrapper::add_distance_constraint(int object_index, const DistanceConstraint &constraint)
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
    c.param1 = constraint.rest_length; // Only distance parameter
    c.param2 = 0.0f;                   // Unused
    c.param3 = 0.0f;                   // Unused
    c.param4 = 0.0f;                   // Unused

    Objects::AddConstraint(object_index, c);
}

// Add boundary (box) constraint to an object
void SimulationWrapper::add_boundary_constraint(int object_index, const BoundaryConstraint &constraint)
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
void SimulationWrapper::set_parameter(const std::string &name, float value)
{
    ensure_initialized();
    if (name == "gravity" || name == "g")
        m_gravity = value;
    else if (name == "damping" || name == "b")
        m_damping = value;
    else if (name == "stiffness" || name == "k")
        m_stiffness = value;
    else
        throw std::runtime_error("Unknown parameter: " + name);
    Objects::SetSystemParameters(m_gravity, m_damping, m_stiffness);
}

// Get current value of a global physics parameter
float SimulationWrapper::get_parameter(const std::string &name) const
{
    ensure_initialized();
    if (name == "gravity" || name == "g")
        return m_gravity;
    if (name == "damping" || name == "b")
        return m_damping;
    if (name == "stiffness" || name == "k")
        return m_stiffness;
    throw std::runtime_error("Unknown parameter: " + name);
}

// ============================================================================
// FILE I/O FUNCTIONS
// ============================================================================

// Save simulation state to file
void SimulationWrapper::save_to_file(
    const std::string &filename,
    const std::string &title,
    const std::string &author,
    const std::string &description)
{
    ensure_initialized();

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));

    FILE *file = fopen(filename.c_str(), "w");
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    fprintf(file, "# Simulation State File\n");
    fprintf(file, "# Created with ProjStellar\n");
    if (!title.empty())
        fprintf(file, "# Title: %s\n", title.c_str());
    if (!author.empty())
        fprintf(file, "# Author: %s\n", author.c_str());
    if (!description.empty())
        fprintf(file, "# Description: %s\n", description.c_str());
    fprintf(file, "# Version: 2.1\n\n");

    fprintf(file, "[SYSTEM_PARAMETERS]\n");
    fprintf(file, "gravity = %.6f\n", get_parameter("gravity"));
    fprintf(file, "damping = %.6f\n", get_parameter("damping"));
    fprintf(file, "stiffness = %.6f\n\n", get_parameter("stiffness"));

    fprintf(file, "[CAMERA]\n");
    fprintf(file, "position = %.6f %.6f\n",
            g_camera.position.x, g_camera.position.y);
    fprintf(file, "zoom = %.6f\n\n", g_camera.zoom);

    fprintf(file, "[OBJECTS]\n");
    int numObjects = object_count();
    fprintf(file, "count = %d\n\n", numObjects);

    std::vector<Object> objects;
    try
    {
        Objects::FetchToCPU(m_currentBuffer, objects);
    }
    catch (const std::exception &e)
    {
        fclose(file);
        throw std::runtime_error("Failed to fetch object data: " + std::string(e.what()));
    }

    for (int i = 0; i < numObjects && i < static_cast<int>(objects.size()); ++i)
    {
        const Object &p = objects[i];
        CollisionProperties props = Objects::GetCollisionProperties(i);

        fprintf(file, "OBJECT %d\n", i);

        fprintf(file, "position3 = %.6f %.6f %.6f\n",
                p.position.x, p.position.y, p.position.z);
        fprintf(file, "velocity3 = %.6f %.6f %.6f\n",
                p.velocity.x, p.velocity.y, p.velocity.z);
        fprintf(file, "orientation = %.6f %.6f %.6f %.6f\n",
                p.orientation.x, p.orientation.y,
                p.orientation.z, p.orientation.w);
        fprintf(file, "spin = %.6f %.6f %.6f\n",
                p.spin.x, p.spin.y, p.spin.z);
        fprintf(file, "visual_data = %.6f %.6f %.6f %.6f\n",
                p.visualData.x, p.visualData.y,
                p.visualData.z, p.visualData.w);

        fprintf(file, "mass = %.6f\n", p.mass);
        fprintf(file, "charge = %.6f\n", p.charge);
        fprintf(file, "skin_type = %d\n", p.visualSkinType);
        fprintf(file, "color = %.6f %.6f %.6f %.6f\n",
                p.color.r, p.color.g, p.color.b, p.color.a);
        fprintf(file, "mesh_id = %d\n", p.meshID);

        fprintf(file, "collision_shape = %d\n", props.shapeType);
        fprintf(file, "collision_enabled = %d\n", props.enabled);
        fprintf(file, "restitution = %.6f\n", props.restitution);
        fprintf(file, "friction = %.6f\n", props.friction);

        fprintf(file, "position = %.6f %.6f\n",
                p.position.x, p.position.y);
        fprintf(file, "velocity = %.6f %.6f\n",
                p.velocity.x, p.velocity.y);
        fprintf(file, "rotation = %.6f\n",
                2.0f * std::atan2(p.orientation.z, p.orientation.w));
        fprintf(file, "angular_velocity = %.6f\n", p.spin.z);

        if (p.visualSkinType == 0)
        {
            fprintf(file, "radius = %.6f\n", p.visualData.x);
        }
        else if (p.visualSkinType == 1)
        {
            fprintf(file, "width  = %.6f\n", p.visualData.x);
            fprintf(file, "height = %.6f\n", p.visualData.y);
        }
        else if (p.visualSkinType == 2)
        {
            fprintf(file, "radius = %.6f\n", p.visualData.x);
            fprintf(file, "sides  = %d\n", static_cast<int>(p.visualData.y));
        }

        fprintf(file, "\n");
    }

    fclose(file);
}

// Load simulation state from file
void SimulationWrapper::load_from_file(const std::string &filename)
{
    ensure_initialized();

    std::ifstream file(filename);
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    clear_all_constraints();
    while (object_count() > 0)
        remove_object(0);
    m_currentBuffer = 0;

    std::string line;
    std::string current_section;

    int current_object_id = -1;
    Object current_object{};
    int current_collision_shape = static_cast<int>(PyCollisionShape::NONE);
    int current_collision_enabled = 1;
    float current_restitution = 0.7f;
    float current_friction = 0.3f;

    bool has_position3 = false;
    bool has_velocity3 = false;
    bool has_orientation = false;
    bool has_spin = false;
    bool has_visual_data = false;
    bool has_mesh_id = false;
    bool has_collision_shape = false;

    float legacy_rotation = 0.0f;
    float legacy_angular_velocity = 0.0f;
    float legacy_radius = 0.0f;
    float legacy_width = 0.0f;
    float legacy_height = 0.0f;
    int legacy_sides = 0;
    int legacy_skin = 0;

    auto reset_accumulator = [&]()
    {
        current_object = Object{};
        current_object.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        current_object.velocity = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        current_object.orientation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        current_object.spin = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        current_object.visualData = glm::vec4(0.0f);
        current_object.color = glm::vec4(1.0f);
        current_object.mass = 1.0f;
        current_object.meshID = -1;
        current_object.collisionShapeType = static_cast<int>(PyCollisionShape::NONE);
        current_object.scriptID = -1;
        current_object.equationID = 0;

        has_position3 = has_velocity3 = has_orientation = false;
        has_spin = has_visual_data = has_mesh_id = has_collision_shape = false;

        legacy_rotation = legacy_angular_velocity = 0.0f;
        legacy_radius = legacy_width = legacy_height = 0.0f;
        legacy_sides = legacy_skin = 0;

        current_collision_shape = static_cast<int>(PyCollisionShape::NONE);
        current_collision_enabled = 1;
        current_restitution = 0.7f;
        current_friction = 0.3f;
    };

    auto emit_object = [&]()
    {
        if (current_object_id < 0)
            return;

        if (!has_orientation)
        {
            float h = legacy_rotation * 0.5f;
            current_object.orientation = glm::vec4(0.0f, 0.0f, std::sin(h), std::cos(h));
        }
        else
        {
            float len = glm::length(current_object.orientation);
            current_object.orientation = (len < 1e-6f)
                                             ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
                                             : current_object.orientation / len;
        }

        if (!has_spin)
            current_object.spin = glm::vec4(0.0f, 0.0f, legacy_angular_velocity, 0.0f);

        if (!has_visual_data)
        {
            if (legacy_skin == 0)
                current_object.visualData = glm::vec4(legacy_radius, 0.0f, 0.0f, 0.0f);
            else if (legacy_skin == 1)
                current_object.visualData = glm::vec4(legacy_width, legacy_height, 0.0f, 0.0f);
            else if (legacy_skin == 2)
                current_object.visualData = glm::vec4(legacy_radius,
                                                      static_cast<float>(legacy_sides),
                                                      0.0f, 0.0f);
        }

        if (!has_collision_shape)
        {
            if (legacy_skin == 0)
                current_collision_shape = static_cast<int>(PyCollisionShape::CIRCLE);
            else if (legacy_skin == 1)
                current_collision_shape = static_cast<int>(PyCollisionShape::AABB);
            else if (legacy_skin == 2)
                current_collision_shape = static_cast<int>(PyCollisionShape::POLYGON);
        }

        current_object.visualSkinType = legacy_skin;
        current_object.collisionShapeType = current_collision_shape;

        if (!has_mesh_id)
            current_object.meshID = -1;

        if (current_object.meshID >= 0 && Meshes::Get(current_object.meshID) == nullptr)
        {
            int fallback = -1;
            if (current_object.visualSkinType == 0)
                fallback = Meshes::RegisterCircle(current_object.visualData.x, 32);
            else if (current_object.visualSkinType == 1)
                fallback = Meshes::RegisterSquare(current_object.visualData.x,
                                                  current_object.visualData.y);
            else if (current_object.visualSkinType == 2)
                fallback = Meshes::RegisterPolygon(current_object.visualData.x,
                                                   static_cast<int>(current_object.visualData.y));
            current_object.meshID = fallback;
        }

        memset(current_object._pad, 0, sizeof(current_object._pad));

        int prev_count = Objects::GetNumObjects();
        Objects::AddObject(current_object.meshID);
        if (Objects::GetNumObjects() == prev_count)
        {
            std::cerr << "[load_from_file] object limit reached; dropping slot "
                      << current_object_id << std::endl;
            return;
        }

        int slot = Objects::GetNumObjects() - 1;
        Objects::UpdateObjectCPU(slot, current_object);

        set_collision_shape(slot, static_cast<PyCollisionShape>(current_collision_shape));
        set_collision_enabled(slot, current_collision_enabled != 0);
        set_collision_properties(slot, current_restitution, current_friction);
    };

    try
    {
        while (getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

            if (current_section == "SYSTEM_PARAMETERS")
            {
                size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                k.erase(0, k.find_first_not_of(" \t"));
                k.erase(k.find_last_not_of(" \t") + 1);
                v.erase(0, v.find_first_not_of(" \t"));
                v.erase(v.find_last_not_of(" \t") + 1);
                try
                {
                    if (k == "gravity" || k == "g" ||
                        k == "damping" || k == "b" ||
                        k == "stiffness" || k == "k")
                        set_parameter(k, std::stof(v));
                }
                catch (const std::exception &)
                {
                    std::cerr << "[load_from_file] bad system parameter: "
                              << line << std::endl;
                }
                continue;
            }

            if (current_section == "CAMERA")
            {
                size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                k.erase(0, k.find_first_not_of(" \t"));
                k.erase(k.find_last_not_of(" \t") + 1);
                v.erase(0, v.find_first_not_of(" \t"));
                v.erase(v.find_last_not_of(" \t") + 1);
                if (k == "position")
                {
                    std::istringstream iss(v);
                    float px, py;
                    if (iss >> px >> py)
                        g_camera.position = {px, py};
                }
                else if (k == "zoom")
                {
                    g_camera.zoom = std::stof(v);
                }
                continue;
            }

            if (current_section != "OBJECTS")
                continue;

            if (line.find("OBJECT") == 0)
            {
                emit_object();

                size_t sp = line.find(' ');
                current_object_id = (sp != std::string::npos)
                                        ? std::stoi(line.substr(sp + 1))
                                        : 0;
                reset_accumulator();
                continue;
            }

            if (current_object_id < 0)
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            k.erase(0, k.find_first_not_of(" \t"));
            k.erase(k.find_last_not_of(" \t") + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            v.erase(v.find_last_not_of(" \t") + 1);

            if (k == "position3")
            {
                std::istringstream iss(v);
                float px, py, pz;
                if (iss >> px >> py >> pz)
                    current_object.position = glm::vec4(px, py, pz, 0.0f);
                has_position3 = true;
            }
            else if (k == "velocity3")
            {
                std::istringstream iss(v);
                float vx, vy, vz;
                if (iss >> vx >> vy >> vz)
                    current_object.velocity = glm::vec4(vx, vy, vz, 0.0f);
                has_velocity3 = true;
            }
            else if (k == "orientation")
            {
                std::istringstream iss(v);
                float qx, qy, qz, qw;
                if (iss >> qx >> qy >> qz >> qw)
                    current_object.orientation = glm::vec4(qx, qy, qz, qw);
                has_orientation = true;
            }
            else if (k == "spin")
            {
                std::istringstream iss(v);
                float sx, sy, sz;
                if (iss >> sx >> sy >> sz)
                    current_object.spin = glm::vec4(sx, sy, sz, 0.0f);
                has_spin = true;
            }
            else if (k == "visual_data")
            {
                std::istringstream iss(v);
                float vx, vy, vz, vw;
                if (iss >> vx >> vy >> vz >> vw)
                    current_object.visualData = glm::vec4(vx, vy, vz, vw);
                has_visual_data = true;
            }
            else if (k == "position")
            {
                std::istringstream iss(v);
                float px, py;
                if (iss >> px >> py)
                {
                    current_object.position.x = px;
                    current_object.position.y = py;
                }
            }
            else if (k == "velocity")
            {
                std::istringstream iss(v);
                float vx, vy;
                if (iss >> vx >> vy)
                {
                    current_object.velocity.x = vx;
                    current_object.velocity.y = vy;
                }
            }
            else if (k == "mass")
            {
                current_object.mass = std::stof(v);
            }
            else if (k == "charge")
            {
                current_object.charge = std::stof(v);
            }
            else if (k == "skin_type")
            {
                legacy_skin = std::stoi(v);
            }
            else if (k == "color")
            {
                std::istringstream iss(v);
                float cr, cg, cb, ca;
                if (iss >> cr >> cg >> cb >> ca)
                    current_object.color = glm::vec4(cr, cg, cb, ca);
            }
            else if (k == "mesh_id")
            {
                current_object.meshID = std::stoi(v);
                has_mesh_id = true;
            }
            else if (k == "collision_shape")
            {
                current_collision_shape = std::stoi(v);
                has_collision_shape = true;
            }
            else if (k == "collision_enabled")
            {
                current_collision_enabled = std::stoi(v);
            }
            else if (k == "restitution")
            {
                current_restitution = std::stof(v);
            }
            else if (k == "friction")
            {
                current_friction = std::stof(v);
            }
            else if (k == "rotation")
            {
                legacy_rotation = std::stof(v);
            }
            else if (k == "angular_velocity")
            {
                legacy_angular_velocity = std::stof(v);
            }
            else if (k == "radius")
            {
                legacy_radius = std::stof(v);
            }
            else if (k == "width")
            {
                legacy_width = std::stof(v);
            }
            else if (k == "height")
            {
                legacy_height = std::stof(v);
            }
            else if (k == "sides")
            {
                legacy_sides = std::stoi(v);
            }
        }

        emit_object();
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Error parsing simulation file: " + std::string(e.what()));
    }
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
    g_camera3d.Reset();
    m_currentBuffer = 0;
}

// Run batch simulations (for parameter studies, optimization, etc.)
void SimulationWrapper::run_batch(
    const std::vector<BatchConfig> &configs,
    std::function<void(int, const std::vector<ObjectState> &)> callback)
{
    ensure_initialized();

    if (!m_headless)
        throw std::runtime_error("Batch mode only available in headless mode");

    // Run each configuration
    for (size_t i = 0; i < configs.size(); ++i)
    {
        const auto &config = configs[i];

        // Reset simulation
        clear_all();
        // Create objects from configuration
        for (const auto &pconfig : config.objects)
        {
            int pid = add_object_raw(
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

            for (const auto &constraint : pconfig.constraints)
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
    const std::string &filename,
    const std::vector<ObjectState> &states)
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
        const auto &s = states[i];
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
        glfwMakeContextCurrent(static_cast<GLFWwindow *>(m_window));

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
// HANDLE IMPLEMENTATION
// ============================================================================

// Generate a new handle for a freshly created object
ObjectHandle SimulationWrapper::make_handle(int slot)
{
    ObjectHandle h;
    h.slot = slot;
    h.generation = g_objectGenerations[slot];
    return h;
}

// Check if a handle is still valid
bool SimulationWrapper::is_handle_valid(const ObjectHandle &handle) const
{
    if (handle.slot < 0 || handle.slot >= Objects::MAX_OBJECTS)
        return false;
    if (!g_objectSlotsUsed[handle.slot])
        return false;
    return g_objectGenerations[handle.slot] == handle.generation;
}

// Get the raw slot index from a handle (throws if invalid)
int SimulationWrapper::get_slot_index(const ObjectHandle &handle) const
{
    if (!is_handle_valid(handle))
    {
        throw std::runtime_error("ObjectHandle is invalid - object was removed or slot was reused");
    }
    return handle.slot;
}

// Get handle for a raw index (returns invalid handle if slot empty)
ObjectHandle SimulationWrapper::get_handle(int raw_index) const
{
    if (raw_index < 0 || raw_index >= Objects::GetNumObjects())
    {
        ObjectHandle invalid;
        invalid.slot = -1;
        invalid.generation = -1;
        return invalid;
    }
    ObjectHandle h;
    h.slot = raw_index;
    h.generation = g_objectGenerations[raw_index];
    return h;
}

// ============================================================================
// COLLISION CALLBACK
// ============================================================================

void SimulationWrapper::set_collision_callback(std::function<void(const CollisionEvent &)> callback)
{
    g_collisionCallback = callback;
}

void SimulationWrapper::clear_collision_callback()
{
    g_collisionCallback = nullptr;
}

// ============================================================================
// SCRATCHPAD ENUMERATION
// ============================================================================

std::vector<int> SimulationWrapper::get_scratchpad_ids() const
{
    ensure_initialized();
    // This requires a function Objects::GetScratchpadIDs() to return keys
    return Objects::GetScratchpadIDs();
}

// ============================================================================
// AGENT MANAGEMENT
// ============================================================================

void SimulationWrapper::unregister_agent(int agent_id)
{
    ensure_initialized();
    scriptManager.unregisterScript(agent_id);
}

void SimulationWrapper::clear_agents()
{
    ensure_initialized();
    auto ids = scriptManager.getAgentIDs();
    for (int id : ids)
    {
        scriptManager.unregisterScript(id);
    }
}

// ============================================================================
// SCREEN↔WORLD CONVERSION
// ============================================================================

std::pair<float, float> SimulationWrapper::screen_to_world(float screen_x, float screen_y) const
{
    ensure_initialized();
    int w = g_width;
    int h = g_height;

    if (w == 0 || h == 0)
        return {0.0f, 0.0f};

    float ndcX = (screen_x / w) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screen_y / h) * 2.0f;

    float aspect = (float)w / (float)h;
    float halfWidth = g_camera.zoom * aspect;
    float halfHeight = g_camera.zoom;

    float worldX = g_camera.position.x + ndcX * halfWidth;
    float worldY = g_camera.position.y + ndcY * halfHeight;

    return {worldX, worldY};
}

std::pair<float, float> SimulationWrapper::world_to_screen(float world_x, float world_y) const
{
    ensure_initialized();
    int w = g_width;
    int h = g_height;

    if (w == 0 || h == 0)
        return {0.0f, 0.0f};

    float aspect = (float)w / (float)h;
    float halfWidth = g_camera.zoom * aspect;
    float halfHeight = g_camera.zoom;

    float ndcX = (world_x - g_camera.position.x) / halfWidth;
    float ndcY = (world_y - g_camera.position.y) / halfHeight;

    float screenX = (ndcX + 1.0f) * 0.5f * w;
    float screenY = (1.0f - ndcY) * 0.5f * h;

    return {screenX, screenY};
}

// ============================================================================
// CAMERA CONVENIENCE FUNCTIONS
// ============================================================================

void SimulationWrapper::fit_camera_to_objects(float padding)
{
    ensure_initialized();

    int count = object_count();
    if (count == 0)
        return;

    // Get all object positions
    std::vector<ObjectState> states = get_all_objects();

    float minX = states[0].x, maxX = states[0].x;
    float minY = states[0].y, maxY = states[0].y;

    for (const auto &s : states)
    {
        minX = std::min(minX, s.x);
        maxX = std::max(maxX, s.x);
        minY = std::min(minY, s.y);
        maxY = std::max(maxY, s.y);
    }

    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;
    float rangeX = (maxX - minX) * (1.0f + padding);
    float rangeY = (maxY - minY) * (1.0f + padding);

    // Set camera position
    g_camera.position.x = centerX;
    g_camera.position.y = centerY;

    // Set zoom to fit both axes
    int w = g_width;
    int h = g_height;
    if (w > 0 && h > 0)
    {
        float aspect = (float)w / (float)h;
        float zoomX = rangeX * 0.5f;
        float zoomY = rangeY * 0.5f;
        g_camera.zoom = std::max(zoomX / aspect, zoomY);
    }
}

void SimulationWrapper::follow_object(int index, float smoothing)
{
    ensure_initialized();

    if (index < 0 || index >= object_count())
        return;

    ObjectState state = get_object(index);

    float dx = state.x - g_camera.position.x;
    float dy = state.y - g_camera.position.y;

    g_camera.position.x += dx * (1.0f - smoothing);
    g_camera.position.y += dy * (1.0f - smoothing);
}

// ============================================================================
// WIDTH/HEIGHT GETTERS
// ============================================================================

int SimulationWrapper::get_width() const { return g_width; }
int SimulationWrapper::get_height() const { return g_height; }

// ============================================================================
// CLEANUP FUNCTION
// ============================================================================

// Clean up all simulation resources
void SimulationWrapper::cleanup()
{
    if (!m_initialized)
        return;

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
    Meshes::Cleanup();

    // Destroy window and terminate GLFW
    if (m_window)
    {
        glfwDestroyWindow(static_cast<GLFWwindow *>(m_window));
        m_window = nullptr;
    }

    glfwTerminate();
    g_activeSimCount.fetch_sub(1);
    m_initialized = false;
}