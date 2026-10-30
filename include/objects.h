#ifndef OBJECTS_H
#define OBJECTS_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include "common_definitions.h"
#include "parser.h"
#include "constraints.h"

// Forward declaration for JIT script manager
class ScriptManager;

struct Object
{
    glm::vec4 position;    // xyz world, w unused
    glm::vec4 velocity;    // xyz, w unused
    glm::vec4 orientation; // quat; identity = (0,0,0,1)
    glm::vec4 spin;        // angular velocity (wx, wy, wz, 0)
    glm::vec4 visualData;  // retained for JIT compat; renderer ignores
    glm::vec4 collisionData;
    glm::vec4 color; // rgba
    float mass;
    float charge;
    int visualSkinType; // 0=CIRCLE 1=RECT 2=POLY 3=SPHERE 4=CUBE — renderer ignores
    int collisionShapeType;
    int equationID;
    int scriptID;
    int meshID;  // -1 = no shape
    int _pad[5]; // 5 × 4 = 20 bytes → struct exactly 160 (std430 alignment)
};
static_assert(sizeof(Object) == 160, "Object layout must match GLSL");

extern float g_simulationTime;
extern int g_paintWidth;
extern int g_paintHeight;

// Equation mapping (for DSL) – 112 bytes, matches GPU

struct EquationMapping
{
    int tokenOffset_ax;
    int tokenCount_ax;
    int constantOffset_ax;
    int _pad1;
    int tokenOffset_ay;
    int tokenCount_ay;
    int constantOffset_ay;
    int _pad2;
    int tokenOffset_angular;
    int tokenCount_angular;
    int constantOffset_angular;
    int _pad3;
    int tokenOffset_r;
    int tokenCount_r;
    int constantOffset_r;
    int _pad4;
    int tokenOffset_g;
    int tokenCount_g;
    int constantOffset_g;
    int _pad5;
    int tokenOffset_b;
    int tokenCount_b;
    int constantOffset_b;
    int _pad6;
    int tokenOffset_a;
    int tokenCount_a;
    int constantOffset_a;
    int _pad7;
};
static_assert(sizeof(EquationMapping) == 112, "EquationMapping must be 112 bytes");

// Collision types

enum CollisionShape
{
    COLLISION_NONE = 0,
    COLLISION_CIRCLE = 1,
    COLLISION_AABB = 2,
    COLLISION_POLYGON = 3,
    COLLISION_SPHERE = 4,
    COLLISION_BOX = 5
};
const int MAX_CONTACTS_PER_OBJECT = 4;

struct CollisionProperties
{
    int enabled;
    int shapeType;
    float restitution;
    float friction;
    float mass_factor;
    int _pad1, _pad2, _pad3;
};

struct CollisionEvent;

// Objects namespace – main physics object manager
namespace Objects
{
    static const int MAX_OBJECTS = 10000;
    static const int MAX_EQUATIONS = 256;
    GLuint GetPrevPaintTexture();

    std::vector<int> GetScratchpadIDs();

    // renderer access
    GLuint GetObjectSSBO(int bufferIndex);
   const std::unordered_map<int, std::vector<int>>& GetMeshGroups(int sourceIndex);
    void MarkMeshGroupsDirty();
    void AddObject(int meshID = -1);
    void Draw(int sourceIndex,
              const glm::mat4 &projection,
              const glm::mat4 &view,
              const glm::vec3 &cameraPos,
              const glm::vec3 &lightDir);

    // In the Objects class:
    GLuint GetPaintTexture(int &width, int &height);
    void GetPaintImage(std::vector<unsigned char> &jpeg_data, int quality = 85);
    int CreateScratchpad(size_t numElements);
    void DestroyScratchpad(int id);
    void UploadScratchpadData(int id, const void *data, size_t count);
    void *MapScratchpad(int id, GLenum access);
    void UnmapScratchpad(int id);
    size_t GetScratchpadSize(int id);
    bool IsValidScratchpad(int id);

