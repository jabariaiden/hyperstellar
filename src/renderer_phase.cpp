#include "renderer.h"
#include "renderer_internals.h"
#include "globals.h"
#include "axis.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void Renderer::RenderPhaseSpace() {
    framebuffer2->Bind();
    glViewport(0, 0, (int)g_simulationViewportSize.x, (int)g_simulationViewportSize.y);
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glm::mat4 projectionPhase = glm::ortho(-3.0f, 3.0f, -3.0f, 3.0f, -1.0f, 1.0f);

    glUseProgram(programAxis);
    glUniformMatrix4fv(glGetUniformLocation(programAxis, "projection"), 1, GL_FALSE, glm::value_ptr(projectionPhase));
    Axis::Draw(programAxis, glm::mat4(1.0f));

    framebuffer2->Unbind();
}