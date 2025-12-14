#ifndef SHADER_UTILS_H
#define SHADER_UTILS_H

#include <glad/glad.h>
#include <string>
#include <vector>

GLuint CompileShader(GLenum type, const char* src);
GLuint CreateProgram(const char* vsSrc, const char* gsSrc, const char* fsSrc);
GLuint CreateProgramWithTransformFeedback(const char* vsSrc, const char* gsSrc, const char* fsSrc, const std::vector<const char*>& varyings);
GLuint CreateComputeProgram(const char* csSrc);
#endif // SHADER_UTILS_H
