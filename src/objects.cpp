#include "objects.h"
#include "shader_utils.h"
#include "../include/globals.h"
#include "utils.h"
#include "debug_helpers.h"
#include "gpu_serializer.h"
#include "constraints.h"
#include "async_shader_loader.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <atomic>

// Static buffer IDs for double-buffered object data
static GLuint g_objectSSBO[2] = {0, 0};
static GLuint g_renderVAO[2] = {0, 0};
static GLuint g_programCompute = 0;
static GLuint g_programQuad = 0;

// Paint shader resources
static int g_paintWidth = 0;
static int g_paintHeight = 0;

// Paint shader and related resources
static AsyncShaderLoader g_paintLoader;
static bool g_paintShaderReady = false;

// Equation and constraint storage buffers
static GLuint g_allTokensSSBO = 0;
static GLuint g_allConstantsSSBO = 0;
static GLuint g_mappingsSSBO = 0;
static GLuint g_initialPosSSBO = 0;
static GLuint g_constraintsSSBO = 0;
static GLuint g_objectConstraintsSSBO = 0;

// Collision system buffers
static GLuint g_collisionPropsSSBO = 0;
static GLuint g_contactBufferSSBO = 0; //  Contact persistence buffer
static std::vector<CollisionProperties> g_collisionProperties(Objects::MAX_OBJECTS);
static std::vector<std::vector<bool>> g_collisionMatrix(Objects::MAX_OBJECTS,
                                                        std::vector<bool>(Objects::MAX_OBJECTS, true));

//  Collision system parameters
static bool g_enableWarmStart = false;
static int g_maxContactIterations = 3;
static bool g_useAnalyticalCollision = true; // Use analytical elastic collisions

// Object count and data storage
static int g_numObjects = 0;
float g_simulationTime = 0.0f;
static std::vector<int> g_allTokens;
static std::vector<float> g_allConstants;
static std::vector<EquationMapping> g_equationMappings(Objects::MAX_EQUATIONS);
static std::unordered_map<std::string, int> g_equationStringToID;

// Constraint management
static std::vector<Constraint> g_allConstraints;
static std::vector<ObjectConstraints> g_objectConstraintMappings(Objects::MAX_OBJECTS);

// Default system parameters
static int g_currentDefaultObjectType = SKIN_CIRCLE;
static float g_currentSystemGravity = 9.81f;
static float g_currentSystemDamping = 0.1f;
static float g_currentSystemStiffness = 1.0f;

// Memory mapping for direct CPU access
static Object *g_mappedSSBO[2] = {nullptr, nullptr};
static bool g_useMapBuffer = true;

// Async shader loading
static AsyncShaderLoader g_computeLoader;
static AsyncShaderLoader g_quadLoader;
static bool g_computeShaderReady = false;
static bool g_quadShaderReady = false;

static GLuint g_paintShaderProgram = 0;
static GLuint g_paintTexture = 0;
static GLuint g_paintTokenSSBO = 0;
static GLuint g_paintConstSSBO = 0;
static GLuint g_paintQuadVAO = 0;
static GLuint g_paintQuadVBO = 0;
static GLuint g_paintQuadProgram = 0;
static bool g_hasPaintEquation = false;
static std::vector<int> g_paintTokens_r, g_paintTokens_g, g_paintTokens_b;
static std::vector<float> g_paintConsts_r, g_paintConsts_g, g_paintConsts_b;

// Helper function to safely delete buffers
static void SafeDeleteBuffers(GLuint *buf, GLsizei n)
{
    if (n <= 0)
        return;
    std::vector<GLuint> toDelete;
    for (GLsizei i = 0; i < n; ++i)
        if (buf[i])
            toDelete.push_back(buf[i]);
    if (!toDelete.empty())
        glDeleteBuffers(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    for (GLsizei i = 0; i < n; ++i)
        buf[i] = 0;
}

// Helper function to safely delete vertex arrays
static void SafeDeleteVertexArrays(GLuint *arr, GLsizei n)
{
    if (n <= 0)
        return;
    std::vector<GLuint> toDelete;
    for (GLsizei i = 0; i < n; ++i)
        if (arr[i])
            toDelete.push_back(arr[i]);
    if (!toDelete.empty())
        glDeleteVertexArrays(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    for (GLsizei i = 0; i < n; ++i)
        arr[i] = 0;
}

// Clamp a value between min and max
static float clamp(float value, float min, float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// Create a default object with specified skin type
static Object CreateDefaultObjectInternal(int skinType, int objectIndex, int equationID = 0)
{
    Object p;
    p.mass = 1.0f;
    p.charge = 0.0f;
    p.visualSkinType = skinType;
    p.collisionShapeType = COLLISION_NONE;
    p.equationID = equationID;
    p._pad1 = 0;

    // Initialize with zeros - wrapper will set actual values
    p.position = glm::vec2(0.0f, 0.0f);
    p.velocity = glm::vec2(0.0f, 0.0f);
    p.visualData = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    p.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Set defaults based on skin type
    switch (skinType)
    {
    case SKIN_CIRCLE:
        p.visualData = glm::vec4(0.3f, 0.0f, 0.0f, 0.0f);
        p.color = glm::vec4(0.4f, 0.8f, 0.3f, 1.0f);
        break;
    case SKIN_RECTANGLE:
        p.visualData = glm::vec4(0.5f, 0.3f, 0.0f, 0.0f);
        p.color = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
        break;
    case SKIN_POLYGON:
        p.visualData = glm::vec4(0.3f, 6.0f, 0.0f, 0.0f);
        p.color = glm::vec4(0.8f, 0.3f, 0.8f, 1.0f);
        break;
    }

    p.collisionData = glm::vec4(0.0f);
    return p;
}

// Set up VAO to read from SSBO for rendering
static void SetupRenderVAOFromSSBO(GLuint vaoID, GLuint ssboID)
{
    glBindVertexArray(vaoID);
    glBindBuffer(GL_ARRAY_BUFFER, ssboID);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, position));
    glEnableVertexAttribArray(0);

    // Velocity attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, velocity));
    glEnableVertexAttribArray(1);

    // Mass attribute
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, mass));
    glEnableVertexAttribArray(2);

    // Charge attribute
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, charge));
    glEnableVertexAttribArray(3);

    // Skin type attribute
    glVertexAttribIPointer(4, 1, GL_INT, sizeof(Object),
                           (void *)offsetof(Object, visualSkinType));
    glEnableVertexAttribArray(4);

    // Visual data attribute
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, visualData));
    glEnableVertexAttribArray(5);

    // Color attribute
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Object),
                          (void *)offsetof(Object, color));
    glEnableVertexAttribArray(6);

    // Equation ID attribute
    glVertexAttribIPointer(7, 1, GL_INT, sizeof(Object),
                           (void *)offsetof(Object, equationID));
    glEnableVertexAttribArray(7);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Upload packed equation data to GPU
