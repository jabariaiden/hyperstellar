#include "debug_helpers.h"
#include <iostream>
#include <vector>
#include <string>
#include <glad/glad.h>

const char* GetTokenName(int token) {
    switch(token) {
        case 0: return "NUMBER";
        case 1: return "VAR_X";
        case 2: return "VAR_Y";
        case 3: return "VAR_VX";
        case 4: return "VAR_VY";
        case 5: return "ADD";
        case 6: return "SUB";
        case 7: return "MUL";
        case 8: return "DIV";
        case 9: return "NEG";
        case 10: return "SIN";
        case 11: return "COS";
        case 12: return "TAN";
        case 13: return "SQRT";
        case 14: return "LOG";
        case 15: return "EXP";
        case 16: return "POW";
        case 17: return "ABS";
        case 18: return "MIN";
        case 19: return "MAX";
        case 20: return "CLAMP";
        case 21: return "FLOOR";
        case 22: return "CEIL";
        case 23: return "FRAC";
        case 24: return "MOD";
        case 25: return "ATAN2";
        case 26: return "TIME";
        case 27: return "CONST_K";
        case 28: return "CONST_B";
        case 29: return "CONST_G";
        case 30: return "CONST_M";
        case 31: return "CONST_Q";
        case 32: return "U_GRAVITY_DIR_X";
        case 33: return "U_GRAVITY_DIR_Y";
        case 34: return "U_EXTERNAL_FORCE_X";
        case 35: return "U_EXTERNAL_FORCE_Y";
        case 36: return "U_DRIVE_FREQ";
        case 37: return "U_DRIVE_AMP";
        case 38: return "U_NUM_OBJECTS";
        case 39: return "U_OBJECT_INDEX";
        default: return "UNKNOWN";
    }
}

// NEW: Get token type name for new parser
const char* GetTokenTypeName(TokenType type) {
    switch(type) {
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_VARIABLE: return "VARIABLE";
        case TOKEN_OBJECT_REF: return "OBJECT_REF";
        case TOKEN_ADD: return "ADD";
        case TOKEN_SUB: return "SUB";
        case TOKEN_MUL: return "MUL";
        case TOKEN_DIV: return "DIV";
        case TOKEN_NEG: return "NEG";
        case TOKEN_POW: return "POW";
        case TOKEN_SIN: return "SIN";
        case TOKEN_COS: return "COS";
        case TOKEN_TAN: return "TAN";
        case TOKEN_SQRT: return "SQRT";
        case TOKEN_LOG: return "LOG";
        case TOKEN_EXP: return "EXP";
        case TOKEN_ABS: return "ABS";
        case TOKEN_MIN: return "MIN";
        case TOKEN_MAX: return "MAX";
        case TOKEN_CLAMP: return "CLAMP";
        case TOKEN_FLOOR: return "FLOOR";
        case TOKEN_CEIL: return "CEIL";
        case TOKEN_FRAC: return "FRAC";
        case TOKEN_MOD: return "MOD";
        case TOKEN_ATAN2: return "ATAN2";
        case TOKEN_REAL: return "REAL";
        case TOKEN_IMAG: return "IMAG";
        case TOKEN_CONJ: return "CONJ";
        case TOKEN_ARG: return "ARG";
        case TOKEN_SIGN: return "SIGN";
        case TOKEN_STEP: return "STEP";
        case TOKEN_OPEN_PAREN: return "OPEN_PAREN";
        case TOKEN_CLOSE_PAREN: return "CLOSE_PAREN";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DERIVATIVE: return "DERIVATIVE";
        default: return "UNKNOWN_TYPE";
    }
}

// UPDATED: Debug print parsed equation for new parser
void DebugPrintParsedEquation(const std::string& name, 
                              const std::vector<Token>& tokens,
                              const std::vector<float>& constants) {
    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "Tokens (" << tokens.size() << "):" << std::endl;
    
    for (size_t i = 0; i < tokens.size(); i++) {
        const Token& token = tokens[i];
        std::cout << "  [" << i << "] " << GetTokenTypeName(token.type);
        
        switch(token.type) {
            case TOKEN_NUMBER:
                std::cout << " = " << token.numeric_value;
                break;
                
            case TOKEN_VARIABLE:
                std::cout << " '" << token.variable_name << "'";
                break;
                
            case TOKEN_OBJECT_REF:
                std::cout << " p[" << token.object_index << "]." << token.object_property;
                break;
                
            case TOKEN_DERIVATIVE:
                std::cout << " D(expr, " << token.derivative_wrt << ", " << token.derivative_order << ")";
                break;
                
            default:
                // Just show the type name
                break;
        }
        std::cout << std::endl;
    }
    
    std::cout << "Constants (" << constants.size() << "):" << std::endl;
    for (size_t i = 0; i < constants.size(); i++) {
        std::cout << "  [" << i << "] = " << constants[i] << std::endl;
    }
}

