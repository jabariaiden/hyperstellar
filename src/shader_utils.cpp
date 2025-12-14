// src/shader_utils.cpp
#include "shader_utils.h"
#include <iostream>

static void checkCompileErrors(GLuint obj, const std::string &type)
{
    GLint success = 0;
    char infoLog[2048];
    if (type != "PROGRAM")
    {
        glGetShaderiv(obj, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(obj, sizeof(infoLog), nullptr, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n"
                      << infoLog << "\n--\n";
        }
    }
    else
    {
        glGetProgramiv(obj, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(obj, sizeof(infoLog), nullptr, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n"
                      << infoLog << "\n--\n";
        }
    }
}

GLuint CompileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    checkCompileErrors(s,
                       type == GL_VERTEX_SHADER ? "VERTEX" :
                       type == GL_GEOMETRY_SHADER ? "GEOMETRY" : "FRAGMENT");
    return s;
}

GLuint CreateProgram(const char* vsSrc, const char* gsSrc, const char* fsSrc)
{
    GLuint vs = 0, gs = 0, fs = 0;
    if (vsSrc) vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    if (gsSrc) gs = CompileShader(GL_GEOMETRY_SHADER, gsSrc);
    if (fsSrc) fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);

    GLuint program = glCreateProgram();
    if (vs) glAttachShader(program, vs);
    if (gs) glAttachShader(program, gs);
    if (fs) glAttachShader(program, fs);

    glLinkProgram(program);
    checkCompileErrors(program, "PROGRAM");

    if (vs) glDeleteShader(vs);
    if (gs) glDeleteShader(gs);
    if (fs) glDeleteShader(fs);

    return program;
}

GLuint CreateProgramWithTransformFeedback(const char* vsSrc, const char* gsSrc, const char* fsSrc, const std::vector<const char*>& varyings)
{
    GLuint vs = 0, gs = 0, fs = 0;
    if (vsSrc) vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    if (gsSrc) gs = CompileShader(GL_GEOMETRY_SHADER, gsSrc);
    if (fsSrc) fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);

    GLuint program = glCreateProgram();
    if (vs) glAttachShader(program, vs);
    if (gs) glAttachShader(program, gs);
    if (fs) glAttachShader(program, fs);

    if (!varyings.empty())
        glTransformFeedbackVaryings(program, (GLsizei)varyings.size(), varyings.data(), GL_INTERLEAVED_ATTRIBS);

    glLinkProgram(program);
    checkCompileErrors(program, "PROGRAM");

    if (vs) glDeleteShader(vs);
    if (gs) glDeleteShader(gs);
    if (fs) glDeleteShader(fs);

    return program;
}
GLuint CreateComputeProgram(const char* computeSource) {
    if (!computeSource) {
        std::cerr << "[CreateComputeProgram] Null compute source!" << std::endl;
        return 0;
    }

    GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(computeShader, 1, &computeSource, nullptr);
    glCompileShader(computeShader);

    // Check compilation
    GLint success;
    glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[1024];
        glGetShaderInfoLog(computeShader, 1024, nullptr, infoLog);
        std::cerr << "[CreateComputeProgram] Compute shader compilation failed:\n" 
                  << infoLog << std::endl;
        glDeleteShader(computeShader);
        return 0;
    }

    // Create program and link
    GLuint program = glCreateProgram();
    glAttachShader(program, computeShader);
    glLinkProgram(program);

    // Check linking
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[1024];
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        std::cerr << "[CreateComputeProgram] Program linking failed:\n" 
                  << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }

    // Cleanup
    glDeleteShader(computeShader);
    
    if (program) {
        std::cout << "[CreateComputeProgram] Successfully created compute program" << std::endl;
    }

    return program;
}