static void UploadPackedEquationsToGPU()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_allTokensSSBO);
    if (!g_allTokens.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     g_allTokens.size() * sizeof(int),
                     g_allTokens.data(),
                     GL_STATIC_DRAW);
    else
    {
        int dummy = 0;
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int), &dummy, GL_STATIC_DRAW);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_allConstantsSSBO);
    if (!g_allConstants.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     g_allConstants.size() * sizeof(float),
                     g_allConstants.data(),
                     GL_STATIC_DRAW);
    else
    {
        float dummy = 0.0f;
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float), &dummy, GL_STATIC_DRAW);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Upload constraint data to GPU
static void UploadConstraintsToGPU()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_constraintsSSBO);
    if (!g_allConstraints.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     g_allConstraints.size() * sizeof(Constraint),
                     g_allConstraints.data(),
                     GL_DYNAMIC_DRAW);
    else
    {
        Constraint dummy = {};
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Constraint), &dummy, GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectConstraintsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 Objects::MAX_OBJECTS * sizeof(ObjectConstraints),
                 g_objectConstraintMappings.data(),
                 GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Initialize contact buffer for warm starting
static void InitializeContactBuffer()
{
    if (g_contactBufferSSBO == 0)
    {
        glGenBuffers(1, &g_contactBufferSSBO);

        // Calculate exact size: MAX_OBJECTS * MAX_CONTACTS_PER_OBJECT * sizeof(ContactPoint)
        // ContactPoint in GLSL: objectA(4), objectB(4), normal(8), position(8), penetration(4),
        // accumulatedNormalImpulse(4), accumulatedTangentImpulse(4), frameCount(4) = 40 bytes
        // With std430 alignment, it pads to 48 bytes. Use 48 to be safe.
        const size_t contactPointSize = 48;
        size_t contactBufferSize = Objects::MAX_OBJECTS * 4 * contactPointSize;

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_contactBufferSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     contactBufferSize,
                     nullptr, // Initialize with zeros
                     GL_DYNAMIC_COPY);

        // Clear buffer to zeros
        void *data = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
        if (data)
        {
            memset(data, 0, contactBufferSize);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

// Clear all contact data (useful for resetting simulation)
static void ClearContactBuffer()
{
    if (g_contactBufferSSBO == 0)
        return;

    const size_t contactPointSize = 48;
    size_t contactBufferSize = Objects::MAX_OBJECTS * 4 * contactPointSize;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_contactBufferSSBO);
    void *data = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
    if (data)
    {
        memset(data, 0, contactBufferSize);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Compact constraint array by removing invalid constraints
void Objects::CompactConstraintArray()
{
    std::vector<Constraint> compacted;
    std::unordered_map<int, int> oldToNewIndex;

    // Build new compacted array and mapping
    int newIndex = 0;
    for (int oldIndex = 0; oldIndex < static_cast<int>(g_allConstraints.size()); oldIndex++)
    {
        if (g_allConstraints[oldIndex].type != -1)
        {
            oldToNewIndex[oldIndex] = newIndex;
            compacted.push_back(g_allConstraints[oldIndex]);
            newIndex++;
        }
    }

    // Update constraint offsets in object mappings
    for (int i = 0; i < MAX_OBJECTS; i++)
    {
        ObjectConstraints &mapping = g_objectConstraintMappings[i];
        if (mapping.numConstraints > 0)
        {
            int oldOffset = mapping.constraintOffset;
            auto it = oldToNewIndex.find(oldOffset);
            if (it != oldToNewIndex.end())
                mapping.constraintOffset = it->second;
            else
            {
                // Invalidate mapping if offset not found
                mapping.objectID = -1;
                mapping.constraintOffset = 0;
                mapping.numConstraints = 0;
            }
        }
    }

    g_allConstraints = compacted;
}

// Add a constraint to an object
void Objects::AddConstraint(int objectIndex, const Constraint &constraint)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
    {
        std::cerr << "[Objects] Invalid object index: " << objectIndex << std::endl;
        return;
    }

    // Validate constraint type-specific parameters
    if (constraint.type == CONSTRAINT_DISTANCE)
    {
        if (constraint.targetObjectID < 0 || constraint.targetObjectID >= g_numObjects)
        {
            std::cerr << "[Objects] Distance constraint has invalid target: "
                      << constraint.targetObjectID << std::endl;
            return;
        }
        if (constraint.targetObjectID == objectIndex)
        {
            std::cerr << "[Objects] Cannot create distance constraint to self!" << std::endl;
            return;
        }

        if (constraint.param1 <= 0.0f) // rest_length must be positive
        {
            std::cerr << "[Objects] Distance constraint has invalid rest length: "
                      << constraint.param1 << std::endl;
            return;
        }
    }
    else if (constraint.type == CONSTRAINT_BOUNDARY)
    {
        float minX = (constraint.param1 < constraint.param2) ? constraint.param1 : constraint.param2;
        float maxX = (constraint.param1 > constraint.param2) ? constraint.param1 : constraint.param2;
        float minY = (constraint.param3 < constraint.param4) ? constraint.param3 : constraint.param4;
        float maxY = (constraint.param3 > constraint.param4) ? constraint.param3 : constraint.param4;

        if (maxX - minX < 0.01f || maxY - minY < 0.01f)
        {
            std::cerr << "[Objects] Boundary constraint has invalid bounds!" << std::endl;
            return;
        }
    }

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];

    // Add constraint to object's constraint list
    if (mapping.numConstraints == 0)
    {
        mapping.objectID = objectIndex;
        mapping.constraintOffset = static_cast<int>(g_allConstraints.size());
        mapping.numConstraints = 1;
        g_allConstraints.push_back(constraint);
    }
    else
    {
        int nextSlot = mapping.constraintOffset + mapping.numConstraints;
        bool canAppend = (nextSlot == static_cast<int>(g_allConstraints.size()));

        if (canAppend)
        {
            g_allConstraints.push_back(constraint);
            mapping.numConstraints++;
        }
        else
        {
            // Need to relocate constraints to maintain contiguous storage
            std::vector<Constraint> existingConstraints;
            for (int i = 0; i < mapping.numConstraints; i++)
                existingConstraints.push_back(g_allConstraints[mapping.constraintOffset + i]);

            // Mark old constraints for removal
            for (int i = 0; i < mapping.numConstraints; i++)
                g_allConstraints[mapping.constraintOffset + i].type = -1;

            // Create new contiguous block
            int newOffset = static_cast<int>(g_allConstraints.size());
            for (const auto &c : existingConstraints)
                g_allConstraints.push_back(c);
            g_allConstraints.push_back(constraint);

            mapping.constraintOffset = newOffset;
            mapping.numConstraints = static_cast<int>(existingConstraints.size()) + 1;
            CompactConstraintArray();
        }
    }

    UploadConstraintsToGPU();
}

// Remove a constraint from an object
void Objects::RemoveConstraint(int objectIndex, int constraintLocalIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (constraintLocalIndex < 0 || constraintLocalIndex >= mapping.numConstraints)
        return;

    // Mark constraint as invalid and shift others
    int globalIndex = mapping.constraintOffset + constraintLocalIndex;
    g_allConstraints[globalIndex].type = -1;

    // Shift remaining constraints down
    for (int i = constraintLocalIndex; i < mapping.numConstraints - 1; i++)
    {
        int srcGlobal = mapping.constraintOffset + i + 1;
        int dstGlobal = mapping.constraintOffset + i;
        g_allConstraints[dstGlobal] = g_allConstraints[srcGlobal];
    }

    // Mark last slot as invalid
    g_allConstraints[mapping.constraintOffset + mapping.numConstraints - 1].type = -1;
    mapping.numConstraints--;

    // Clear mapping if no constraints remain
    if (mapping.numConstraints == 0)
    {
        mapping.objectID = -1;
        mapping.constraintOffset = 0;
    }

    CompactConstraintArray();
    UploadConstraintsToGPU();
}

// Clear all constraints from an object
void Objects::ClearConstraints(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (mapping.numConstraints == 0)
        return;

    // Mark all constraints as invalid
    for (int i = 0; i < mapping.numConstraints; i++)
    {
        int globalIndex = mapping.constraintOffset + i;
        if (globalIndex >= 0 && globalIndex < static_cast<int>(g_allConstraints.size()))
            g_allConstraints[globalIndex].type = -1;
    }

    // Reset object's constraint mapping
    mapping.objectID = -1;
    mapping.constraintOffset = 0;
    mapping.numConstraints = 0;

    CompactConstraintArray();
    UploadConstraintsToGPU();
}

// Clear all constraints from all objects
void Objects::ClearAllConstraints()
{
    for (int i = 0; i < MAX_OBJECTS; i++)
        g_objectConstraintMappings[i] = ObjectConstraints();
    g_allConstraints.clear();
    UploadConstraintsToGPU();
}

// Get all constraints for an object
std::vector<Constraint> Objects::GetConstraints(int objectIndex)
{
    std::vector<Constraint> result;
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return result;

    const ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (mapping.numConstraints == 0)
        return result;

    // Copy constraints to result vector
    for (int i = 0; i < mapping.numConstraints; i++)
        result.push_back(g_allConstraints[mapping.constraintOffset + i]);

    return result;
}

// Update an existing constraint
void Objects::UpdateConstraint(int objectIndex, int constraintLocalIndex, const Constraint &newConstraint)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    ObjectConstraints &mapping = g_objectConstraintMappings[objectIndex];
    if (constraintLocalIndex < 0 || constraintLocalIndex >= mapping.numConstraints)
        return;

    // Validate updated constraint
    if (newConstraint.type == CONSTRAINT_DISTANCE)
    {
        if (newConstraint.targetObjectID < 0 || newConstraint.targetObjectID >= g_numObjects)
        {
            std::cerr << "[Objects] Updated constraint has invalid target!" << std::endl;
            return;
        }
        if (newConstraint.targetObjectID == objectIndex)
        {
            std::cerr << "[Objects] Cannot target self!" << std::endl;
            return;
        }
    }

    // Update constraint data
    int globalIndex = mapping.constraintOffset + constraintLocalIndex;
    g_allConstraints[globalIndex] = newConstraint;
    UploadConstraintsToGPU();
}