// The rest of your functions remain UNCHANGED since they work with serialized data
bool VerifyEquationMapping(int eqID,
                           const std::vector<int>& allTokensAX,
                           const std::vector<float>& allConstantsAX,
                           const std::vector<int>& allTokensAY,
                           const std::vector<float>& allConstantsAY,
                           int tokOffAX, int tokCntAX, int constOffAX,
                           int tokOffAY, int tokCntAY, int constOffAY) {
    
    bool valid = true;
    
    std::cout << "\n=== Verifying Equation " << eqID << " ===" << std::endl;
    
    // Check AX bounds
    std::cout << "AX Mapping: tokens[" << tokOffAX << ":" << (tokOffAX + tokCntAX) 
              << "], constants[" << constOffAX << ":?]" << std::endl;
    
    if (tokOffAX < 0 || tokOffAX + tokCntAX > static_cast<int>(allTokensAX.size())) {
        std::cerr << "  ERROR: AX token range out of bounds!" << std::endl;
        std::cerr << "    Token array size: " << allTokensAX.size() << std::endl;
        valid = false;
    }
    
    if (constOffAX < 0 || constOffAX >= static_cast<int>(allConstantsAX.size())) {
        std::cerr << "  ERROR: AX constant offset out of bounds!" << std::endl;
        std::cerr << "    Constant array size: " << allConstantsAX.size() << std::endl;
        valid = false;
    }
    
    // Check AY bounds
    std::cout << "AY Mapping: tokens[" << tokOffAY << ":" << (tokOffAY + tokCntAY) 
              << "], constants[" << constOffAY << ":?]" << std::endl;
    
    if (tokOffAY < 0 || tokOffAY + tokCntAY > static_cast<int>(allTokensAY.size())) {
        std::cerr << "  ERROR: AY token range out of bounds!" << std::endl;
        std::cerr << "    Token array size: " << allTokensAY.size() << std::endl;
        valid = false;
    }
    
    if (constOffAY < 0 || constOffAY >= static_cast<int>(allConstantsAY.size())) {
        std::cerr << "  ERROR: AY constant offset out of bounds!" << std::endl;
        std::cerr << "    Constant array size: " << allConstantsAY.size() << std::endl;
        valid = false;
    }
    
    // Verify all constant indices in token stream are valid
    if (valid) {
        // Check AX
        for (int i = 0; i < tokCntAX; i++) {
            int token = allTokensAX[tokOffAX + i];
            if (token == 0) { // TOKEN_TYPE_NUMBER
                if (i + 1 < tokCntAX) {
                    int constIdx = allTokensAX[tokOffAX + i + 1];
                    int globalIdx = constOffAX + constIdx;
                    
                    if (globalIdx < 0 || globalIdx >= static_cast<int>(allConstantsAX.size())) {
                        std::cerr << "  ERROR: AX constant index " << constIdx 
                                  << " (global: " << globalIdx << ") out of bounds at token " 
                                  << i << std::endl;
                        valid = false;
                    } else {
                        std::cout << "  AX token[" << i << "]: NUMBER -> const[" 
                                  << constIdx << "] = " << allConstantsAX[globalIdx] 
                                  << " (global idx: " << globalIdx << ")" << std::endl;
                    }
                    i++; // Skip next token
                }
            }
        }
        
        // Check AY
        for (int i = 0; i < tokCntAY; i++) {
            int token = allTokensAY[tokOffAY + i];
            if (token == 0) { // TOKEN_TYPE_NUMBER
                if (i + 1 < tokCntAY) {
                    int constIdx = allTokensAY[tokOffAY + i + 1];
                    int globalIdx = constOffAY + constIdx;
                    
                    if (globalIdx < 0 || globalIdx >= static_cast<int>(allConstantsAY.size())) {
                        std::cerr << "  ERROR: AY constant index " << constIdx 
                                  << " (global: " << globalIdx << ") out of bounds at token " 
                                  << i << std::endl;
                        valid = false;
                    } else {
                        std::cout << "  AY token[" << i << "]: NUMBER -> const[" 
                                  << constIdx << "] = " << allConstantsAY[globalIdx] 
                                  << " (global idx: " << globalIdx << ")" << std::endl;
                    }
                    i++; // Skip next token
                }
            }
        }
    }
    
    if (valid) {
        std::cout << "  ✓ Equation mapping is valid" << std::endl;
    } else {
        std::cerr << "  ✗ Equation mapping has ERRORS!" << std::endl;
    }
    
    return valid;
}

