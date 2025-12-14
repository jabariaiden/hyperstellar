#include "grid.h"
#include <iostream>

// Static members
std::vector<glm::vec3> Grid::fieldPositions;
std::vector<glm::vec3> Grid::fieldDirections;

GLuint Grid::fieldVAO = 0;
GLuint Grid::fieldVBO = 0;
GLuint Grid::axesVAO = 0;
GLuint Grid::axesVBO = 0;

void Grid::GenerateVectorField(int resolution, float spacing) {
    fieldPositions.clear();
    fieldDirections.clear();

    for (int x = -resolution; x <= resolution; x++) {
        for (int y = -resolution; y <= resolution; y++) {
            for (int z = -resolution; z <= resolution; z++) {
                glm::vec3 pos = glm::vec3(x, y, z) * spacing;
                glm::vec3 dir = glm::normalize(glm::vec3(-y, x, z)); // simple swirl field

                fieldPositions.push_back(pos);
                fieldDirections.push_back(dir);
            }
        }
    }

    if (fieldVAO == 0) {
        glGenVertexArrays(1, &fieldVAO);
        glGenBuffers(1, &fieldVBO);
    }

    glBindVertexArray(fieldVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fieldVBO);

    std::vector<glm::vec3> vertices;
    for (size_t i = 0; i < fieldPositions.size(); i++) {
        vertices.push_back(fieldPositions[i]);
        vertices.push_back(fieldPositions[i] + 0.2f * fieldDirections[i]);
    }

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glBindVertexArray(0);

    std::cout << "Generated vector field with " << fieldPositions.size() << " vectors\n";
}

void Grid::GenerateAxes() {
    glm::vec3 axesVertices[] = {
        glm::vec3(0,0,0), glm::vec3(10,0,0), // X
        glm::vec3(0,0,0), glm::vec3(0,10,0), // Y
        glm::vec3(0,0,0), glm::vec3(0,0,10)  // Z
    };

    if (axesVAO == 0) {
        glGenVertexArrays(1, &axesVAO);
        glGenBuffers(1, &axesVBO);
    }

    glBindVertexArray(axesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, axesVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axesVertices), axesVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glBindVertexArray(0);

    std::cout << "Generated axes\n";
}

void Grid::RenderField() {
    glBindVertexArray(fieldVAO);
    glDrawArrays(GL_LINES, 0, fieldPositions.size() * 2);
    glBindVertexArray(0);
}

void Grid::RenderAxes() {
    glBindVertexArray(axesVAO);
    glDrawArrays(GL_LINES, 0, 6);
    glBindVertexArray(0);
}

void Grid::Cleanup() {
    if (fieldVAO) glDeleteVertexArrays(1, &fieldVAO);
    if (fieldVBO) glDeleteBuffers(1, &fieldVBO);
    if (axesVAO) glDeleteVertexArrays(1, &axesVAO);
    if (axesVBO) glDeleteBuffers(1, &axesVBO);
}