// ============================================================================
//  COLLISION PARAMETER MANAGEMENT
// ============================================================================

void Objects::SetCollisionParameters(bool enableWarmStart, int maxContactIterations)
{
    g_enableWarmStart = enableWarmStart;
    g_maxContactIterations = clamp(maxContactIterations, 1, 20);

    // Initialize contact buffer if warm starting is enabled
    if (g_enableWarmStart && g_contactBufferSSBO == 0)
    {
        InitializeContactBuffer();
    }

    // Set uniform values when compute shader is ready
    if (g_programCompute && g_computeShaderReady)
    {
        glUseProgram(g_programCompute);

        GLint enableWarmStartLoc = glGetUniformLocation(g_programCompute, "uEnableWarmStart");
        if (enableWarmStartLoc != -1)
            glUniform1i(enableWarmStartLoc, g_enableWarmStart ? 1 : 0);

        GLint maxContactIterationsLoc = glGetUniformLocation(g_programCompute, "uMaxContactIterations");
        if (maxContactIterationsLoc != -1)
            glUniform1i(maxContactIterationsLoc, g_maxContactIterations);

        glUseProgram(0);
    }
}

void Objects::GetCollisionParameters(bool &enableWarmStart, int &maxContactIterations)
{
    enableWarmStart = g_enableWarmStart;
    maxContactIterations = g_maxContactIterations;
}