void CheckGLError(const char* operation) {
    GLenum err;
    bool hasError = false;
    while ((err = glGetError()) != GL_NO_ERROR) {
        hasError = true;
        const char* errStr = "Unknown error";
        switch(err) {
            case GL_INVALID_ENUM: errStr = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE: errStr = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: errStr = "GL_INVALID_OPERATION"; break;
            case GL_STACK_OVERFLOW: errStr = "GL_STACK_OVERFLOW"; break;
            case GL_STACK_UNDERFLOW: errStr = "GL_STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY: errStr = "GL_OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        std::cerr << "OpenGL Error after " << operation << ": " << errStr 
                  << " (0x" << std::hex << err << std::dec << ")" << std::endl;
    }
    if (!hasError && operation) {
        std::cout << "✓ " << operation << " - no errors" << std::endl;
    }
}

template<typename T>
bool VerifySSBOData(GLuint ssbo, const std::vector<T>& cpuData, const char* name) {
    std::cout << "\n=== Verifying " << name << " ===" << std::endl;
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    
    // Get buffer size
    GLint bufferSize = 0;
    glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    
    size_t expectedSize = cpuData.size() * sizeof(T);
    std::cout << "Buffer size: " << bufferSize << " bytes" << std::endl;
    std::cout << "Expected size: " << expectedSize << " bytes" << std::endl;
    
    if (static_cast<size_t>(bufferSize) != expectedSize) {
        std::cerr << "  ✗ Size mismatch!" << std::endl;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return false;
    }
    
    // Read back data
    std::vector<T> gpuData(cpuData.size());
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, gpuData.data());
    
    // Compare
    bool matches = true;
    size_t maxPrint = std::min(cpuData.size(), size_t(10)); // Print first 10
    
    for (size_t i = 0; i < cpuData.size(); i++) {
        if (gpuData[i] != cpuData[i]) {
            if (matches) {
                std::cerr << "  ✗ Data mismatch detected!" << std::endl;
            }
            if (i < maxPrint) {
                std::cerr << "    [" << i << "] CPU: " << cpuData[i] 
                          << " != GPU: " << gpuData[i] << std::endl;
            }
            matches = false;
        } else if (i < maxPrint) {
            std::cout << "    [" << i << "] = " << cpuData[i] << " ✓" << std::endl;
        }
    }
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
    if (matches) {
        std::cout << "  ✓ All data matches!" << std::endl;
    }
    
    return matches;
}

void TestSimpleConstantEquation() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing Simple Constant Equation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Equation: ax = 5.0, ay = -9.8" << std::endl;
    
    // Create using new parser
    ParserContext context;
    ParsedEquation eq = ParseEquation("-k*x/m, -k*y/m, 0, 1, 1, 1, 1", context);
    
    DebugPrintParsedEquation("AX Expression", eq.tokens_ax, eq.constants);
    DebugPrintParsedEquation("AY Expression", eq.tokens_ay, eq.constants);
}

void TestSpringEquation() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing Spring Equation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Equation: ax = -k*x, ay = -g" << std::endl;
    
    // Create using new parser
    ParserContext context;
    ParsedEquation eq = ParseEquation("-k*x/m, -k*y/m, 0, 1, 1, 1, 1", context);
    
    DebugPrintParsedEquation("AX Expression", eq.tokens_ax, eq.constants);
    DebugPrintParsedEquation("AY Expression", eq.tokens_ay, eq.constants);
}