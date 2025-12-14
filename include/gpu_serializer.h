#pragma once
#include "parser.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>

// ============================================================================
// GPU TOKEN CONSTANTS - MUST MATCH SHADER
// ============================================================================
namespace GPUTokens {
    const int TOKEN_NUMBER = 0;
    const int TOKEN_VARIABLE = 1;
    const int TOKEN_OBJECT_REF = 2;
    const int TOKEN_ADD = 3;
    const int TOKEN_SUB = 4;
    const int TOKEN_MUL = 5;
    const int TOKEN_DIV = 6;
    const int TOKEN_NEG = 7;
    const int TOKEN_POW = 8;
    const int TOKEN_SIN = 9;
    const int TOKEN_COS = 10;
    const int TOKEN_TAN = 11;
    const int TOKEN_SQRT = 12;
    const int TOKEN_LOG = 13;
    const int TOKEN_EXP = 14;
    const int TOKEN_ABS = 15;
    const int TOKEN_MIN = 16;
    const int TOKEN_MAX = 17;
    const int TOKEN_CLAMP = 18;
    const int TOKEN_FLOOR = 19;
    const int TOKEN_CEIL = 20;
    const int TOKEN_FRAC = 21;
    const int TOKEN_MOD = 22;
    const int TOKEN_ATAN2 = 23;
    const int TOKEN_REAL = 24;
    const int TOKEN_IMAG = 25;
    const int TOKEN_CONJ = 26;
    const int TOKEN_ARG = 27;
    const int TOKEN_SIGN = 28;
    const int TOKEN_STEP = 29;
    const int TOKEN_OPEN_PAREN = 30;
    const int TOKEN_CLOSE_PAREN = 31;
    const int TOKEN_COMMA = 32;
    const int TOKEN_DERIVATIVE = 33;
}

// ============================================================================
// VARIABLE NAME HASHING - MUST MATCH SHADER EXACTLY
// ============================================================================
namespace VariableHashes {
    const int VAR_HASH_X = 1;
    const int VAR_HASH_Y = 2;
    const int VAR_HASH_VX = 3;
    const int VAR_HASH_VY = 4;
    const int VAR_HASH_AX = 5;
    const int VAR_HASH_AY = 6;
    const int VAR_HASH_T = 7;
    const int VAR_HASH_THETA = 8;
    const int VAR_HASH_R = 9;
    const int VAR_HASH_G = 10;
    const int VAR_HASH_B = 11;
    const int VAR_HASH_A = 12;
    const int VAR_HASH_H = 13;
    const int VAR_HASH_S = 14;
    const int VAR_HASH_V = 15;
    const int VAR_HASH_I = 16;
    const int VAR_HASH_PI = 17;
    const int VAR_HASH_E = 18;
    const int VAR_HASH_K = 19;
    const int VAR_HASH_B_DAMP = 20;
    const int VAR_HASH_G_GRAV = 21;
    const int VAR_HASH_MASS = 22;
    const int VAR_HASH_CHARGE = 23;
    const int VAR_HASH_COUPLING = 24;
    const int VAR_HASH_FREQ = 25;
    const int VAR_HASH_AMP = 26;
    const int VAR_HASH_OMEGA = 27;      // FIXED: Was 28, now matches shader
    const int VAR_HASH_ALPHA = 28;      // FIXED: Was 29, now matches shader
}

// ============================================================================
// PROPERTY NAME HASHING - MUST MATCH SHADER EXACTLY
// ============================================================================
namespace PropertyHashes {
    const int PROP_HASH_X = 1;
    const int PROP_HASH_Y = 2;
    const int PROP_HASH_VX = 3;
    const int PROP_HASH_VY = 4;
    const int PROP_HASH_AX = 5;
    const int PROP_HASH_AY = 6;
    const int PROP_HASH_MASS = 7;
    const int PROP_HASH_CHARGE = 8;
    const int PROP_HASH_DATA_X = 9;      // rotation
    const int PROP_HASH_DATA_Y = 10;     // angular_vel
    const int PROP_HASH_DATA_Z = 11;
    const int PROP_HASH_DATA_W = 12;
    // NEW: Color properties (matches shader lines 118-121)
    const int PROP_HASH_COLOR_R = 13;
    const int PROP_HASH_COLOR_G = 14;
    const int PROP_HASH_COLOR_B = 15;
    const int PROP_HASH_COLOR_A = 16;
}