// ============================================================================
// Initialize the objects system
// ============================================================================
bool Objects::Init(void *glfwWindow)
{
    // Check OpenGL state
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        return false;

    // Test OpenGL buffer creation
    GLuint testBuffer;
    glGenBuffers(1, &testBuffer);
    if (glGetError() != GL_NO_ERROR)
        return false;
    glDeleteBuffers(1, &testBuffer);

    // Initialize data structures
    g_equationMappings.resize(MAX_EQUATIONS);
    g_objectConstraintMappings.resize(MAX_OBJECTS);

    for (auto &mapping : g_equationMappings)
        mapping = EquationMapping{};

    for (auto &mapping : g_objectConstraintMappings)
        mapping = ObjectConstraints{};

    // Create default equation
    ParserContext context;
    ParsedEquation defaultEq = ParseEquation("vx, vy, -k*x/mass, -k*y/mass, 0, 1, 0, 0, 1", context);
    int defaultEqID = AddOrGetEquation("default_zero", defaultEq);
    g_numObjects = 1;

    // Create double-buffered SSBOs for objects
    if (g_objectSSBO[0] == 0)
    {
        while (glGetError() != GL_NO_ERROR)
            ;
        glGenBuffers(2, g_objectSSBO);
        err = glGetError();
        if (err != GL_NO_ERROR)
            return false;

        // Allocate buffer memory
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         MAX_OBJECTS * sizeof(Object),
                         nullptr,
                         GL_DYNAMIC_COPY);
            err = glGetError();
            if (err != GL_NO_ERROR)
                return false;
        }
        g_useMapBuffer = false;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // Initialize with default object
        Object defaultObject = CreateDefaultObjectInternal(SKIN_CIRCLE, 0, defaultEqID);
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Object), &defaultObject);
            err = glGetError();
            if (err != GL_NO_ERROR)
                return false;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Create VAOs for rendering
    if (g_renderVAO[0] == 0)
    {
        glGenVertexArrays(2, g_renderVAO);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
            return false;

        SetupRenderVAOFromSSBO(g_renderVAO[0], g_objectSSBO[0]);
        SetupRenderVAOFromSSBO(g_renderVAO[1], g_objectSSBO[1]);
    }

    // Create additional SSBOs
    if (g_allTokensSSBO == 0)
        glGenBuffers(1, &g_allTokensSSBO);
    if (g_allConstantsSSBO == 0)
        glGenBuffers(1, &g_allConstantsSSBO);
    if (g_mappingsSSBO == 0)
        glGenBuffers(1, &g_mappingsSSBO);
    if (g_initialPosSSBO == 0)
        glGenBuffers(1, &g_initialPosSSBO);
    if (g_constraintsSSBO == 0)
        glGenBuffers(1, &g_constraintsSSBO);
    if (g_objectConstraintsSSBO == 0)
        glGenBuffers(1, &g_objectConstraintsSSBO);

    // Initialize collision properties buffer
    if (g_collisionPropsSSBO == 0)
    {
        glGenBuffers(1, &g_collisionPropsSSBO);

        // Initialize collision properties with defaults
        for (auto &prop : g_collisionProperties)
        {
            prop.enabled = 1; // Collisions enabled by default
            prop.shapeType = COLLISION_NONE;
            prop.restitution = 0.7f; // Default bounciness
            prop.friction = 0.3f;    // Default friction
            prop.mass_factor = 1.0f;
            prop._pad1 = prop._pad2 = prop._pad3 = 0;
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     MAX_OBJECTS * sizeof(CollisionProperties),
                     g_collisionProperties.data(),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Initialize contact buffer (will be created when needed)
    // g_contactBufferSSBO is initialized lazily when warm starting is enabled

    err = glGetError();
    if (err != GL_NO_ERROR)
        return false;

    // Upload initial data to GPU
    UploadPackedEquationsToGPU();
    UploadConstraintsToGPU();

    // Upload equation mappings
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_mappingsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 MAX_EQUATIONS * sizeof(EquationMapping),
                 g_equationMappings.data(),
                 GL_DYNAMIC_DRAW);
    err = glGetError();
    if (err != GL_NO_ERROR)
        return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Load compute shader asynchronously
    if (g_programCompute == 0)
    {
        g_computeLoader.LoadComputeShaderAsync(
            "math.comp",
            [](GLuint program)
            {
                g_programCompute = program;
                g_computeShaderReady = true;

                // Set initial collision parameters when shader is loaded
                if (g_programCompute)
                {
                    glUseProgram(g_programCompute);

                    GLint enableWarmStartLoc = glGetUniformLocation(g_programCompute, "uEnableWarmStart");
                    if (enableWarmStartLoc != -1)
                        glUniform1i(enableWarmStartLoc, g_enableWarmStart ? 1 : 0);

                    GLint maxContactIterationsLoc = glGetUniformLocation(g_programCompute, "uMaxContactIterations");
                    if (maxContactIterationsLoc != -1)
                        glUniform1i(maxContactIterationsLoc, g_maxContactIterations);

                    glUseProgram(0);
                }
            },
            [](const std::string &error)
            {
                std::cerr << "\n[Objects] Compute shader FAILED: " << error << std::endl;
                g_computeShaderReady = false;
            });
    }

    // Load quad rendering shader asynchronously
    if (g_programQuad == 0)
    {
        g_quadLoader.LoadGraphicsShaderAsync(
            "quad.vert",
            "quad.frag",
            "quad.geom",
            [](GLuint program)
            {
                g_programQuad = program;
                g_quadShaderReady = true;
            },
            [](const std::string &error)
            {
                std::cerr << "\n[Objects] Quad shader FAILED: " << error << std::endl;
                g_quadShaderReady = false;
            });
    }
    InitPaintShader(g_width, g_height);
    return true;
}

