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

namespace
{
    bool g_axisInitialized = false;
    GLuint g_axisShaderProgram = 0;
}

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

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

SimulationWrapper::SimulationWrapper(bool headless, int width, int height, std::string title)
    : m_headless(headless), m_initialized(false), m_paused(false),
    m_window(nullptr), m_currentBuffer(0), m_title(title),
    m_width(width), m_height(height), m_simulationTime(0.0f)
{
    try
    {
        g_width = width;
        g_height = height;
        g_simulationViewportSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));

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
        cleanup();
        throw;
    }
}

SimulationWrapper::~SimulationWrapper()
{
    cleanup();
}

bool SimulationWrapper::init_headless()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    m_window = glfwCreateWindow(640, 480, "Headless", nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create headless window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    if (!gladLoaderLoadGL())
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    glFlush();
    glFinish();

    while (glGetError() != GL_NO_ERROR);

    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR || major == 0)
    {
        std::cerr << "OpenGL context verification failed (error: " << err << ")" << std::endl;
        return false;
    }

    if (!Objects::Init(m_window))
    {
        std::cerr << "Failed to initialize object system" << std::endl;
        return false;
    }

    m_initialized = true;
    return true;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    SimulationWrapper* sim = static_cast<SimulationWrapper*>(glfwGetWindowUserPointer(window));
    if (sim)
    {
        g_width = width;
        g_height = height;
        g_simulationViewportSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
        glViewport(0, 0, width, height);
    }
}

bool SimulationWrapper::init_windowed(int width, int height, const std::string& title)
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));
    glfwSetWindowUserPointer(static_cast<GLFWwindow*>(m_window), this);
    glfwSetFramebufferSizeCallback(static_cast<GLFWwindow*>(m_window), framebuffer_size_callback);
    glfwSwapInterval(1);

    glfwShowWindow(static_cast<GLFWwindow*>(m_window));
    glfwFocusWindow(static_cast<GLFWwindow*>(m_window));

    for (int i = 0; i < 5; i++)
        glfwPollEvents();

    if (!gladLoaderLoadGL())
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    glFlush();
    glFinish();

    while (glGetError() != GL_NO_ERROR);

    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR || major == 0)
    {
        std::cerr << "OpenGL context verification failed (error: " << err << ")" << std::endl;
        return false;
    }

    g_camera.Reset();

    if (!g_axisInitialized)
    {
        Axis::Init();
        g_axisInitialized = true;

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

    g_axisShaderProgram = CreateAxisShader();
    if (g_axisShaderProgram == 0)
    {
        std::cerr << "Failed to create axis shader!" << std::endl;
        return false;
    }

    if (!Objects::Init(m_window))
    {
        std::cerr << "Failed to initialize object system" << std::endl;
        return false;
    }

    m_initialized = true;
    return true;
}

void SimulationWrapper::ensure_initialized() const
{
    if (!m_initialized)
        throw std::runtime_error("Simulation not initialized");
}

void SimulationWrapper::process_input()
{
    if (!m_headless && m_window)
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window);
        glfwMakeContextCurrent(glfwWindow);

        static bool escWasPressed = false;
        bool escIsPressed = (glfwGetKey(glfwWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS);

        if (escIsPressed && !escWasPressed)
            glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
        escWasPressed = escIsPressed;

        g_camera.ProcessInput(glfwWindow, 0.016f);
    }
}

bool SimulationWrapper::should_close() const
{
    if (!m_headless && m_window)
        return glfwWindowShouldClose(static_cast<GLFWwindow*>(m_window));
    return false;
}

void SimulationWrapper::update(float dt)
{
    ensure_initialized();
    if (m_paused) return;

    m_simulationTime += dt;

    while (glGetError() != GL_NO_ERROR);

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    GLuint computeProgram = Objects::GetComputeProgram();
    if (computeProgram && Objects::IsComputeShaderReady())
    {
        glUseProgram(computeProgram);

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

        if (dtLoc != -1) glUniform1f(dtLoc, dt);
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

        glUseProgram(0);

        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
            std::cerr << "OpenGL error after uniforms: " << err << std::endl;
    }

    Objects::Update(m_currentBuffer, 1 - m_currentBuffer);
    m_currentBuffer = 1 - m_currentBuffer;

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        std::cerr << "OpenGL error after Objects::Update: " << err << std::endl;
}