// ============================================================================
// DERIVATIVE METHOD CONSTANTS
// ============================================================================
namespace DerivativeMethods {
    const int DERIV_METHOD_NUMERICAL = 0;
    const int DERIV_METHOD_SYMBOLIC = 1;
}

// ============================================================================
// HASH LOOKUP TABLES
// ============================================================================
static const std::unordered_map<std::string, int> s_variableHashMap = {
    {"x", VariableHashes::VAR_HASH_X},
    {"y", VariableHashes::VAR_HASH_Y},
    {"vx", VariableHashes::VAR_HASH_VX},
    {"vy", VariableHashes::VAR_HASH_VY},
    {"ax", VariableHashes::VAR_HASH_AX},
    {"ay", VariableHashes::VAR_HASH_AY},
    {"t", VariableHashes::VAR_HASH_T},
    {"theta", VariableHashes::VAR_HASH_THETA},
    {"omega", VariableHashes::VAR_HASH_OMEGA},
    {"alpha", VariableHashes::VAR_HASH_ALPHA},
    {"r", VariableHashes::VAR_HASH_R},
    {"g", VariableHashes::VAR_HASH_G},
    {"b", VariableHashes::VAR_HASH_B},
    {"a", VariableHashes::VAR_HASH_A},
    {"h", VariableHashes::VAR_HASH_H},
    {"s", VariableHashes::VAR_HASH_S},
    {"v", VariableHashes::VAR_HASH_V},
    {"i", VariableHashes::VAR_HASH_I},
    {"pi", VariableHashes::VAR_HASH_PI},
    {"e", VariableHashes::VAR_HASH_E},
    {"k", VariableHashes::VAR_HASH_K},
    {"damping", VariableHashes::VAR_HASH_B_DAMP},   // Maps to 'b' uniform in shader
    {"gravity", VariableHashes::VAR_HASH_G_GRAV},   // Maps to 'g' uniform in shader
    {"mass", VariableHashes::VAR_HASH_MASS},
    {"charge", VariableHashes::VAR_HASH_CHARGE},
    {"coupling", VariableHashes::VAR_HASH_COUPLING},
    {"freq", VariableHashes::VAR_HASH_FREQ},
    {"amp", VariableHashes::VAR_HASH_AMP}
};

static const std::unordered_map<std::string, int> s_propertyHashMap = {
    {"x", PropertyHashes::PROP_HASH_X},
    {"y", PropertyHashes::PROP_HASH_Y},
    {"vx", PropertyHashes::PROP_HASH_VX},
    {"vy", PropertyHashes::PROP_HASH_VY},
    {"ax", PropertyHashes::PROP_HASH_AX},
    {"ay", PropertyHashes::PROP_HASH_AY},
    {"mass", PropertyHashes::PROP_HASH_MASS},
    {"charge", PropertyHashes::PROP_HASH_CHARGE},
    {"data.x", PropertyHashes::PROP_HASH_DATA_X},     // rotation
    {"data.y", PropertyHashes::PROP_HASH_DATA_Y},     // angular_vel
    {"data.z", PropertyHashes::PROP_HASH_DATA_Z},
    {"data.w", PropertyHashes::PROP_HASH_DATA_W},
    // NEW: Color properties
    {"color.r", PropertyHashes::PROP_HASH_COLOR_R},
    {"color.g", PropertyHashes::PROP_HASH_COLOR_G},
    {"color.b", PropertyHashes::PROP_HASH_COLOR_B},
    {"color.a", PropertyHashes::PROP_HASH_COLOR_A}
};

