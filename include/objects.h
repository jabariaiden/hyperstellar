#ifndef OBJECTS_H
#define OBJECTS_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "common_definitions.h"
#include "parser.h"
#include "constraints.h"

// CRITICAL FIX: Ensure struct packing matches GPU (std430 layout)
// Use explicit padding to avoid alignment issues
struct Object
{
    glm::vec2 position; // offset 0,  size 8
    glm::vec2 velocity; // offset 8,  size 8
    float mass;         // offset 16, size 4
    float charge;       // offset 20, size 4

    // NEW: Separated visual and physics
    int visualSkinType;     // offset 24, size 4 - How it looks
    int collisionShapeType; // offset 28, size 4 - Collision geometry (0 = none for now)

    glm::vec4 visualData;    // offset 32, size 16 - Skin parameters (x=rotation, y=angular_vel, z=unused, w=unused)
    glm::vec4 collisionData; // offset 48, size 16 - Collision parameters (unused for now)
    glm::vec4 color;         // NEW: offset 64, size 16 - (r, g, b, a)

    int equationID; // offset 80, size 4
    int _pad1;      // offset 84, size 4
    int _padEnd[2]; // offset 88, size 8
    // Total: 96 bytes (was 80 bytes)
};
static_assert(sizeof(Object) == 96, "Object struct size must be 96 bytes!");

// Extended Equation mapping structure for rotation and color
struct EquationMapping
{
    // AX components
    int tokenOffset_ax;
    int tokenCount_ax;
    int constantOffset_ax;
    int _pad1;

    // AY components
    int tokenOffset_ay;
    int tokenCount_ay;
    int constantOffset_ay;
    int _pad2;

    // NEW: Angular acceleration
    int tokenOffset_angular;
    int tokenCount_angular;
    int constantOffset_angular;
    int _pad3;

    // NEW: Color components
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

enum CollisionShape
{
    COLLISION_NONE = 0,
    COLLISION_CIRCLE = 1,
    COLLISION_AABB = 2,
    COLLISION_POLYGON = 3
};
const int MAX_CONTACTS_PER_OBJECT = 4;

// Collision properties per object
struct CollisionProperties
{
    int enabled;           // 0 = disabled, 1 = enabled
    int shapeType;         // CollisionShape enum
    float restitution;     // Bounciness (0-1)
    float friction;        // Surface friction (0-1)
    float mass_factor;     // Mass multiplier for collision response
    int _pad1, _pad2, _pad3;
};

static_assert(sizeof(EquationMapping) == 112, "EquationMapping must be 112 bytes!");

namespace Objects
{
    static const int MAX_OBJECTS = 100000;
    static const int MAX_EQUATIONS = 256;

    // Core functions
    bool Init(void* glfwWindow = nullptr);
    void Update(int inputIndex, int outputIndex);
    void Draw(int sourceIndex);
    void Cleanup();

    // Equation management
    void SetEquation(const std::string &equationString, const ParsedEquation &eq, int objectIndex);
    int AddOrGetEquation(const std::string &equationString, const ParsedEquation &eq);

    // Data management
    void FetchToCPU(int sourceIndex, std::vector<Object> &out);
    void UpdateObjectCPU(int index, const Object &newData);
    void UploadCpuDataToGpu();

    void UploadBulkObjects(const std::vector<Object> &objects, int startIndex = 0);
    const Object *GetObjectDataDirect(int sourceIndex);
    Object *GetObjectDataDirectMutable(int sourceIndex);

    // Constraint management 
    void CompactConstraintArray();
    void AddConstraint(int objectIndex, const Constraint &constraint);
    void RemoveConstraint(int objectIndex, int constraintLocalIndex);
    void UpdateConstraint(int objectIndex, int constraintLocalIndex, const Constraint &newConstraint);
    void ClearConstraints(int objectIndex);
    void ClearAllConstraints();
    std::vector<Constraint> GetConstraints(int objectIndex);

    void SetCollisionEnabled(int objectIndex, bool enabled);
    void SetCollisionShape(int objectIndex, CollisionShape shape);
    void SetCollisionProperties(int objectIndex, float restitution, float friction);
    CollisionProperties GetCollisionProperties(int objectIndex);
    void EnableCollisionBetween(int obj1, int obj2, bool enable);
    bool IsCollisionEnabled(int objectIndex);
    void SetCollisionParameters(bool enableWarmStart, int maxContactIterations);
    void GetCollisionParameters(bool& enableWarmStart, int& maxContactIterations);

    // Object management
    void AddObject();
    void RemoveObject(int index = -1);
    void ResetToInitialConditions();

    // System parameters
    void SetDefaultObjectType(int type);
    void SetSystemParameters(float gravity, float damping, float stiffness);

    // Async shader loading
    void UpdateShaderLoadingStatus();
    bool IsComputeShaderReady();
    bool IsQuadShaderReady();
    bool AreAllShadersReady();
    float GetShaderLoadProgress();
    std::string GetShaderLoadStatusMessage();

    // Accessors
    GLuint GetQuadProgram();
    GLuint GetComputeProgram();
    int GetNumObjects();
}

#endif // OBJECTS_H