// ============================================================================
// Update object physics using compute shader
// ============================================================================
void Objects::Update(int inputIndex, int outputIndex)
{
    UpdateShaderLoadingStatus();

    if (g_programCompute == 0 || !g_computeShaderReady)
        return;

    // Clear OpenGL errors
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
        ;

    // Validate compute shader program
    GLint isProgram = glIsProgram(g_programCompute);
    if (!isProgram)
        return;

    GLint linkStatus;
    glGetProgramiv(g_programCompute, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
        return;

    // Bind compute shader program
    glUseProgram(g_programCompute);
    err = glGetError();
    if (err != GL_NO_ERROR)
        return;

    // Set uniforms
    GLint equationModeLoc = glGetUniformLocation(g_programCompute, "uEquationMode");
    if (equationModeLoc != -1)
        glUniform1i(equationModeLoc, 0);

    GLint numObjectsLoc = glGetUniformLocation(g_programCompute, "uNumObjects");
    if (numObjectsLoc != -1)
        glUniform1i(numObjectsLoc, g_numObjects);

    //  Set collision system parameters
    GLint enableWarmStartLoc = glGetUniformLocation(g_programCompute, "uEnableWarmStart");
    if (enableWarmStartLoc != -1)
        glUniform1i(enableWarmStartLoc, g_enableWarmStart ? 1 : 0);

    GLint maxContactIterationsLoc = glGetUniformLocation(g_programCompute, "uMaxContactIterations");
    if (maxContactIterationsLoc != -1)
        glUniform1i(maxContactIterationsLoc, g_maxContactIterations);

    // Bind SSBOs for compute shader
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[inputIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_objectSSBO[outputIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_allTokensSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, g_allConstantsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, g_mappingsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, g_constraintsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, g_objectConstraintsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, g_collisionPropsSSBO);

    //  Bind contact buffer if warm starting is enabled
    if (g_enableWarmStart)
    {
        if (g_contactBufferSSBO == 0)
        {
            InitializeContactBuffer();
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, g_contactBufferSSBO);
    }

    // Dispatch compute shader
    int workGroupSize = 64;
    int numWorkGroups = (g_numObjects + workGroupSize - 1) / workGroupSize;
    if (numWorkGroups < 1)
        numWorkGroups = 1;

    glDispatchCompute(numWorkGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    // Clean up bindings
    glUseProgram(0);
    for (int i = 0; i < 8; i++)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, 0);
}

// ============================================================================
// Add or retrieve equation ID for a given equation string
// ============================================================================
int Objects::AddOrGetEquation(const std::string &equationString, const ParsedEquation &eq)
{
    // Return existing ID if equation already registered
    auto it = g_equationStringToID.find(equationString);
    if (it != g_equationStringToID.end())
        return it->second;

    // Serialize equation for GPU
    ParserContext context;
    GPUSerializedEquation gpu_eq = serializeEquationForGPU(eq);

    // Find available equation slot
    int newID = -1;
    for (int i = 0; i < MAX_EQUATIONS; i++)
    {
        if (g_equationMappings[i].tokenCount_ax == 0 &&
            g_equationMappings[i].tokenCount_ay == 0 &&
            g_equationMappings[i].tokenCount_angular == 0)
        {
            newID = i;
            break;
        }
    }

    if (newID == -1)
    {
        std::cerr << "[Objects] ERROR: Max equations reached!" << std::endl;
        return 0;
    }

    // Calculate offsets for equation components
    int currentTokenOffset = static_cast<int>(g_allTokens.size());
    int currentConstantOffset = static_cast<int>(g_allConstants.size());

    EquationMapping mapping;
    mapping.tokenOffset_ax = currentTokenOffset;
    mapping.tokenCount_ax = static_cast<int>(gpu_eq.tokenBuffer_ax.size());
    mapping.constantOffset_ax = currentConstantOffset;
    mapping._pad1 = 0;

    mapping.tokenOffset_ay = currentTokenOffset + mapping.tokenCount_ax;
    mapping.tokenCount_ay = static_cast<int>(gpu_eq.tokenBuffer_ay.size());
    mapping.constantOffset_ay = currentConstantOffset + static_cast<int>(gpu_eq.constantBuffer_ax.size());
    mapping._pad2 = 0;

    mapping.tokenOffset_angular = mapping.tokenOffset_ay + mapping.tokenCount_ay;
    mapping.tokenCount_angular = static_cast<int>(gpu_eq.tokenBuffer_angular.size());
    mapping.constantOffset_angular = mapping.constantOffset_ay + static_cast<int>(gpu_eq.constantBuffer_ay.size());
    mapping._pad3 = 0;

    mapping.tokenOffset_r = mapping.tokenOffset_angular + mapping.tokenCount_angular;
    mapping.tokenCount_r = static_cast<int>(gpu_eq.tokenBuffer_r.size());
    mapping.constantOffset_r = mapping.constantOffset_angular + static_cast<int>(gpu_eq.constantBuffer_angular.size());
    mapping._pad4 = 0;

    mapping.tokenOffset_g = mapping.tokenOffset_r + mapping.tokenCount_r;
    mapping.tokenCount_g = static_cast<int>(gpu_eq.tokenBuffer_g.size());
    mapping.constantOffset_g = mapping.constantOffset_r + static_cast<int>(gpu_eq.constantBuffer_r.size());
    mapping._pad5 = 0;

    mapping.tokenOffset_b = mapping.tokenOffset_g + mapping.tokenCount_g;
    mapping.tokenCount_b = static_cast<int>(gpu_eq.tokenBuffer_b.size());
    mapping.constantOffset_b = mapping.constantOffset_g + static_cast<int>(gpu_eq.constantBuffer_g.size());
    mapping._pad6 = 0;

    mapping.tokenOffset_a = mapping.tokenOffset_b + mapping.tokenCount_b;
    mapping.tokenCount_a = static_cast<int>(gpu_eq.tokenBuffer_a.size());
    mapping.constantOffset_a = mapping.constantOffset_b + static_cast<int>(gpu_eq.constantBuffer_b.size());
    mapping._pad7 = 0;

    // Store mapping
    g_equationMappings[newID] = mapping;
    g_equationStringToID[equationString] = newID;

    // Append tokens and constants
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_ax.begin(), gpu_eq.tokenBuffer_ax.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_ay.begin(), gpu_eq.tokenBuffer_ay.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_angular.begin(), gpu_eq.tokenBuffer_angular.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_r.begin(), gpu_eq.tokenBuffer_r.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_g.begin(), gpu_eq.tokenBuffer_g.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_b.begin(), gpu_eq.tokenBuffer_b.end());
    g_allTokens.insert(g_allTokens.end(), gpu_eq.tokenBuffer_a.begin(), gpu_eq.tokenBuffer_a.end());

    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_ax.begin(), gpu_eq.constantBuffer_ax.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_ay.begin(), gpu_eq.constantBuffer_ay.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_angular.begin(), gpu_eq.constantBuffer_angular.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_r.begin(), gpu_eq.constantBuffer_r.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_g.begin(), gpu_eq.constantBuffer_g.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_b.begin(), gpu_eq.constantBuffer_b.end());
    g_allConstants.insert(g_allConstants.end(), gpu_eq.constantBuffer_a.begin(), gpu_eq.constantBuffer_a.end());

    // Update GPU data
    UploadPackedEquationsToGPU();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_mappingsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    MAX_EQUATIONS * sizeof(EquationMapping),
                    g_equationMappings.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return newID;
}

// ============================================================================
// Set equation for an object
// ============================================================================
void Objects::SetEquation(const std::string &equationString, const ParsedEquation &eq, int objectIndex)
{
    int eqID = AddOrGetEquation(equationString, eq);

    if (objectIndex >= 0 && objectIndex < g_numObjects)
    {
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            // Update via mapped memory
            g_mappedSSBO[0][objectIndex].equationID = eqID;
            g_mappedSSBO[1][objectIndex].equationID = eqID;
        }
        else
        {
            // Update via buffer subdata
            for (int i = 0; i < 2; i++)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                                objectIndex * sizeof(Object) + offsetof(Object, equationID),
                                sizeof(int), &eqID);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }
}

// ============================================================================
// Upload CPU data to GPU (compatibility function)
// ============================================================================
void Objects::UploadCpuDataToGpu()
{
    // NO-OP for compatibility
}

// ============================================================================
// Fetch object data from GPU to CPU
// ============================================================================
void Objects::FetchToCPU(int sourceIndex, std::vector<Object> &out)
{
    out.resize(g_numObjects);
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex])
        std::memcpy(out.data(), g_mappedSSBO[sourceIndex], g_numObjects * sizeof(Object));
    else
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[sourceIndex]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           g_numObjects * sizeof(Object),
                           out.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

// ============================================================================
// Draw all objects
// ============================================================================
void Objects::Draw(int sourceIndex)
{
    if (!g_programQuad)
        return;

    glUseProgram(g_programQuad);
    glBindVertexArray(g_renderVAO[sourceIndex]);
    glDrawArrays(GL_POINTS, 0, g_numObjects);
    glBindVertexArray(0);
    glUseProgram(0);
}

// ============================================================================
// Add a new object with default properties
// ============================================================================
void Objects::AddObject()
{
    if (g_numObjects >= MAX_OBJECTS)
    {
        std::cerr << "[Objects::AddObject] Max objects reached!" << std::endl;
        return;
    }

    // Create default object
    Object newObject = CreateDefaultObjectInternal(
        g_currentDefaultObjectType,
        g_numObjects,
        0);

    // Store object data
    if (g_useMapBuffer && g_mappedSSBO[0])
    {
        g_mappedSSBO[0][g_numObjects] = newObject;
        g_mappedSSBO[1][g_numObjects] = newObject;
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            g_numObjects * sizeof(Object),
                            sizeof(Object),
                            &newObject);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Initialize empty constraint mapping
    g_objectConstraintMappings[g_numObjects] = ObjectConstraints();
    g_numObjects++;
}

