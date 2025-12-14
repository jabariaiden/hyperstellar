#include "objects.h"
#include "shader_utils.h"
#include "utils.h"
#include "debug_helpers.h"
#include "gpu_serializer.h"
#include "constraints.h"
#include "async_shader_loader.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <atomic>

static GLuint g_objectSSBO[2] = { 0, 0 };
static GLuint g_renderVAO[2] = { 0, 0 };
static GLuint g_programCompute = 0;
static GLuint g_programQuad = 0;

static GLuint g_allTokensSSBO = 0;
static GLuint g_allConstantsSSBO = 0;
static GLuint g_mappingsSSBO = 0;
static GLuint g_initialPosSSBO = 0;
static GLuint g_constraintsSSBO = 0;
static GLuint g_objectConstraintsSSBO = 0;

static int g_numObjects = 0;
static std::vector<int> g_allTokens;
static std::vector<float> g_allConstants;
static std::vector<EquationMapping> g_equationMappings(Objects::MAX_EQUATIONS);
static std::unordered_map<std::string, int> g_equationStringToID;

static std::vector<Constraint> g_allConstraints;
static std::vector<ObjectConstraints> g_objectConstraintMappings(Objects::MAX_OBJECTS);

static int g_currentDefaultObjectType = SKIN_CIRCLE;
static float g_currentSystemGravity = 9.81f;
static float g_currentSystemDamping = 0.1f;
static float g_currentSystemStiffness = 1.0f;

static Object* g_mappedSSBO[2] = { nullptr, nullptr };
static bool g_useMapBuffer = true;

static AsyncShaderLoader g_computeLoader;
static AsyncShaderLoader g_quadLoader;
static bool g_computeShaderReady = false;
static bool g_quadShaderReady = false;

static void SafeDeleteBuffers(GLuint* buf, GLsizei n)
{
    if (n <= 0) return;
    std::vector<GLuint> toDelete;
    for (GLsizei i = 0; i < n; ++i)
        if (buf[i]) toDelete.push_back(buf[i]);
    if (!toDelete.empty())
        glDeleteBuffers(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    for (GLsizei i = 0; i < n; ++i) buf[i] = 0;
}

static void SafeDeleteVertexArrays(GLuint* arr, GLsizei n)
{
    if (n <= 0) return;
    std::vector<GLuint> toDelete;
    for (GLsizei i = 0; i < n; ++i)
        if (arr[i]) toDelete.push_back(arr[i]);
    if (!toDelete.empty())
        glDeleteVertexArrays(static_cast<GLsizei>(toDelete.size()), toDelete.data());
    for (GLsizei i = 0; i < n; ++i) arr[i] = 0;
}

static Object CreateDefaultObjectInternal(int skinType, int objectIndex, int equationID = 0)
{
    Object p;
    p.mass = 1.0f;
    p.charge = 0.0f;
    p.visualSkinType = skinType;
    p.collisionShapeType = COLLISION_NONE;
    p.equationID = equationID;
    p._pad1 = 0;

    // FIXED: Use zeros instead of random values
    // The wrapper will immediately set correct values after AddObject()
    p.position = glm::vec2(0.0f, 0.0f);
    p.velocity = glm::vec2(0.0f, 0.0f);
    p.visualData = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    p.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

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

static void SetupRenderVAOFromSSBO(GLuint vaoID, GLuint ssboID)
{
    glBindVertexArray(vaoID);
    glBindBuffer(GL_ARRAY_BUFFER, ssboID);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Object),
        (void*)offsetof(Object, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Object),
        (void*)offsetof(Object, velocity));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Object),
        (void*)offsetof(Object, mass));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Object),
        (void*)offsetof(Object, charge));
    glEnableVertexAttribArray(3);

    glVertexAttribIPointer(4, 1, GL_INT, sizeof(Object),
        (void*)offsetof(Object, visualSkinType));
    glEnableVertexAttribArray(4);

    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Object),
        (void*)offsetof(Object, visualData));
    glEnableVertexAttribArray(5);

    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Object),
        (void*)offsetof(Object, color));
    glEnableVertexAttribArray(6);

    glVertexAttribIPointer(7, 1, GL_INT, sizeof(Object),
        (void*)offsetof(Object, equationID));
    glEnableVertexAttribArray(7);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

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