    // Signal queue
    void SetSignalQueueCapacity(size_t capacity);
    void ClearSignalQueue();
    size_t GetSignalQueueCount();

    // Agent dispatch
    void DispatchAgent(int agentID, bool clearAfter = true);
    void DispatchAllAgents(bool clearAfter = true);

    // ---- JIT script management (new) ----
    void SetScriptID(int objectIndex, int scriptID);
    int GetScriptID(int objectIndex);
    void SetScriptManager(ScriptManager *mgr);

    // ---- Paint shader ----
    void InitPaintShader(int screenWidth, int screenHeight);
    void SetPaintEquation(const std::vector<int> &tokens_r, const std::vector<float> &consts_r,
                          const std::vector<int> &tokens_g, const std::vector<float> &consts_g,
                          const std::vector<int> &tokens_b, const std::vector<float> &consts_b,
                          const std::vector<int> &tokens_a, const std::vector<float> &consts_a);
    void DispatchPaint(int screenWidth, int screenHeight, float camX, float camY, float zoom, int objectBufferIndex);
    void SetPaintResolution(int width, int height);
    void ResizePaintTexture(int width, int height);
    void CleanupPaint();
    void SetPaintScript(int scriptID);
    void GetFullFrameImage(std::vector<unsigned char> &jpeg_data, int quality,
                           int objectBufferIndex, const glm::mat4 &projView);

    // ---- Core simulation ----
    bool Init(void *glfwWindow = nullptr);
    void Update(int inputIndex, int outputIndex, float dt);
    void Cleanup();

    // ---- Equation (DSL) management ----
    void SetEquation(const std::string &equationString, const ParsedEquation &eq, int objectIndex);
    int AddOrGetEquation(const std::string &equationString, const ParsedEquation &eq);

    // ---- Object data access ----
    void FetchToCPU(int sourceIndex, std::vector<Object> &out);
    void UpdateObjectCPU(int index, const Object &newData);
    void UploadCpuDataToGpu();

    void UploadBulkObjects(const std::vector<Object> &objects, int startIndex = 0);
    const Object *GetObjectDataDirect(int sourceIndex);
    Object *GetObjectDataDirectMutable(int sourceIndex);

    // ---- Constraints ----
    void CompactConstraintArray();
    void AddConstraint(int objectIndex, const Constraint &constraint);
    void RemoveConstraint(int objectIndex, int constraintLocalIndex);
    void UpdateConstraint(int objectIndex, int constraintLocalIndex, const Constraint &newConstraint);
    void ClearConstraints(int objectIndex);
    void ClearAllConstraints();
    std::vector<Constraint> GetConstraints(int objectIndex);

    // ---- Collisions ----
    void SetCollisionEnabled(int objectIndex, bool enabled);
    void SetCollisionShape(int objectIndex, CollisionShape shape);
    void SetCollisionProperties(int objectIndex, float restitution, float friction);
    CollisionProperties GetCollisionProperties(int objectIndex);
    bool IsCollisionEnabled(int objectIndex);
    void SetCollisionParameters(bool enableWarmStart, int maxContactIterations);
    void GetCollisionParameters(bool &enableWarmStart, int &maxContactIterations);

    // In namespace Objects
    GLuint GetContactBuffer(); // returns g_contactBufferSSBO
    int GetNumObjects();       // already exists

    // ---- Object lifecycle ----
    void RemoveObject(int index = -1);
    void ResetToInitialConditions();

    // ---- System parameters ----
    void SetDefaultObjectType(int type);
    void SetSystemParameters(float gravity, float damping, float stiffness);

    // ---- Shader loading status (async) ----
    void UpdateShaderLoadingStatus();
    bool IsComputeShaderReady();
    bool IsQuadShaderReady();
    bool AreAllShadersReady();
    float GetShaderLoadProgress();
    std::string GetShaderLoadStatusMessage();

    // ---- Accessors ----
    GLuint GetQuadProgram();
    GLuint GetComputeProgram();
    int GetNumObjects();
}

#endif // OBJECTS_H