// ============================================================================
// Upload multiple objects at once
// ============================================================================
void Objects::UploadBulkObjects(const std::vector<Object> &objects, int startIndex)
{
    if (startIndex < 0 || startIndex + static_cast<int>(objects.size()) > MAX_OBJECTS)
    {
        std::cerr << "[Objects::UploadBulkObjects] Invalid range!" << std::endl;
        return;
    }

    // Copy objects to GPU buffers
    if (g_useMapBuffer && g_mappedSSBO[0])
    {
        std::memcpy(&g_mappedSSBO[0][startIndex], objects.data(),
                    objects.size() * sizeof(Object));
        std::memcpy(&g_mappedSSBO[1][startIndex], objects.data(),
                    objects.size() * sizeof(Object));
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            startIndex * sizeof(Object),
                            objects.size() * sizeof(Object),
                            objects.data());
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Update object count
    if (startIndex + static_cast<int>(objects.size()) > g_numObjects)
        g_numObjects = startIndex + static_cast<int>(objects.size());
}

// ============================================================================
// Get direct pointer to object data (read-only)
// ============================================================================
const Object *Objects::GetObjectDataDirect(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex > 1)
        return nullptr;
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex])
        return g_mappedSSBO[sourceIndex];
    return nullptr;
}

// ============================================================================
// Get direct pointer to object data (mutable)
// ============================================================================
Object *Objects::GetObjectDataDirectMutable(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex > 1)
        return nullptr;
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex])
        return g_mappedSSBO[sourceIndex];
    return nullptr;
}

// ============================================================================
// Remove an object from the system
// ============================================================================
void Objects::RemoveObject(int index)
{
    if (g_numObjects == 0)
    {
        std::cerr << "[Objects::RemoveObject] No objects to remove!" << std::endl;
        return;
    }

    // Determine which index to remove
    int removeIdx = (index >= 0 && index < g_numObjects) ? index : (g_numObjects - 1);
    ClearConstraints(removeIdx);

    // Remove constraints that reference the removed object
    for (int i = 0; i < g_numObjects; i++)
    {
        if (i == removeIdx)
            continue;

        ObjectConstraints &pc = g_objectConstraintMappings[i];
        for (int j = pc.numConstraints - 1; j >= 0; j--)
        {
            int globalIdx = pc.constraintOffset + j;
            Constraint &c = g_allConstraints[globalIdx];
            if (c.type == CONSTRAINT_DISTANCE && c.targetObjectID == removeIdx)
                RemoveConstraint(i, j);
        }
    }

    // If removing the last object, just decrement count
    if (removeIdx == g_numObjects - 1)
    {
        g_objectConstraintMappings[removeIdx] = ObjectConstraints();
        g_numObjects--;
        UploadConstraintsToGPU();
        return;
    }

    // Swap with last object and decrement count
    int lastObjectIdx = g_numObjects - 1;

    if (g_useMapBuffer && g_mappedSSBO[0])
    {
        g_mappedSSBO[0][removeIdx] = g_mappedSSBO[0][lastObjectIdx];
        g_mappedSSBO[1][removeIdx] = g_mappedSSBO[1][lastObjectIdx];
    }
    else
    {
        Object lastObject;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[0]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                           lastObjectIdx * sizeof(Object),
                           sizeof(Object),
                           &lastObject);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            removeIdx * sizeof(Object),
                            sizeof(Object),
                            &lastObject);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Update constraint mappings
    g_objectConstraintMappings[removeIdx] = g_objectConstraintMappings[lastObjectIdx];
    if (g_objectConstraintMappings[removeIdx].objectID == lastObjectIdx)
        g_objectConstraintMappings[removeIdx].objectID = removeIdx;

    // Update constraint references to the moved object
    for (int i = 0; i < g_numObjects - 1; i++)
    {
        ObjectConstraints &pc = g_objectConstraintMappings[i];
        for (int j = 0; j < pc.numConstraints; j++)
        {
            int globalIdx = pc.constraintOffset + j;
            Constraint &c = g_allConstraints[globalIdx];
            if (c.type == CONSTRAINT_DISTANCE && c.targetObjectID == lastObjectIdx)
                c.targetObjectID = removeIdx;
        }
    }

    // Clear last slot and decrement count
    g_objectConstraintMappings[lastObjectIdx] = ObjectConstraints();
    g_numObjects--;
    UploadConstraintsToGPU();
}

// ============================================================================
// Reset all objects to initial conditions
// ============================================================================
void Objects::ResetToInitialConditions()
{
    for (int i = 0; i < g_numObjects; i++)
    {
        // Preserve equation ID
        int preservedEqID = 0;
        if (g_useMapBuffer && g_mappedSSBO[0])
            preservedEqID = g_mappedSSBO[0][i].equationID;
        else
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[0]);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                               i * sizeof(Object) + offsetof(Object, equationID),
                               sizeof(int),
                               &preservedEqID);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // Create reset object with preserved equation ID
        Object resetObject = CreateDefaultObjectInternal(
            g_currentDefaultObjectType,
            i,
            preservedEqID);

        // Update object data
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            g_mappedSSBO[0][i] = resetObject;
            g_mappedSSBO[1][i] = resetObject;
        }
        else
        {
            for (int j = 0; j < 2; j++)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[j]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                                i * sizeof(Object),
                                sizeof(Object),
                                &resetObject);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }

    //  Clear contact buffer to remove stale warm start data
    ClearContactBuffer();
}

// ============================================================================
// Update a single object's data from CPU
// ============================================================================
void Objects::UpdateObjectCPU(int index, const Object &newData)
{
    if (index >= 0 && index < g_numObjects)
    {
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            g_mappedSSBO[0][index] = newData;
            g_mappedSSBO[1][index] = newData;
        }
        else
        {
            for (int i = 0; i < 2; i++)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                                index * sizeof(Object),
                                sizeof(Object),
                                &newData);
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
    }
}

// ============================================================================
// Set default object type for new objects
// ============================================================================
void Objects::SetDefaultObjectType(int type)
{
    g_currentDefaultObjectType = type;
}

// ============================================================================
// Set global system parameters
// ============================================================================
void Objects::SetSystemParameters(float gravity, float damping, float stiffness)
{
    g_currentSystemGravity = gravity;
    g_currentSystemDamping = damping;
    g_currentSystemStiffness = stiffness;
}

// ============================================================================
// Get quad rendering shader program ID
// ============================================================================
GLuint Objects::GetQuadProgram()
{
    return g_programQuad;
}

// ============================================================================
// Get compute shader program ID
// ============================================================================
GLuint Objects::GetComputeProgram()
{
    return g_programCompute;
}

// ============================================================================
// Get current number of objects
// ============================================================================
int Objects::GetNumObjects()
{
    return g_numObjects;
}