void SimulationWrapper::render()
{
    if (m_headless || !m_window) return;

    glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    int w, h;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_window), &w, &h);

    if (w <= 0 || h <= 0) return;

    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glm::mat4 projection = g_camera.GetProjectionMatrix(w, h);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-g_camera.position, 0.0f));
    glm::mat4 projView = projection * view;

    while (glGetError() != GL_NO_ERROR);

    if (g_axisInitialized && g_axisShaderProgram)
    {
        glUseProgram(g_axisShaderProgram);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) return;

        Axis::Update(g_camera, w, h);
        Axis::Draw(g_axisShaderProgram, projView);
        glUseProgram(0);
    }

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

        GLint projLoc = glGetUniformLocation(objectProgram, "uProjection");
        GLint viewLoc = glGetUniformLocation(objectProgram, "uView");

        if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

        Objects::Draw(m_currentBuffer);
        glUseProgram(0);
    }

    glfwSwapBuffers(static_cast<GLFWwindow*>(m_window));
    glfwPollEvents();
}

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
    newObject.collisionShapeType = 0; // COLLISION_NONE
    newObject.equationID = 0; // Default equation
    newObject._pad1 = 0;
    newObject.color = glm::vec4(r, g, b, a);
    newObject.collisionData = glm::vec4(0.0f);

    // Set visualData based on skin type
    switch (skin)
    {
    case PySkinType::PY_SKIN_CIRCLE:
        newObject.visualData = glm::vec4(size, 0.0f, rotation, angular_velocity);
        break;
    case PySkinType::PY_SKIN_RECTANGLE:
        newObject.visualData = glm::vec4(width, height, rotation, angular_velocity);
        break;
    case PySkinType::PY_SKIN_POLYGON:
        // Use conditional operators instead of std::min/std::max to avoid Windows issues
        int minPolySides = 3;
        int maxPolySides = 12;
        polygon_sides = (polygon_sides < minPolySides) ? minPolySides :
            ((polygon_sides > maxPolySides) ? maxPolySides : polygon_sides);
        newObject.visualData = glm::vec4(size, static_cast<float>(polygon_sides), rotation, angular_velocity);
        break;
    }

    // Step 2: Allocate slot in Objects system (creates zero-initialized object on GPU after Fix 1)
    Objects::AddObject();
    int objectID = Objects::GetNumObjects() - 1;

    // Step 3: IMMEDIATELY overwrite with correct values
    Objects::UpdateObjectCPU(objectID, newObject);

    // Step 4: CRITICAL - Force GPU to finish all pending operations
    // This ensures correct values are on GPU before Python code continues
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glFlush();

    return objectID;
}

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

        p.position = glm::vec2(x, y);
        p.velocity = glm::vec2(vx, vy);
        p.mass = mass;
        p.charge = charge;
        p.color = glm::vec4(r, g, b, a);

        if (p.visualSkinType == 0)
        {
            p.visualData.x = width;
            p.visualData.z = rotation;
            p.visualData.w = angular_velocity;
        }
        else if (p.visualSkinType == 1)
        {
            p.visualData.x = width;
            p.visualData.y = height;
            p.visualData.z = rotation;
            p.visualData.w = angular_velocity;
        }
        else if (p.visualSkinType == 2)
        {
            p.visualData.x = width;
            p.visualData.z = rotation;
            p.visualData.w = angular_velocity;
        }

        Objects::UpdateObjectCPU(index, p);
    }
}

