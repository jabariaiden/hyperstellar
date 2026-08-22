#ifndef OBJECTS_H
#define OBJECTS_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "common_definitions.h"
#include "parser.h"
#include "constraints.h"

// Forward declaration for JIT script manager
class ScriptManager;

// Object structure – matches GLSL layout exactly (96 bytes)
struct Object
{
    glm::vec2 position; // offset 0
    glm::vec2 velocity; // offset 8
    float mass;         // offset 16
    float charge;       // offset 20

    int visualSkinType;     // offset 24
    int collisionShapeType; // offset 28

    glm::vec4 visualData;    // offset 32  (x=radius/width, y=height/sides, z=rotation, w=omega)
    glm::vec4 collisionData; // offset 48
    glm::vec4 color;         // offset 64

    int equationID; // offset 80 (DSL equation ID)
    int scriptID;   // offset 84 –1 = DSL, >=0 = JIT script
    int _padEnd[2]; // offset 88, pad to 96 bytes
};
static_assert(sizeof(Object) == 96, "Object size must be 96 bytes for GLSL");

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
    COLLISION_POLYGON = 3
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
    void SetSignalQueueOverflowPolicy(int policy); // 0=drop, 1=block
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
    void Draw(int sourceIndex);
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
    void EnableCollisionBetween(int obj1, int obj2, bool enable);
    bool IsCollisionEnabled(int objectIndex);
    void SetCollisionParameters(bool enableWarmStart, int maxContactIterations);
    void GetCollisionParameters(bool &enableWarmStart, int &maxContactIterations);

    // In namespace Objects
    GLuint GetContactBuffer();   // returns g_contactBufferSSBO
    int GetNumObjects();        // already exists

    // ---- Object lifecycle ----
    void AddObject();
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