void Objects::CompactConstraintArray()
{
    std::vector<Constraint> compacted;
    std::unordered_map<int, int> oldToNewIndex;

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

    for (int i = 0; i < MAX_OBJECTS; i++)
    {
        ObjectConstraints& mapping = g_objectConstraintMappings[i];
        if (mapping.numConstraints > 0)
        {
            int oldOffset = mapping.constraintOffset;
            auto it = oldToNewIndex.find(oldOffset);
            if (it != oldToNewIndex.end())
                mapping.constraintOffset = it->second;
            else
            {
                mapping.objectID = -1;
                mapping.constraintOffset = 0;
                mapping.numConstraints = 0;
            }
        }
    }

    g_allConstraints = compacted;
}

void Objects::AddConstraint(int objectIndex, const Constraint& constraint)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects)
    {
        std::cerr << "[Objects] Invalid object index: " << objectIndex << std::endl;
        return;
    }

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

    ObjectConstraints& mapping = g_objectConstraintMappings[objectIndex];

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
            std::vector<Constraint> existingConstraints;
            for (int i = 0; i < mapping.numConstraints; i++)
                existingConstraints.push_back(g_allConstraints[mapping.constraintOffset + i]);

            for (int i = 0; i < mapping.numConstraints; i++)
                g_allConstraints[mapping.constraintOffset + i].type = -1;

            int newOffset = static_cast<int>(g_allConstraints.size());
            for (const auto& c : existingConstraints)
                g_allConstraints.push_back(c);
            g_allConstraints.push_back(constraint);

            mapping.constraintOffset = newOffset;
            mapping.numConstraints = static_cast<int>(existingConstraints.size()) + 1;
            CompactConstraintArray();
        }
    }

    UploadConstraintsToGPU();
}

void Objects::RemoveConstraint(int objectIndex, int constraintLocalIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects) return;

    ObjectConstraints& mapping = g_objectConstraintMappings[objectIndex];
    if (constraintLocalIndex < 0 || constraintLocalIndex >= mapping.numConstraints) return;

    int globalIndex = mapping.constraintOffset + constraintLocalIndex;
    g_allConstraints[globalIndex].type = -1;

    for (int i = constraintLocalIndex; i < mapping.numConstraints - 1; i++)
    {
        int srcGlobal = mapping.constraintOffset + i + 1;
        int dstGlobal = mapping.constraintOffset + i;
        g_allConstraints[dstGlobal] = g_allConstraints[srcGlobal];
    }

    g_allConstraints[mapping.constraintOffset + mapping.numConstraints - 1].type = -1;
    mapping.numConstraints--;

    if (mapping.numConstraints == 0)
    {
        mapping.objectID = -1;
        mapping.constraintOffset = 0;
    }

    CompactConstraintArray();
    UploadConstraintsToGPU();
}

void Objects::ClearConstraints(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects) return;

    ObjectConstraints& mapping = g_objectConstraintMappings[objectIndex];
    if (mapping.numConstraints == 0) return;

    for (int i = 0; i < mapping.numConstraints; i++)
    {
        int globalIndex = mapping.constraintOffset + i;
        if (globalIndex >= 0 && globalIndex < static_cast<int>(g_allConstraints.size()))
            g_allConstraints[globalIndex].type = -1;
    }

    mapping.objectID = -1;
    mapping.constraintOffset = 0;
    mapping.numConstraints = 0;

    CompactConstraintArray();
    UploadConstraintsToGPU();
}

void Objects::ClearAllConstraints()
{
    for (int i = 0; i < MAX_OBJECTS; i++)
        g_objectConstraintMappings[i] = ObjectConstraints();
    g_allConstraints.clear();
    UploadConstraintsToGPU();
}

std::vector<Constraint> Objects::GetConstraints(int objectIndex)
{
    std::vector<Constraint> result;
    if (objectIndex < 0 || objectIndex >= g_numObjects) return result;

    const ObjectConstraints& mapping = g_objectConstraintMappings[objectIndex];
    if (mapping.numConstraints == 0) return result;

    for (int i = 0; i < mapping.numConstraints; i++)
        result.push_back(g_allConstraints[mapping.constraintOffset + i]);

    return result;
}

