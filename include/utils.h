#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <glad/glad.h>

std::string ReadTextFile(const std::string &fileName);
GLuint LoadTexture2D(const char *path);

#endif // UTILS_H
