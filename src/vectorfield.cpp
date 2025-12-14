// src/vectorfield.cpp
#include "vectorfield.h"
#include <vector>
#include <glad/glad.h>
#include <iostream>

static std::vector<float> g_grid;
static GLuint g_VAO = 0;
static GLuint g_VBO = 0;

void VectorField::Generate(float sizeWorld, float spacing)
{
    g_grid.clear();
    for (float x = -sizeWorld; x <= sizeWorld; x += spacing)
    {
        for (float y = -sizeWorld; y <= sizeWorld; y += spacing)
        {
            g_grid.push_back(x);
            g_grid.push_back(y);
        }
    }
}

void VectorField::CreateGL()
{
    if (g_VAO == 0) glGenVertexArrays(1, &g_VAO);
    if (g_VBO == 0) glGenBuffers(1, &g_VBO);

    glBindVertexArray(g_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_VBO);
    glBufferData(GL_ARRAY_BUFFER, g_grid.size() * sizeof(float), g_grid.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void VectorField::BindVAO()
{
    glBindVertexArray(g_VAO);
}

GLsizei VectorField::Count()
{
    return (GLsizei)(g_grid.size() / 2);
}

void VectorField::Cleanup()
{
    if (g_VAO) { glDeleteVertexArrays(1, &g_VAO); g_VAO = 0; }
    if (g_VBO) { glDeleteBuffers(1, &g_VBO); g_VBO = 0; }
    g_grid.clear();
}

GLuint VectorField::GetVAO()
{
    return g_VAO;
}