void SimulationWrapper::remove_object(int index)
{
    ensure_initialized();

    if (index < 0 || index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    Objects::RemoveObject(index);
}

int SimulationWrapper::object_count() const
{
    ensure_initialized();
    return Objects::GetNumObjects();
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

    const Object& p = objects[index];
    ObjectState state;

    state.x = p.position.x;
    state.y = p.position.y;
    state.vx = p.velocity.x;
    state.vy = p.velocity.y;
    state.mass = p.mass;
    state.charge = p.charge;
    state.rotation = p.visualData.z;
    state.angular_velocity = p.visualData.w;
    state.skin_type = p.visualSkinType;

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

        if (p.visualSkinType == 1)
        {
            p.visualData.x = width;
            p.visualData.y = height;
            Objects::UpdateObjectCPU(index, p);
        }
        else
        {
            throw std::runtime_error("set_dimensions only works for RECTANGLE objects");
        }
    }
}

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

        if (p.visualSkinType == 0 || p.visualSkinType == 2)
        {
            p.visualData.x = radius;
            Objects::UpdateObjectCPU(index, p);
        }
        else
        {
            throw std::runtime_error("set_radius only works for CIRCLE or POLYGON objects");
        }
    }
}

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

void SimulationWrapper::set_equation(int object_index, const std::string& equation_string)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    try
    {
        ParserContext context;
        ParsedEquation eq = ParseEquation(equation_string, context);
        Objects::SetEquation(equation_string, eq, object_index);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Equation parsing failed: " + std::string(e.what()));
    }
}

void SimulationWrapper::add_distance_constraint(int object_index, const DistanceConstraint& constraint)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    if (constraint.target_object < 0 || constraint.target_object >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid target object");

    if (object_index == constraint.target_object)
        throw std::runtime_error("Cannot create distance constraint to self");

    Constraint c;
    c.type = CONSTRAINT_DISTANCE;
    c.targetObjectID = constraint.target_object;
    c.param1 = constraint.rest_length;
    c.param2 = constraint.stiffness;

    Objects::AddConstraint(object_index, c);
}

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

void SimulationWrapper::clear_constraints(int object_index)
{
    ensure_initialized();

    if (object_index < 0 || object_index >= Objects::GetNumObjects())
        throw std::runtime_error("Invalid object index");

    Objects::ClearConstraints(object_index);
}

void SimulationWrapper::clear_all_constraints()
{
    ensure_initialized();
    Objects::ClearAllConstraints();
}

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

float SimulationWrapper::get_parameter(const std::string& name) const
{
    ensure_initialized();

    if (name == "gravity" || name == "g") return 9.81f;
    if (name == "damping" || name == "b") return 0.1f;
    if (name == "stiffness" || name == "k") return 1.0f;

    throw std::runtime_error("Unknown parameter: " + name);
}

void SimulationWrapper::save_to_file(
    const std::string& filename,
    const std::string& title,
    const std::string& author,
    const std::string& description)
{
    ensure_initialized();

    if (m_window)
        glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_window));

    FILE* file = fopen(filename.c_str(), "w");
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    fprintf(file, "# Simulation State File\n");
    fprintf(file, "# Created with ProjStellar\n");
    if (!title.empty()) fprintf(file, "# Title: %s\n", title.c_str());
    if (!author.empty()) fprintf(file, "# Author: %s\n", author.c_str());
    if (!description.empty()) fprintf(file, "# Description: %s\n", description.c_str());
    fprintf(file, "# Version: 1.0\n\n");

    fprintf(file, "[SYSTEM_PARAMETERS]\n");
    fprintf(file, "gravity = %.6f\n", get_parameter("gravity"));
    fprintf(file, "damping = %.6f\n", get_parameter("damping"));
    fprintf(file, "stiffness = %.6f\n\n", get_parameter("stiffness"));

    fprintf(file, "[CAMERA]\n");
    fprintf(file, "position = %.6f %.6f\n", g_camera.position.x, g_camera.position.y);
    fprintf(file, "zoom = %.6f\n\n", g_camera.zoom);

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

