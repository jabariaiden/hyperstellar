#ifndef DEBUG_HELPERS_H
#define DEBUG_HELPERS_H

#include <vector>
#include <string>
#include <glad/glad.h>
#include "parser.h"  // ADD THIS for Token and ParsedEquation

inline const char* GetSkinTypeName(int skinType) {
    switch(skinType) {
        case 0: return "Circle";
        case 1: return "Rectangle";
        case 2: return "Polygon";
        default: return "Unknown";
    }
}

// Token type names for debugging (must match parser.h)
const char* GetTokenName(int token);

// NEW: Get token type name for new parser
const char* GetTokenTypeName(TokenType type);

// UPDATED: Debug print parsed equation for new parser
void DebugPrintParsedEquation(const std::string& name, 
                              const std::vector<Token>& tokens,  // ← CHANGED to Token
                              const std::vector<float>& constants);

// Verify equation mapping is valid
bool VerifyEquationMapping(int eqID,
                           const std::vector<int>& allTokensAX,
                           const std::vector<float>& allConstantsAX,
                           const std::vector<int>& allTokensAY,
                           const std::vector<float>& allConstantsAY,
                           int tokOffAX, int tokCntAX, int constOffAX,
                           int tokOffAY, int tokCntAY, int constOffAY);

// Check OpenGL errors with context
void CheckGLError(const char* operation);

// Verify SSBO data on GPU matches CPU
template<typename T>
bool VerifySSBOData(GLuint ssbo, const std::vector<T>& cpuData, const char* name);

// Test simple equation: ax = 5.0, ay = -9.8
void TestSimpleConstantEquation();

// Test equation with variables: ax = -k*x, ay = -g
void TestSpringEquation();

#endif // DEBUG_HELPERS_H