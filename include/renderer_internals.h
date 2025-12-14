#pragma once
#include <glad/glad.h>
#include "framebuffer.h"

// Declare all shared internal variables as extern
extern GLuint programField;
extern GLuint programAxis;
extern GLuint programQuad;
extern GLuint programUpdate;
extern Framebuffer::Framebuffer* framebuffer;
extern Framebuffer::Framebuffer* framebuffer2;
extern GLuint texNeg;
extern GLuint texPos;
extern GLuint texCircle;
extern GLuint texSpring;
extern GLuint texRod;
extern GLuint texPendulumBob;
extern int inputIndex;
extern int outputIndex;