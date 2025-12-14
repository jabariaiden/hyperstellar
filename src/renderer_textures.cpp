#include "renderer.h"
#include "renderer_internals.h"
#include "globals.h"
#include "shader_utils.h"
#include "utils.h"
#include <glad/glad.h>

void Renderer::SetupTextures() {
    texNeg = LoadTexture2D(TEXTURE_PATH1);
    texPos = LoadTexture2D(TEXTURE_PATH2);
    texCircle = LoadTexture2D(TEXTURE_PATH_CIRCLE);
    texSpring = LoadTexture2D(TEXTURE_PATH_SPRING);
    texRod = LoadTexture2D(TEXTURE_PATH_ROD);
    texPendulumBob = LoadTexture2D(TEXTURE_PATH_PENDULUM_BOB);

    glUseProgram(programQuad);
    glUniform1i(glGetUniformLocation(programQuad, "textureNeg"), 0);
    glUniform1i(glGetUniformLocation(programQuad, "texturePos"), 1);
    glUniform1i(glGetUniformLocation(programQuad, "textureCircle"), 2);
    glUniform1i(glGetUniformLocation(programQuad, "textureSpring"), 3);
    glUniform1i(glGetUniformLocation(programQuad, "textureRod"), 4);
    glUniform1i(glGetUniformLocation(programQuad, "texturePendulumBob"), 5);
    glUseProgram(0);
}

void Renderer::CleanupTextures() {
    glDeleteTextures(1, &texNeg);
    glDeleteTextures(1, &texPos);
    glDeleteTextures(1, &texCircle);
    glDeleteTextures(1, &texSpring);
    glDeleteTextures(1, &texRod);
    glDeleteTextures(1, &texPendulumBob);
}