// Map new parser token types to GPU token types
static const std::unordered_map<TokenType, int> s_tokenTypeMap = {
    {TOKEN_ADD, GPUTokens::TOKEN_ADD},
    {TOKEN_SUB, GPUTokens::TOKEN_SUB},
    {TOKEN_MUL, GPUTokens::TOKEN_MUL},
    {TOKEN_DIV, GPUTokens::TOKEN_DIV},
    {TOKEN_NEG, GPUTokens::TOKEN_NEG},
    {TOKEN_POW, GPUTokens::TOKEN_POW},
    {TOKEN_SIN, GPUTokens::TOKEN_SIN},
    {TOKEN_COS, GPUTokens::TOKEN_COS},
    {TOKEN_TAN, GPUTokens::TOKEN_TAN},
    {TOKEN_SQRT, GPUTokens::TOKEN_SQRT},
    {TOKEN_LOG, GPUTokens::TOKEN_LOG},
    {TOKEN_EXP, GPUTokens::TOKEN_EXP},
    {TOKEN_ABS, GPUTokens::TOKEN_ABS},
    {TOKEN_MIN, GPUTokens::TOKEN_MIN},
    {TOKEN_MAX, GPUTokens::TOKEN_MAX},
    {TOKEN_CLAMP, GPUTokens::TOKEN_CLAMP},
    {TOKEN_FLOOR, GPUTokens::TOKEN_FLOOR},
    {TOKEN_CEIL, GPUTokens::TOKEN_CEIL},
    {TOKEN_FRAC, GPUTokens::TOKEN_FRAC},
    {TOKEN_MOD, GPUTokens::TOKEN_MOD},
    {TOKEN_ATAN2, GPUTokens::TOKEN_ATAN2},
    {TOKEN_REAL, GPUTokens::TOKEN_REAL},
    {TOKEN_IMAG, GPUTokens::TOKEN_IMAG},
    {TOKEN_CONJ, GPUTokens::TOKEN_CONJ},
    {TOKEN_ARG, GPUTokens::TOKEN_ARG},
    {TOKEN_SIGN, GPUTokens::TOKEN_SIGN},
    {TOKEN_STEP, GPUTokens::TOKEN_STEP},
    {TOKEN_OPEN_PAREN, GPUTokens::TOKEN_OPEN_PAREN},
    {TOKEN_CLOSE_PAREN, GPUTokens::TOKEN_CLOSE_PAREN},
    {TOKEN_COMMA, GPUTokens::TOKEN_COMMA}
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

inline int hashVariableName(const std::string& name) {
    auto it = s_variableHashMap.find(name);
    if (it != s_variableHashMap.end()) {
        return it->second;
    }
    throw std::runtime_error("Unknown variable name: " + name);
}

inline int hashPropertyName(const std::string& name) {
    auto it = s_propertyHashMap.find(name);
    if (it != s_propertyHashMap.end()) {
        return it->second;
    }
    throw std::runtime_error("Unknown property name: " + name);
}

// ============================================================================
// GPU SERIALIZED EQUATION STRUCTURE (EXTENDED)
// ============================================================================
struct GPUSerializedEquation {
    std::vector<int> tokenBuffer_ax;
    std::vector<float> constantBuffer_ax;
    std::vector<int> tokenBuffer_ay;
    std::vector<float> constantBuffer_ay;
    std::vector<int> tokenBuffer_angular;
    std::vector<float> constantBuffer_angular;
    std::vector<int> tokenBuffer_r;
    std::vector<float> constantBuffer_r;
    std::vector<int> tokenBuffer_g;
    std::vector<float> constantBuffer_g;
    std::vector<int> tokenBuffer_b;
    std::vector<float> constantBuffer_b;
    std::vector<int> tokenBuffer_a;
    std::vector<float> constantBuffer_a;
    
    void clear() {
        tokenBuffer_ax.clear();
        constantBuffer_ax.clear();
        tokenBuffer_ay.clear();
        constantBuffer_ay.clear();
        tokenBuffer_angular.clear();
        constantBuffer_angular.clear();
        tokenBuffer_r.clear();
        constantBuffer_r.clear();
        tokenBuffer_g.clear();
        constantBuffer_g.clear();
        tokenBuffer_b.clear();
        constantBuffer_b.clear();
        tokenBuffer_a.clear();
        constantBuffer_a.clear();
    }
};

// ============================================================================
// SERIALIZATION FUNCTIONS
// ============================================================================

// Forward declaration for recursive serialization
inline void serializeTokensToGPU(
    const std::vector<Token>& tokens,
    std::vector<int>& outTokenBuffer,
    std::vector<float>& outConstantBuffer,
    std::unordered_map<float, int>& constantMap
);

inline void serializeTokensToGPU(
    const std::vector<Token>& tokens,
    std::vector<int>& outTokenBuffer,
    std::vector<float>& outConstantBuffer,
    std::unordered_map<float, int>& constantMap
) {
    for (const auto& token : tokens) {
        switch (token.type) {
            case TOKEN_NUMBER: {
                // Find or add constant to constant buffer
                float value = token.numeric_value;
                int constIndex;
                
                auto it = constantMap.find(value);
                if (it != constantMap.end()) {
                    constIndex = it->second;
                } else {
                    constIndex = static_cast<int>(outConstantBuffer.size());
                    constantMap[value] = constIndex;
                    outConstantBuffer.push_back(value);
                }
                
                outTokenBuffer.push_back(GPUTokens::TOKEN_NUMBER);
                outTokenBuffer.push_back(constIndex);
                break;
            }
            
            case TOKEN_VARIABLE: {
                int varHash = hashVariableName(token.variable_name);
                outTokenBuffer.push_back(GPUTokens::TOKEN_VARIABLE);
                outTokenBuffer.push_back(varHash);
                break;
            }
            
            case TOKEN_OBJECT_REF: {
                // p[index].property format
                int propHash = hashPropertyName(token.object_property);
                outTokenBuffer.push_back(GPUTokens::TOKEN_OBJECT_REF);
                outTokenBuffer.push_back(token.object_index);
                outTokenBuffer.push_back(propHash);
                break;
            }
            
            case TOKEN_DERIVATIVE: {
                // Serialize derivative token
                int wrtVarHash = hashVariableName(token.derivative_wrt);
                int order = token.derivative_order;
                int method = token.derivative_method;
                
                // First, recursively serialize the derivative expression
                std::vector<int> exprTokenBuffer;
                std::vector<float> exprConstantBuffer;
                std::unordered_map<float, int> exprConstantMap;
                
                serializeTokensToGPU(
                    token.derivative_expr_tokens,
                    exprTokenBuffer,
                    exprConstantBuffer,
                    exprConstantMap
                );
                
                // Merge expression constants into main constant buffer
                // and update indices in expression token buffer
                std::unordered_map<int, int> indexRemap;
                for (size_t i = 0; i < exprConstantBuffer.size(); ++i) {
                    float value = exprConstantBuffer[i];
                    int newIndex;
                    
                    auto it = constantMap.find(value);
                    if (it != constantMap.end()) {
                        newIndex = it->second;
                    } else {
                        newIndex = static_cast<int>(outConstantBuffer.size());
                        constantMap[value] = newIndex;
                        outConstantBuffer.push_back(value);
                    }
                    indexRemap[static_cast<int>(i)] = newIndex;
                }
                
                // Update constant indices in expression token buffer
                for (size_t i = 0; i < exprTokenBuffer.size(); ++i) {
                    if (exprTokenBuffer[i] == GPUTokens::TOKEN_NUMBER) {
                        if (i + 1 < exprTokenBuffer.size()) {
                            int oldIndex = exprTokenBuffer[i + 1];
                            auto it = indexRemap.find(oldIndex);
                            if (it != indexRemap.end()) {
                                exprTokenBuffer[i + 1] = it->second;
                            }
                        }
                    }
                }
                
                // Write derivative metadata
                outTokenBuffer.push_back(GPUTokens::TOKEN_DERIVATIVE);
                outTokenBuffer.push_back(wrtVarHash);
                outTokenBuffer.push_back(order);
                outTokenBuffer.push_back(method);
                outTokenBuffer.push_back(static_cast<int>(exprTokenBuffer.size()));
                
                // Append expression tokens
                outTokenBuffer.insert(
                    outTokenBuffer.end(),
                    exprTokenBuffer.begin(),
                    exprTokenBuffer.end()
                );
                
                break;
            }
            
            default: {
                // Map other token types
                auto it = s_tokenTypeMap.find(token.type);
                if (it != s_tokenTypeMap.end()) {
                    outTokenBuffer.push_back(it->second);
                } else {
                    throw std::runtime_error("Unknown token type in serialization: " + std::to_string(static_cast<int>(token.type)));
                }
                break;
            }
        }
    }
}

// ============================================================================
// MAIN SERIALIZATION FUNCTION (EXTENDED)
// ============================================================================

inline GPUSerializedEquation serializeEquationForGPU(const ParsedEquation& equation) {
    GPUSerializedEquation result;
    
    // Separate constant maps for each component to allow deduplication
    std::unordered_map<float, int> constantMap_ax;
    std::unordered_map<float, int> constantMap_ay;
    std::unordered_map<float, int> constantMap_angular;
    std::unordered_map<float, int> constantMap_r;
    std::unordered_map<float, int> constantMap_g;
    std::unordered_map<float, int> constantMap_b;
    std::unordered_map<float, int> constantMap_a;
    
    // Helper lambda to serialize a component with default value
    auto serializeComponent = [](const std::vector<Token>& tokens,
                                std::vector<int>& tokenBuffer,
                                std::vector<float>& constantBuffer,
                                std::unordered_map<float, int>& constantMap,
                                float defaultValue = 0.0f) {
        if (!tokens.empty()) {
            serializeTokensToGPU(tokens, tokenBuffer, constantBuffer, constantMap);
        } /*else {
            // Empty equation: push default value
            tokenBuffer.push_back(GPUTokens::TOKEN_NUMBER);
            tokenBuffer.push_back(0);
            constantBuffer.push_back(defaultValue);
            constantMap[defaultValue] = 0;
        }
            */
    };
    
    // Serialize acceleration X
    serializeComponent(equation.tokens_ax, result.tokenBuffer_ax, result.constantBuffer_ax, constantMap_ax);
    
    // Serialize acceleration Y
    serializeComponent(equation.tokens_ay, result.tokenBuffer_ay, result.constantBuffer_ay, constantMap_ay);
    
    // Serialize angular acceleration (default 0.0)
    serializeComponent(equation.tokens_angular, result.tokenBuffer_angular, result.constantBuffer_angular, constantMap_angular);
    
    // Serialize color components (defaults: r=1.0, g=1.0, b=1.0, a=1.0)
    serializeComponent(equation.tokens_r, result.tokenBuffer_r, result.constantBuffer_r, constantMap_r, 1.0f);
    serializeComponent(equation.tokens_g, result.tokenBuffer_g, result.constantBuffer_g, constantMap_g, 1.0f);
    serializeComponent(equation.tokens_b, result.tokenBuffer_b, result.constantBuffer_b, constantMap_b, 1.0f);
    serializeComponent(equation.tokens_a, result.tokenBuffer_a, result.constantBuffer_a, constantMap_a, 1.0f);
    
    return result;
}

// ============================================================================
// BATCH SERIALIZATION FOR MULTIPLE EQUATIONS (CORRECTED)
// ============================================================================

struct GPUEquationBatch {
    // CRITICAL: Each component needs its own global buffer
    // This matches the shader's separate binding points (2 & 3 for ax, etc.)
    std::vector<int> globalTokenBuffer_ax;
    std::vector<float> globalConstantBuffer_ax;
    std::vector<int> globalTokenBuffer_ay;
    std::vector<float> globalConstantBuffer_ay;
    std::vector<int> globalTokenBuffer_angular;
    std::vector<float> globalConstantBuffer_angular;
    std::vector<int> globalTokenBuffer_r;
    std::vector<float> globalConstantBuffer_r;
    std::vector<int> globalTokenBuffer_g;
    std::vector<float> globalConstantBuffer_g;
    std::vector<int> globalTokenBuffer_b;
    std::vector<float> globalConstantBuffer_b;
    std::vector<int> globalTokenBuffer_a;
    std::vector<float> globalConstantBuffer_a;
    std::vector<EquationMapping> mappings;
    
    void clear() {
        globalTokenBuffer_ax.clear();
        globalConstantBuffer_ax.clear();
        globalTokenBuffer_ay.clear();
        globalConstantBuffer_ay.clear();
        globalTokenBuffer_angular.clear();
        globalConstantBuffer_angular.clear();
        globalTokenBuffer_r.clear();
        globalConstantBuffer_r.clear();
        globalTokenBuffer_g.clear();
        globalConstantBuffer_g.clear();
        globalTokenBuffer_b.clear();
        globalConstantBuffer_b.clear();
        globalTokenBuffer_a.clear();
        globalConstantBuffer_a.clear();
        mappings.clear();
    }
};

inline GPUEquationBatch serializeEquationBatchForGPU(const std::vector<ParsedEquation>& equations) {
    GPUEquationBatch batch;
    batch.mappings.reserve(equations.size());
    
    // Helper lambda to append a component
    auto appendComponent = [](const std::vector<int>& srcTokens,
                             const std::vector<float>& srcConstants,
                             std::vector<int>& dstTokens,
                             std::vector<float>& dstConstants,
                             int& outTokenOffset,
                             int& outTokenCount,
                             int& outConstantOffset) {
        outTokenOffset = static_cast<int>(dstTokens.size());
        outConstantOffset = static_cast<int>(dstConstants.size());
        outTokenCount = static_cast<int>(srcTokens.size());
        
        dstTokens.insert(dstTokens.end(), srcTokens.begin(), srcTokens.end());
        dstConstants.insert(dstConstants.end(), srcConstants.begin(), srcConstants.end());
    };
    
    for (const auto& eq : equations) {
        EquationMapping mapping = {};
        auto serialized = serializeEquationForGPU(eq);
        
        // Append all components
        appendComponent(serialized.tokenBuffer_ax, serialized.constantBuffer_ax,
                       batch.globalTokenBuffer_ax, batch.globalConstantBuffer_ax,
                       mapping.tokenOffset_ax, mapping.tokenCount_ax, mapping.constantOffset_ax);
        
        appendComponent(serialized.tokenBuffer_ay, serialized.constantBuffer_ay,
                       batch.globalTokenBuffer_ay, batch.globalConstantBuffer_ay,
                       mapping.tokenOffset_ay, mapping.tokenCount_ay, mapping.constantOffset_ay);
        
        appendComponent(serialized.tokenBuffer_angular, serialized.constantBuffer_angular,
                       batch.globalTokenBuffer_angular, batch.globalConstantBuffer_angular,
                       mapping.tokenOffset_angular, mapping.tokenCount_angular, mapping.constantOffset_angular);
        
        appendComponent(serialized.tokenBuffer_r, serialized.constantBuffer_r,
                       batch.globalTokenBuffer_r, batch.globalConstantBuffer_r,
                       mapping.tokenOffset_r, mapping.tokenCount_r, mapping.constantOffset_r);
        
        appendComponent(serialized.tokenBuffer_g, serialized.constantBuffer_g,
                       batch.globalTokenBuffer_g, batch.globalConstantBuffer_g,
                       mapping.tokenOffset_g, mapping.tokenCount_g, mapping.constantOffset_g);
        
        appendComponent(serialized.tokenBuffer_b, serialized.constantBuffer_b,
                       batch.globalTokenBuffer_b, batch.globalConstantBuffer_b,
                       mapping.tokenOffset_b, mapping.tokenCount_b, mapping.constantOffset_b);
        
        appendComponent(serialized.tokenBuffer_a, serialized.constantBuffer_a,
                       batch.globalTokenBuffer_a, batch.globalConstantBuffer_a,
                       mapping.tokenOffset_a, mapping.tokenCount_a, mapping.constantOffset_a);
        
        batch.mappings.push_back(mapping);
    }
    
    return batch;
}

// ============================================================================
// DEBUGGING / VALIDATION (EXTENDED)
// ============================================================================

inline void printGPUSerializedEquation(const GPUSerializedEquation& eq) {
    std::cout << "=== GPU Serialized Equation ===" << std::endl;
    
    auto printComponent = [](const std::string& name, const std::vector<int>& tokens, const std::vector<float>& constants) {
        std::cout << name << " Token Buffer (" << tokens.size() << " tokens): ";
        for (size_t i = 0; i < tokens.size(); ++i) {
            std::cout << tokens[i];
            if (i < tokens.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
        
        std::cout << name << " Constant Buffer (" << constants.size() << " constants): ";
        for (size_t i = 0; i < constants.size(); ++i) {
            std::cout << constants[i];
            if (i < constants.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
    };
    
    printComponent("AX", eq.tokenBuffer_ax, eq.constantBuffer_ax);
    printComponent("AY", eq.tokenBuffer_ay, eq.constantBuffer_ay);
    printComponent("ANGULAR", eq.tokenBuffer_angular, eq.constantBuffer_angular);
    printComponent("R", eq.tokenBuffer_r, eq.constantBuffer_r);
    printComponent("G", eq.tokenBuffer_g, eq.constantBuffer_g);
    printComponent("B", eq.tokenBuffer_b, eq.constantBuffer_b);
    printComponent("A", eq.tokenBuffer_a, eq.constantBuffer_a);
    std::cout << "===============================" << std::endl;
}

inline void printGPUEquationBatch(const GPUEquationBatch& batch) {
    std::cout << "=== GPU Equation Batch ===" << std::endl;
    std::cout << "Total Equations: " << batch.mappings.size() << std::endl;
    std::cout << "Global AX Tokens: " << batch.globalTokenBuffer_ax.size() << std::endl;
    std::cout << "Global AX Constants: " << batch.globalConstantBuffer_ax.size() << std::endl;
    std::cout << "Global AY Tokens: " << batch.globalTokenBuffer_ay.size() << std::endl;
    std::cout << "Global AY Constants: " << batch.globalConstantBuffer_ay.size() << std::endl;
    
    for (size_t i = 0; i < batch.mappings.size(); ++i) {
        const auto& m = batch.mappings[i];
        std::cout << "\nEquation " << i << ":" << std::endl;
        std::cout << "  AX: offset=" << m.tokenOffset_ax 
                  << ", count=" << m.tokenCount_ax 
                  << ", const_offset=" << m.constantOffset_ax << std::endl;
        std::cout << "  AY: offset=" << m.tokenOffset_ay 
                  << ", count=" << m.tokenCount_ay 
                  << ", const_offset=" << m.constantOffset_ay << std::endl;
        std::cout << "  ANGULAR: offset=" << m.tokenOffset_angular 
                  << ", count=" << m.tokenCount_angular 
                  << ", const_offset=" << m.constantOffset_angular << std::endl;
        std::cout << "  R: offset=" << m.tokenOffset_r 
                  << ", count=" << m.tokenCount_r 
                  << ", const_offset=" << m.constantOffset_r << std::endl;
        std::cout << "  G: offset=" << m.tokenOffset_g 
                  << ", count=" << m.tokenCount_g 
                  << ", const_offset=" << m.constantOffset_g << std::endl;
        std::cout << "  B: offset=" << m.tokenOffset_b 
                  << ", count=" << m.tokenCount_b 
                  << ", const_offset=" << m.constantOffset_b << std::endl;
        std::cout << "  A: offset=" << m.tokenOffset_a 
                  << ", count=" << m.tokenCount_a 
                  << ", const_offset=" << m.constantOffset_a << std::endl;
    }
    std::cout << "===========================" << std::endl;
}