void Objects::UpdateConstraint(int objectIndex, int constraintLocalIndex, const Constraint& newConstraint)
{
    if (objectIndex < 0 || objectIndex >= g_numObjects) return;

    ObjectConstraints& mapping = g_objectConstraintMappings[objectIndex];
    if (constraintLocalIndex < 0 || constraintLocalIndex >= mapping.numConstraints) return;

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

    int globalIndex = mapping.constraintOffset + constraintLocalIndex;
    g_allConstraints[globalIndex] = newConstraint;
    UploadConstraintsToGPU();
}

bool Objects::Init(void* glfwWindow)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) return false;

    GLuint testBuffer;
    glGenBuffers(1, &testBuffer);
    if (glGetError() != GL_NO_ERROR) return false;
    glDeleteBuffers(1, &testBuffer);

    g_equationMappings.resize(MAX_EQUATIONS);
    g_objectConstraintMappings.resize(MAX_OBJECTS);

    for (auto& mapping : g_equationMappings)
        mapping = EquationMapping{};

    for (auto& mapping : g_objectConstraintMappings)
        mapping = ObjectConstraints{};

    ParserContext context;
    ParsedEquation defaultEq = ParseEquation("vx, vy, -k*x/mass, -k*y/mass, 0, 1, 0, 0, 1", context);
    int defaultEqID = AddOrGetEquation("default_zero", defaultEq);
    g_numObjects = 1;

    if (g_objectSSBO[0] == 0)
    {
        while (glGetError() != GL_NO_ERROR);
        glGenBuffers(2, g_objectSSBO);
        err = glGetError();
        if (err != GL_NO_ERROR) return false;

        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                MAX_OBJECTS * sizeof(Object),
                nullptr,
                GL_DYNAMIC_COPY);
            err = glGetError();
            if (err != GL_NO_ERROR) return false;
        }
        g_useMapBuffer = false;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        Object defaultObject = CreateDefaultObjectInternal(SKIN_CIRCLE, 0, defaultEqID);
        for (int i = 0; i < 2; i++)
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_objectSSBO[i]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Object), &defaultObject);
            err = glGetError();
            if (err != GL_NO_ERROR) return false;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    if (g_renderVAO[0] == 0)
    {
        glGenVertexArrays(2, g_renderVAO);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) return false;

        SetupRenderVAOFromSSBO(g_renderVAO[0], g_objectSSBO[0]);
        SetupRenderVAOFromSSBO(g_renderVAO[1], g_objectSSBO[1]);
    }

    if (g_allTokensSSBO == 0) glGenBuffers(1, &g_allTokensSSBO);
    if (g_allConstantsSSBO == 0) glGenBuffers(1, &g_allConstantsSSBO);
    if (g_mappingsSSBO == 0) glGenBuffers(1, &g_mappingsSSBO);
    if (g_initialPosSSBO == 0) glGenBuffers(1, &g_initialPosSSBO);
    if (g_constraintsSSBO == 0) glGenBuffers(1, &g_constraintsSSBO);
    if (g_objectConstraintsSSBO == 0) glGenBuffers(1, &g_objectConstraintsSSBO);

    err = glGetError();
    if (err != GL_NO_ERROR) return false;

    UploadPackedEquationsToGPU();
    UploadConstraintsToGPU();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_mappingsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        MAX_EQUATIONS * sizeof(EquationMapping),
        g_equationMappings.data(),
        GL_DYNAMIC_DRAW);
    err = glGetError();
    if (err != GL_NO_ERROR) return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    if (g_programCompute == 0)
    {
        g_computeLoader.LoadComputeShaderAsync(
            "math.comp",
            [](GLuint program)
            {
                g_programCompute = program;
                g_computeShaderReady = true;
            },
            [](const std::string& error)
            {
                std::cerr << "\n[Objects] Compute shader FAILED: " << error << std::endl;
                g_computeShaderReady = false;
            });
    }

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
            [](const std::string& error)
            {
                std::cerr << "\n[Objects] Quad shader FAILED: " << error << std::endl;
                g_quadShaderReady = false;
            });
    }

    return true;
}

