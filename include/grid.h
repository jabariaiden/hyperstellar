#ifndef GRID_H
#define GRID_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

class Grid {
public:
    // Data for vector field (arrows)
    static std::vector<glm::vec3> fieldPositions;
    static std::vector<glm::vec3> fieldDirections;

    // VAOs/VBOs
    static GLuint fieldVAO, fieldVBO;
    static GLuint axesVAO, axesVBO;

    // Init and generation
    static void GenerateVectorField(int resolution, float spacing);
    static void GenerateAxes();

    // Render functions
    static void RenderField();
    static void RenderAxes();

    // Cleanup
    static void Cleanup();
};

#endif