// ============================================================================
// Clean up all resources
// ============================================================================
void Objects::Cleanup()
{
    // Unmap buffers if mapped
    if (g_useMapBuffer)
    {
        for (int i = 0; i < 2; i++)
        {
            if (g_mappedSSBO[i])
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
                glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                g_mappedSSBO[i] = nullptr;
            }
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Delete shader programs
    glDeleteProgram(g_programCompute);
    glDeleteProgram(g_programQuad);
    g_programCompute = 0;
    g_programQuad = 0;

    g_computeShaderReady = false;
    g_quadShaderReady = false;

    // Delete all buffers and VAOs
    SafeDeleteBuffers(g_objectSSBO, 2);
    SafeDeleteVertexArrays(g_renderVAO, 2);

    SafeDeleteBuffers(&g_allTokensSSBO, 1);
    SafeDeleteBuffers(&g_allConstantsSSBO, 1);
    SafeDeleteBuffers(&g_mappingsSSBO, 1);
    SafeDeleteBuffers(&g_initialPosSSBO, 1);
    SafeDeleteBuffers(&g_constraintsSSBO, 1);
    SafeDeleteBuffers(&g_objectConstraintsSSBO, 1);
    SafeDeleteBuffers(&g_collisionPropsSSBO, 1);
    SafeDeleteBuffers(&g_contactBufferSSBO, 1); //  Delete contact buffer

    // Clear all data structures
    g_numObjects = 0;
    g_allTokens.clear();
    g_allConstants.clear();
    g_equationMappings.clear();
    g_equationStringToID.clear();
    g_allConstraints.clear();
    g_objectConstraintMappings.clear();
    g_collisionProperties.clear();
    g_collisionMatrix.clear();

    // Reset collision parameters
    g_enableWarmStart = false;
    g_maxContactIterations = 3;
    g_useAnalyticalCollision = true;
}

// ============================================================================
// Initialize paint shader and related resources
// ============================================================================
void Objects::InitPaintShader(int screenWidth, int screenHeight)
{
    // Determine texture size (user‑set resolution or fallback to window size)
    int texW = (g_paintWidth > 0) ? g_paintWidth : screenWidth;
    int texH = (g_paintHeight > 0) ? g_paintHeight : screenHeight;

    // Load paint.comp asynchronously
    g_paintLoader.LoadComputeShaderAsync(
        "paint.comp",
        [](GLuint program)
        {
            g_paintShaderProgram = program;
            g_paintShaderReady = true;
            std::cout << "[AsyncLoader] Paint shader ready (ID: " << program << ")" << std::endl;
        },
        [](const std::string &error)
        {
            std::cerr << "[AsyncLoader] Paint shader FAILED: " << error << std::endl;
            g_paintShaderReady = false;
        });

    // Create paint texture with chosen resolution
    glGenTextures(1, &g_paintTexture);
    glBindTexture(GL_TEXTURE_2D, g_paintTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, texW, texH);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create SSBOs for paint tokens and constants
    glGenBuffers(1, &g_paintTokenSSBO);
    glGenBuffers(1, &g_paintConstSSBO);

    // Create fullscreen quad VAO and VBO
    float verts[] = {
        -1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1,
        -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    glGenVertexArrays(1, &g_paintQuadVAO);
    glGenBuffers(1, &g_paintQuadVBO);
    glBindVertexArray(g_paintQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_paintQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Simple quad shader (draws the paint texture)
    const char *quadVert = R"(
        #version 430 core
        layout(location=0) in vec2 pos;
        layout(location=1) in vec2 uv;
        out vec2 vUV;
        void main() { vUV = uv; gl_Position = vec4(pos, 0, 1); }
    )";
    const char *quadFrag = R"(
        #version 430 core
        in vec2 vUV;
        out vec4 FragColor;
        uniform sampler2D uTex;
        void main() { FragColor = texture(uTex, vUV); }
    )";
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &quadVert, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &quadFrag, nullptr);
    glCompileShader(fs);
    g_paintQuadProgram = glCreateProgram();
    glAttachShader(g_paintQuadProgram, vs);
    glAttachShader(g_paintQuadProgram, fs);
    glLinkProgram(g_paintQuadProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void Objects::ResizePaintTexture(int width, int height)
{
    if (g_paintTexture)
    {
        glDeleteTextures(1, &g_paintTexture);
        g_paintTexture = 0;
    }
    glGenTextures(1, &g_paintTexture);
    glBindTexture(GL_TEXTURE_2D, g_paintTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Objects::SetPaintResolution(int width, int height)
{
    g_paintWidth = width;
    g_paintHeight = height;
    if (g_paintTexture)
        ResizePaintTexture(width, height);
}

void Objects::SetPaintEquation(const std::vector<int> &tokens_r, const std::vector<float> &consts_r,
                               const std::vector<int> &tokens_g, const std::vector<float> &consts_g,
                               const std::vector<int> &tokens_b, const std::vector<float> &consts_b)
{
    g_paintTokens_r = tokens_r;
    g_paintConsts_r = consts_r;
    g_paintTokens_g = tokens_g;
    g_paintConsts_g = consts_g;
    g_paintTokens_b = tokens_b;
    g_paintConsts_b = consts_b;
    g_hasPaintEquation = true;

    // Combine all tokens/constants into one buffer
    std::vector<int> allTokens;
    std::vector<float> allConsts;
    allTokens.insert(allTokens.end(), tokens_r.begin(), tokens_r.end());
    allTokens.insert(allTokens.end(), tokens_g.begin(), tokens_g.end());
    allTokens.insert(allTokens.end(), tokens_b.begin(), tokens_b.end());
    allConsts.insert(allConsts.end(), consts_r.begin(), consts_r.end());
    allConsts.insert(allConsts.end(), consts_g.begin(), consts_g.end());
    allConsts.insert(allConsts.end(), consts_b.begin(), consts_b.end());

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_paintTokenSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, allTokens.size() * sizeof(int), allTokens.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_paintConstSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, allConsts.size() * sizeof(float), allConsts.data(), GL_DYNAMIC_DRAW);
}

void Objects::DispatchPaint(int screenWidth, int screenHeight, float camX, float camY, float zoom, int objectBufferIndex)
{
    // No paint equation set or shader not ready → nothing to draw
    if (!g_hasPaintEquation || !g_paintShaderReady) return;

    // ------------------------------------------------------------------------
    // Paint resolution (user‑set via set_paint_resolution)
    // Fallback to window size if not set
    // ------------------------------------------------------------------------
    int texW = (g_paintWidth > 0) ? g_paintWidth : screenWidth;
    int texH = (g_paintHeight > 0) ? g_paintHeight : screenHeight;

    // ------------------------------------------------------------------------
    // Token/constant offsets for R, G, B channels
    // (these are the same for all paint equations)
    // ------------------------------------------------------------------------
    int rOff = 0;
    int rCount = (int)g_paintTokens_r.size();
    int gOff = rCount;
    int gCount = (int)g_paintTokens_g.size();
    int bOff = rCount + gCount;
    int bCount = (int)g_paintTokens_b.size();

    int rConOff = 0;
    int gConOff = (int)g_paintConsts_r.size();
    int bConOff = gConOff + (int)g_paintConsts_g.size();

    // ------------------------------------------------------------------------
    // Orthographic projection parameters (exactly as in Camera::GetProjectionMatrix)
    // halfHeight = zoom, halfWidth = zoom * aspect
    // ------------------------------------------------------------------------
    float aspect = (float)screenWidth / (float)screenHeight;
    float halfHeight = zoom;
    float halfWidth  = halfHeight * aspect;

    glUseProgram(g_paintShaderProgram);

    // ------------------------------------------------------------------------
    // Bind SSBOs
    // binding 0 : object data (read‑only)
    // binding 2 : paint tokens
    // binding 3 : paint constants
    // ------------------------------------------------------------------------
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[objectBufferIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_paintTokenSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, g_paintConstSSBO);

    // Output texture (image unit 0)
    glBindImageTexture(0, g_paintTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    // ------------------------------------------------------------------------
    // Camera and projection uniforms (analytical unproject)
    // ------------------------------------------------------------------------
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uCameraX"), camX);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uCameraY"), camY);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uHalfWidth"), halfWidth);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uHalfHeight"), halfHeight);

    // Screen dimensions (actual framebuffer size, for NDC conversion)
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uScreenWidth"), screenWidth);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uScreenHeight"), screenHeight);

    // Paint texture dimensions (for dispatch and texel‑to‑screen stretching)
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTexWidth"), texW);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTexHeight"), texH);

    // Simulation globals
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uNumObjects"), g_numObjects);
    glUniform1f(glGetUniformLocation(g_paintShaderProgram, "uTime"), g_simulationTime);

    // Paint equation parameters
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uColorMode"), 0);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_r"), rOff);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_r"), rCount);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_r"), rConOff);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_g"), gOff);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_g"), gCount);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_g"), gConOff);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenOffset_b"), bOff);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uTokenCount_b"), bCount);
    glUniform1i(glGetUniformLocation(g_paintShaderProgram, "uConstOffset_b"), bConOff);

    std::cout << "[Paint] camX=" << camX << " camY=" << camY 
          << " halfW=" << halfWidth << " halfH=" << halfHeight 
          << " screenW=" << screenWidth << " screenH=" << screenHeight << std::endl;

    // ------------------------------------------------------------------------
    // Dispatch the compute shader
    // Work group size = 16x16, so ((texW+15)/16) groups in X, ((texH+15)/16) in Y
    // This uses the paint resolution (texW/texH) – lower resolution = fewer groups = faster
    // ------------------------------------------------------------------------
    glDispatchCompute((texW + 15) / 16, (texH + 15) / 16, 1);

    // Wait for compute writes to finish and ensure texture is ready for reading
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // ------------------------------------------------------------------------
    // Draw the paint texture as a fullscreen quad
    // ------------------------------------------------------------------------
    glUseProgram(g_paintQuadProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_paintTexture);
    glUniform1i(glGetUniformLocation(g_paintQuadProgram, "uTex"), 0);
    glBindVertexArray(g_paintQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ============================================================================
// Update shader loading status
// ============================================================================
void Objects::UpdateShaderLoadingStatus()
{
    g_computeLoader.Update();
    g_quadLoader.Update();
    g_paintLoader.Update();
}

// ============================================================================
// Check if compute shader is ready
// ============================================================================
bool Objects::IsComputeShaderReady()
{
    return g_computeShaderReady;
}

// ============================================================================
// Check if quad shader is ready
// ============================================================================
bool Objects::IsQuadShaderReady()
{
    return g_quadShaderReady;
}

// ============================================================================
// Check if all shaders are ready
// ============================================================================
bool Objects::AreAllShadersReady()
{
    return g_computeShaderReady && g_quadShaderReady && g_paintShaderReady;
}

// ============================================================================
// Get overall shader loading progress
// ============================================================================
float Objects::GetShaderLoadProgress()
{
    float computeProgress = g_computeLoader.GetProgress();
    float quadProgress = g_quadLoader.GetProgress();
    float paintProgress = g_paintLoader.GetProgress();
    return (computeProgress + quadProgress + paintProgress) / 3.0f;
}

// ============================================================================
// Get shader loading status message
// ============================================================================
std::string Objects::GetShaderLoadStatusMessage()
{
    if (!g_computeShaderReady)
        return "[1/3] " + g_computeLoader.GetStatusMessage();
    else if (!g_quadShaderReady)
        return "[2/3] " + g_quadLoader.GetStatusMessage();
    else if (!g_paintShaderReady)
        return "[3/3] " + g_paintLoader.GetStatusMessage();
    else
        return "All shaders ready!";
}

// ============================================================================
// COLLISION MANAGEMENT FUNCTIONS
// ============================================================================

// Enable or disable collisions for an object
void Objects::SetCollisionEnabled(int objectIndex, bool enabled)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    g_collisionProperties[objectIndex].enabled = enabled ? 1 : 0;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    objectIndex * sizeof(CollisionProperties),
                    sizeof(CollisionProperties),
                    &g_collisionProperties[objectIndex]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Set collision shape for an object
void Objects::SetCollisionShape(int objectIndex, CollisionShape shape)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    g_collisionProperties[objectIndex].shapeType = static_cast<int>(shape);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    objectIndex * sizeof(CollisionProperties),
                    sizeof(CollisionProperties),
                    &g_collisionProperties[objectIndex]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Set collision material properties for an object
void Objects::SetCollisionProperties(int objectIndex, float restitution, float friction)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return;

    g_collisionProperties[objectIndex].restitution = clamp(restitution, 0.0f, 1.0f);
    g_collisionProperties[objectIndex].friction = clamp(friction, 0.0f, 1.0f);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_collisionPropsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    objectIndex * sizeof(CollisionProperties),
                    sizeof(CollisionProperties),
                    &g_collisionProperties[objectIndex]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Get collision properties for an object
CollisionProperties Objects::GetCollisionProperties(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return CollisionProperties{};

    return g_collisionProperties[objectIndex];
}

// Enable or disable collisions between two specific objects
void Objects::EnableCollisionBetween(int obj1, int obj2, bool enable)
{
    if (obj1 < 0 || obj1 >= MAX_OBJECTS || obj2 < 0 || obj2 >= MAX_OBJECTS)
        return;

    g_collisionMatrix[obj1][obj2] = enable;
    g_collisionMatrix[obj2][obj1] = enable;
}

// Check if collisions are enabled for an object
bool Objects::IsCollisionEnabled(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
        return false;
    return g_collisionProperties[objectIndex].enabled == 1;
}