void Objects::Update(int inputIndex, int outputIndex)
{
    UpdateShaderLoadingStatus();

    if (g_programCompute == 0 || !g_computeShaderReady) return;

    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR);

    GLint isProgram = glIsProgram(g_programCompute);
    if (!isProgram) return;

    GLint linkStatus;
    glGetProgramiv(g_programCompute, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus) return;

    glUseProgram(g_programCompute);
    err = glGetError();
    if (err != GL_NO_ERROR) return;

    GLint equationModeLoc = glGetUniformLocation(g_programCompute, "uEquationMode");
    if (equationModeLoc != -1) glUniform1i(equationModeLoc, 0);

    GLint numObjectsLoc = glGetUniformLocation(g_programCompute, "uNumObjects");
    if (numObjectsLoc != -1) glUniform1i(numObjectsLoc, g_numObjects);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_objectSSBO[inputIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_objectSSBO[outputIndex]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_allTokensSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, g_allConstantsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, g_mappingsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, g_constraintsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, g_objectConstraintsSSBO);

    int workGroupSize = 64;
    int numWorkGroups = (g_numObjects + workGroupSize - 1) / workGroupSize;
    if (numWorkGroups < 1) numWorkGroups = 1;
    if (numWorkGroups > 16) numWorkGroups = 16;

    glDispatchCompute(numWorkGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glUseProgram(0);
    for (int i = 0; i < 7; i++) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, 0);
}

int Objects::AddOrGetEquation(const std::string& equationString, const ParsedEquation& eq)
{
    auto it = g_equationStringToID.find(equationString);
    if (it != g_equationStringToID.end()) return it->second;

    ParserContext context;
    GPUSerializedEquation gpu_eq = serializeEquationForGPU(eq);

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

    g_equationMappings[newID] = mapping;
    g_equationStringToID[equationString] = newID;

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

    UploadPackedEquationsToGPU();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_mappingsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        MAX_EQUATIONS * sizeof(EquationMapping),
        g_equationMappings.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return newID;
}

void Objects::SetEquation(const std::string& equationString, const ParsedEquation& eq, int objectIndex)
{
    int eqID = AddOrGetEquation(equationString, eq);

    if (objectIndex >= 0 && objectIndex < g_numObjects)
    {
        if (g_useMapBuffer && g_mappedSSBO[0])
        {
            g_mappedSSBO[0][objectIndex].equationID = eqID;
            g_mappedSSBO[1][objectIndex].equationID = eqID;
        }
        else
        {
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

void Objects::UploadCpuDataToGpu()
{
    // NO-OP for compatibility
}

void Objects::FetchToCPU(int sourceIndex, std::vector<Object>& out)
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

void Objects::Draw(int sourceIndex)
{
    if (!g_programQuad) return;

    glUseProgram(g_programQuad);
    glBindVertexArray(g_renderVAO[sourceIndex]);
    glDrawArrays(GL_POINTS, 0, g_numObjects);
    glBindVertexArray(0);
    glUseProgram(0);
}

void Objects::AddObject()
{
    if (g_numObjects >= MAX_OBJECTS)
    {
        std::cerr << "[Objects::AddObject] Max objects reached!" << std::endl;
        return;
    }

    Object newObject = CreateDefaultObjectInternal(
        g_currentDefaultObjectType,
        g_numObjects,
        0);

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

    g_objectConstraintMappings[g_numObjects] = ObjectConstraints();
    g_numObjects++;
}

void Objects::UploadBulkObjects(const std::vector<Object>& objects, int startIndex)
{
    if (startIndex < 0 || startIndex + static_cast<int>(objects.size()) > MAX_OBJECTS)
    {
        std::cerr << "[Objects::UploadBulkObjects] Invalid range!" << std::endl;
        return;
    }

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

    if (startIndex + static_cast<int>(objects.size()) > g_numObjects)
        g_numObjects = startIndex + static_cast<int>(objects.size());
}

const Object* Objects::GetObjectDataDirect(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex > 1) return nullptr;
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex]) return g_mappedSSBO[sourceIndex];
    return nullptr;
}

Object* Objects::GetObjectDataDirectMutable(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex > 1) return nullptr;
    if (g_useMapBuffer && g_mappedSSBO[sourceIndex]) return g_mappedSSBO[sourceIndex];
    return nullptr;
}

