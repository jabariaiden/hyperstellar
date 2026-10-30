#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

struct Mesh {
    unsigned int vao = 0, vbo = 0, ibo = 0;
    unsigned int indexCount = 0;
    unsigned int vertexCount = 0;
    glm::vec3 aabbMin = {};
    glm::vec3 aabbMax = {};
    bool valid = false;
};

namespace Meshes {
    std::vector<int> GetLiveIds(); 

    int RegisterCircle  (float radius, int segments = 32);
    int RegisterSquare  (float width, float height);
    int RegisterPolygon (float radius, int sides);

    int RegisterSphere  (float radius, int segments = 32, int rings = 16);
    int RegisterCube    (float sx, float sy, float sz);
    int RegisterCapsule (float radius, float height, int segments = 16, int capRings = 8);
    int RegisterCylinder(float radius, float height, int segments = 24);

    int RegisterRaw(const float* verts, size_t vertexCount,
                    const uint32_t* idx, size_t indexCount);

    const Mesh* Get(int id);
    int  Count();
    void Destroy(int id);
    void Cleanup();
}