#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <map>
#include <string>

// Python module doesn't need FreeType text rendering
#define NO_TEXT_RENDERING

struct Character {
    GLuint textureID;
    glm::ivec2 size;
    glm::ivec2 bearing;
    unsigned int advance;
};

class TextRenderer {
public:
    TextRenderer(const std::string& fontPath, unsigned int fontSize) {
        // Empty - Python module doesn't need text rendering
    }

    ~TextRenderer() {
        // Empty destructor
    }

    void RenderText(const std::string& text, float x, float y,
        float scale, glm::vec3 color, float alpha,
        float windowWidth = 800.0f, float windowHeight = 600.0f) {
        // Do nothing - Python module
    }

private:
    std::map<char, Character> Characters;
    GLuint VAO, VBO;
    GLuint shaderProgram;

    void InitShaders() {
        // Empty
    }
};