void Objects::RemoveObject(int index)
{
    if (g_numObjects == 0)
    {
        std::cerr << "[Objects::RemoveObject] No objects to remove!" << std::endl;
        return;
    }

    int removeIdx = (index >= 0 && index < g_numObjects) ? index : (g_numObjects - 1);
    ClearConstraints(removeIdx);

    for (int i = 0; i < g_numObjects; i++)
    {
        if (i == removeIdx) continue;

        ObjectConstraints& pc = g_objectConstraintMappings[i];
        for (int j = pc.numConstraints - 1; j >= 0; j--)
        {
            int globalIdx = pc.constraintOffset + j;
            Constraint& c = g_allConstraints[globalIdx];
            if (c.type == CONSTRAINT_DISTANCE && c.targetObjectID == removeIdx)
                RemoveConstraint(i, j);
        }
    }

    if (removeIdx == g_numObjects - 1)
    {
        g_objectConstraintMappings[removeIdx] = ObjectConstraints();
        g_numObjects--;
        UploadConstraintsToGPU();
        return;
    }

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

    g_objectConstraintMappings[removeIdx] = g_objectConstraintMappings[lastObjectIdx];
    if (g_objectConstraintMappings[removeIdx].objectID == lastObjectIdx)
        g_objectConstraintMappings[removeIdx].objectID = removeIdx;

    for (int i = 0; i < g_numObjects - 1; i++)
    {
        ObjectConstraints& pc = g_objectConstraintMappings[i];
        for (int j = 0; j < pc.numConstraints; j++)
        {
            int globalIdx = pc.constraintOffset + j;
            Constraint& c = g_allConstraints[globalIdx];
            if (c.type == CONSTRAINT_DISTANCE && c.targetObjectID == lastObjectIdx)
                c.targetObjectID = removeIdx;
        }
    }

    g_objectConstraintMappings[lastObjectIdx] = ObjectConstraints();
    g_numObjects--;
    UploadConstraintsToGPU();
}

void Objects::ResetToInitialConditions()
{
    for (int i = 0; i < g_numObjects; i++)
    {
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

        Object resetObject = CreateDefaultObjectInternal(
            g_currentDefaultObjectType,
            i,
            preservedEqID);

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
}

void Objects::UpdateObjectCPU(int index, const Object& newData)
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

void Objects::SetDefaultObjectType(int type)
{
    g_currentDefaultObjectType = type;
}

void Objects::SetSystemParameters(float gravity, float damping, float stiffness)
{
    g_currentSystemGravity = gravity;
    g_currentSystemDamping = damping;
    g_currentSystemStiffness = stiffness;
}

GLuint Objects::GetQuadProgram()
{
    return g_programQuad;
}

GLuint Objects::GetComputeProgram()
{
    return g_programCompute;
}

int Objects::GetNumObjects()
{
    return g_numObjects;
}

void Objects::Cleanup()
{
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

    glDeleteProgram(g_programCompute);
    glDeleteProgram(g_programQuad);
    g_programCompute = 0;
    g_programQuad = 0;

    g_computeShaderReady = false;
    g_quadShaderReady = false;

    SafeDeleteBuffers(g_objectSSBO, 2);
    SafeDeleteVertexArrays(g_renderVAO, 2);

    SafeDeleteBuffers(&g_allTokensSSBO, 1);
    SafeDeleteBuffers(&g_allConstantsSSBO, 1);
    SafeDeleteBuffers(&g_mappingsSSBO, 1);
    SafeDeleteBuffers(&g_initialPosSSBO, 1);
    SafeDeleteBuffers(&g_constraintsSSBO, 1);
    SafeDeleteBuffers(&g_objectConstraintsSSBO, 1);

    g_numObjects = 0;
    g_allTokens.clear();
    g_allConstants.clear();
    g_equationMappings.clear();
    g_equationStringToID.clear();
    g_allConstraints.clear();
    g_objectConstraintMappings.clear();
}

void Objects::UpdateShaderLoadingStatus()
{
    g_computeLoader.Update();
    g_quadLoader.Update();
}

bool Objects::IsComputeShaderReady()
{
    return g_computeShaderReady;
}

bool Objects::IsQuadShaderReady()
{
    return g_quadShaderReady;
}

bool Objects::AreAllShadersReady()
{
    return g_computeShaderReady && g_quadShaderReady;
}

float Objects::GetShaderLoadProgress()
{
    float computeProgress = g_computeLoader.GetProgress();
    float quadProgress = g_quadLoader.GetProgress();
    return (computeProgress + quadProgress) / 2.0f;
}

std::string Objects::GetShaderLoadStatusMessage()
{
    if (!g_computeShaderReady) return "[1/2] " + g_computeLoader.GetStatusMessage();
    else if (!g_quadShaderReady) return "[2/2] " + g_quadLoader.GetStatusMessage();
    else return "All shaders ready!";
}