void SimulationWrapper::load_from_file(const std::string& filename)
{
    ensure_initialized();

    std::ifstream file(filename);
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

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

            if (line.size() > 0 && line[0] == '[' && line[line.size() - 1] == ']')
            {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

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
            else if (current_section == "OBJECTS")
            {
                if (line.find("OBJECT") == 0)
                {
                    if (current_object_id >= 0)
                    {
                        int pid = add_object(
                            current_object.position.x, current_object.position.y,
                            current_object.velocity.x, current_object.velocity.y,
                            current_object.mass, current_object.charge,
                            current_object.visualData.z, current_object.visualData.w,
                            current_skin,
                            current_object.visualData.x,
                            current_object.visualData.x,
                            (current_skin == PySkinType::PY_SKIN_RECTANGLE) ? current_object.visualData.y : current_object.visualData.x,
                            current_object.color.r, current_object.color.g,
                            current_object.color.b, current_object.color.a,
                            (current_skin == PySkinType::PY_SKIN_POLYGON) ? static_cast<int>(current_object.visualData.y) : 0);

                        if (pid != current_object_id)
                            std::cerr << "Object ID mismatch during load" << std::endl;
                    }

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

        if (current_object_id >= 0)
        {
            int pid = add_object(
                current_object.position.x, current_object.position.y,
                current_object.velocity.x, current_object.velocity.y,
                current_object.mass, current_object.charge,
                current_object.visualData.z, current_object.visualData.w,
                current_skin,
                current_object.visualData.x,
                current_object.visualData.x,
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

void SimulationWrapper::set_paused(bool paused)
{
    m_paused = paused;
}

bool SimulationWrapper::is_paused() const
{
    return m_paused;
}

void SimulationWrapper::reset()
{
    ensure_initialized();
    Objects::ResetToInitialConditions();
    m_currentBuffer = 0;
}

void SimulationWrapper::run_batch(
    const std::vector<BatchConfig>& configs,
    std::function<void(int, const std::vector<ObjectState>&)> callback)
{
    ensure_initialized();

    if (!m_headless)
        throw std::runtime_error("Batch mode only available in headless mode");

    for (size_t i = 0; i < configs.size(); ++i)
    {
        const auto& config = configs[i];

        reset();
        clear_all_constraints();

        while (object_count() > 0)
            remove_object(0);

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

            if (!pconfig.equation.empty())
                set_equation(pid, pconfig.equation);

            for (const auto& constraint : pconfig.constraints)
            {
                if (constraint.type == 0)
                {
                    DistanceConstraint dc;
                    dc.target_object = constraint.target;
                    dc.rest_length = constraint.param1;
                    dc.stiffness = constraint.param2;
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

        int steps = static_cast<int>(config.duration / config.dt);
        std::vector<ObjectState> results;

        for (int step = 0; step < steps; ++step)
        {
            update(config.dt);

            if (step == steps - 1)
            {
                results.clear();
                for (int p = 0; p < object_count(); ++p)
                    results.push_back(get_object(p));

                if (callback)
                    callback(static_cast<int>(i), results);
            }
        }

        if (!config.output_file.empty())
            save_results(config.output_file, results);
    }
}

void SimulationWrapper::save_results(
    const std::string& filename,
    const std::vector<ObjectState>& states)
{
    std::ofstream file(filename);
    if (!file)
        throw std::runtime_error("Failed to open output file: " + filename);

    file << "object_id,x,y,vx,vy,mass,charge,rotation,angular_velocity,"
        << "width,height,radius,polygon_sides,skin_type,r,g,b,a\n";

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

bool SimulationWrapper::are_all_shaders_ready() const
{
    ensure_initialized();
    return Objects::AreAllShadersReady();
}

float SimulationWrapper::get_shader_load_progress() const
{
    ensure_initialized();
    return Objects::GetShaderLoadProgress();
}

std::string SimulationWrapper::get_shader_load_status() const
{
    ensure_initialized();
    return Objects::GetShaderLoadStatusMessage();
}

void SimulationWrapper::cleanup()
{
    if (!m_initialized) return;

    if (g_axisInitialized)
    {
        Axis::Cleanup();
        g_axisInitialized = false;
    }

    if (g_axisShaderProgram)
    {
        glDeleteProgram(g_axisShaderProgram);
        g_axisShaderProgram = 0;
    }

    Objects::Cleanup();

    if (m_window)
    {
        glfwDestroyWindow(static_cast<GLFWwindow*>(m_window));
        m_window = nullptr;
    }

    glfwTerminate();
    m_initialized = false;
}