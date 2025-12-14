#ifndef VECTORFIELD_H
#define VECTORFIELD_H

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace VectorField {
    void Generate(float sizeWorld = 20.0f, float spacing = 0.1f);
    void CreateGL();
    void BindVAO();
    GLsizei Count();
    void Cleanup();
    // Provide accessors if needed
    GLuint GetVAO();
}

#endif // VECTORFIELD_H
