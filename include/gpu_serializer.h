#pragma once
#include "objects.h"
#include "parser.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>

// ----------------------------------------------------------------------------
// GPU token constants (mirror TokenType but with integer codes)
// ----------------------------------------------------------------------------
namespace GPUTokens
{
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
    const int TOKEN_DOT = 34;
    const int TOKEN_CROSS = 35;
    const int TOKEN_NORM = 36;
    const int TOKEN_SELECT = 37;
    const int TOKEN_NOISE = 38;
    const int TOKEN_RAND = 39;
    const int TOKEN_SUM_NEIGHBORS = 40;
    const int TOKEN_NEIGHBOR_INDEX = 41;
    const int TOKEN_TENSOR_LIT = 42;
    const int TOKEN_LT = 43;
    const int TOKEN_LE = 44;
    const int TOKEN_GT = 45;
    const int TOKEN_GE = 46;
    const int TOKEN_EQ = 47;
    const int TOKEN_NE = 48;
    const int TOKEN_COMP = 49;
}

// ----------------------------------------------------------------------------
// Variable hashes – must match shader (math.comp / paint.comp)
// ----------------------------------------------------------------------------
namespace VariableHashes
{
    const int VAR_HASH_X        = 1;
    const int VAR_HASH_Y        = 2;
    const int VAR_HASH_VX       = 3;
    const int VAR_HASH_VY       = 4;
    const int VAR_HASH_AX       = 5;
    const int VAR_HASH_AY       = 6;
    const int VAR_HASH_T        = 7;
    const int VAR_HASH_THETA    = 8;
    const int VAR_HASH_R        = 9;
    const int VAR_HASH_G        = 10;
    const int VAR_HASH_B        = 11;
    const int VAR_HASH_A        = 12;
    const int VAR_HASH_H        = 13;
    const int VAR_HASH_S        = 14;
    const int VAR_HASH_V        = 15;
    const int VAR_HASH_I        = 16;
    const int VAR_HASH_PI       = 17;
    const int VAR_HASH_E        = 18;
    const int VAR_HASH_K        = 19;
    const int VAR_HASH_B_DAMP   = 20;
    const int VAR_HASH_G_GRAV   = 21;
    const int VAR_HASH_MASS     = 22;
    const int VAR_HASH_CHARGE   = 23;
    const int VAR_HASH_COUPLING = 24;
    const int VAR_HASH_FREQ     = 25;
    const int VAR_HASH_AMP      = 26;
    const int VAR_HASH_OMEGA    = 27;
    const int VAR_HASH_ALPHA    = 28;

    // Extended for object properties (must match shader)
    const int VAR_HASH_RADIUS   = 102;
    const int VAR_HASH_WIDTH    = 103;
    const int VAR_HASH_HEIGHT   = 104;
    const int VAR_HASH_VIS_X    = 100;
    const int VAR_HASH_VIS_Y    = 101;

    // Paint shader
    const int VAR_HASH_PX       = 200;
    const int VAR_HASH_PY       = 201;
}

// ----------------------------------------------------------------------------
// Derivative method constants
// ----------------------------------------------------------------------------
namespace DerivativeMethods
{
    const int DERIV_METHOD_NUMERICAL = 0;
    const int DERIV_METHOD_SYMBOLIC  = 1;
}

// ----------------------------------------------------------------------------
// Mapping from variable name to hash (used for normal variables)
// ----------------------------------------------------------------------------
static const std::unordered_map<std::string, int> s_variableHashMap = {
    {"x",        VariableHashes::VAR_HASH_X},
    {"y",        VariableHashes::VAR_HASH_Y},
    {"vx",       VariableHashes::VAR_HASH_VX},
    {"vy",       VariableHashes::VAR_HASH_VY},
    {"ax",       VariableHashes::VAR_HASH_AX},
    {"ay",       VariableHashes::VAR_HASH_AY},
    {"t",        VariableHashes::VAR_HASH_T},
    {"theta",    VariableHashes::VAR_HASH_THETA},
    {"omega",    VariableHashes::VAR_HASH_OMEGA},
    {"alpha",    VariableHashes::VAR_HASH_ALPHA},
    {"r",        VariableHashes::VAR_HASH_R},
    {"g",        VariableHashes::VAR_HASH_G},
    {"b",        VariableHashes::VAR_HASH_B},
    {"a",        VariableHashes::VAR_HASH_A},
    {"h",        VariableHashes::VAR_HASH_H},
    {"s",        VariableHashes::VAR_HASH_S},
    {"v",        VariableHashes::VAR_HASH_V},
    {"i",        VariableHashes::VAR_HASH_I},
    {"pi",       VariableHashes::VAR_HASH_PI},
    {"e",        VariableHashes::VAR_HASH_E},
    {"k",        VariableHashes::VAR_HASH_K},
    {"damping",  VariableHashes::VAR_HASH_B_DAMP},
    {"gravity",  VariableHashes::VAR_HASH_G_GRAV},
    {"mass",     VariableHashes::VAR_HASH_MASS},
    {"charge",   VariableHashes::VAR_HASH_CHARGE},
    {"coupling", VariableHashes::VAR_HASH_COUPLING},
    {"freq",     VariableHashes::VAR_HASH_FREQ},
    {"amp",      VariableHashes::VAR_HASH_AMP},
    {"px",       VariableHashes::VAR_HASH_PX},
    {"py",       VariableHashes::VAR_HASH_PY},
};

// ----------------------------------------------------------------------------
// Mapping from object property name to hash (must match shader's getObjectProperty)
// ----------------------------------------------------------------------------
static const std::unordered_map<std::string, int> s_propertyHashMap = {
    {"x",        VariableHashes::VAR_HASH_X},
    {"y",        VariableHashes::VAR_HASH_Y},
    {"vx",       VariableHashes::VAR_HASH_VX},
    {"vy",       VariableHashes::VAR_HASH_VY},
    {"ax",       VariableHashes::VAR_HASH_AX},
    {"ay",       VariableHashes::VAR_HASH_AY},
    {"mass",     VariableHashes::VAR_HASH_MASS},
    {"charge",   VariableHashes::VAR_HASH_CHARGE},
    {"theta",    VariableHashes::VAR_HASH_THETA},
    {"omega",    VariableHashes::VAR_HASH_OMEGA},
    {"radius",   VariableHashes::VAR_HASH_RADIUS},
    {"width",    VariableHashes::VAR_HASH_WIDTH},
    {"height",   VariableHashes::VAR_HASH_HEIGHT},
    {"data.x",   VariableHashes::VAR_HASH_VIS_X},
    {"data.y",   VariableHashes::VAR_HASH_VIS_Y},
    {"color.r",  VariableHashes::VAR_HASH_R},
    {"color.g",  VariableHashes::VAR_HASH_G},
    {"color.b",  VariableHashes::VAR_HASH_B},
    {"color.a",  VariableHashes::VAR_HASH_A},
};

// ----------------------------------------------------------------------------
// Mapping from TokenType to GPU token constant
// ----------------------------------------------------------------------------
static const std::unordered_map<TokenType, int> s_tokenTypeMap = {
    {TOKEN_ADD,            GPUTokens::TOKEN_ADD},
    {TOKEN_SUB,            GPUTokens::TOKEN_SUB},
    {TOKEN_MUL,            GPUTokens::TOKEN_MUL},
    {TOKEN_DIV,            GPUTokens::TOKEN_DIV},
    {TOKEN_NEG,            GPUTokens::TOKEN_NEG},
    {TOKEN_POW,            GPUTokens::TOKEN_POW},
    {TOKEN_SIN,            GPUTokens::TOKEN_SIN},
    {TOKEN_COS,            GPUTokens::TOKEN_COS},
    {TOKEN_TAN,            GPUTokens::TOKEN_TAN},
    {TOKEN_SQRT,           GPUTokens::TOKEN_SQRT},
    {TOKEN_LOG,            GPUTokens::TOKEN_LOG},
    {TOKEN_EXP,            GPUTokens::TOKEN_EXP},
    {TOKEN_ABS,            GPUTokens::TOKEN_ABS},
    {TOKEN_MIN,            GPUTokens::TOKEN_MIN},
    {TOKEN_MAX,            GPUTokens::TOKEN_MAX},
    {TOKEN_CLAMP,          GPUTokens::TOKEN_CLAMP},
    {TOKEN_FLOOR,          GPUTokens::TOKEN_FLOOR},
    {TOKEN_CEIL,           GPUTokens::TOKEN_CEIL},
    {TOKEN_FRAC,           GPUTokens::TOKEN_FRAC},
    {TOKEN_MOD,            GPUTokens::TOKEN_MOD},
    {TOKEN_ATAN2,          GPUTokens::TOKEN_ATAN2},
    {TOKEN_REAL,           GPUTokens::TOKEN_REAL},
    {TOKEN_IMAG,           GPUTokens::TOKEN_IMAG},
    {TOKEN_CONJ,           GPUTokens::TOKEN_CONJ},
    {TOKEN_ARG,            GPUTokens::TOKEN_ARG},
    {TOKEN_SIGN,           GPUTokens::TOKEN_SIGN},
    {TOKEN_STEP,           GPUTokens::TOKEN_STEP},
    {TOKEN_OPEN_PAREN,     GPUTokens::TOKEN_OPEN_PAREN},
    {TOKEN_CLOSE_PAREN,    GPUTokens::TOKEN_CLOSE_PAREN},
    {TOKEN_COMMA,          GPUTokens::TOKEN_COMMA},
    {TOKEN_DERIVATIVE,     GPUTokens::TOKEN_DERIVATIVE},
    {TOKEN_DOT,            GPUTokens::TOKEN_DOT},
    {TOKEN_CROSS,          GPUTokens::TOKEN_CROSS},
    {TOKEN_NORM,           GPUTokens::TOKEN_NORM},
    {TOKEN_SELECT,         GPUTokens::TOKEN_SELECT},
    {TOKEN_NOISE,          GPUTokens::TOKEN_NOISE},
    {TOKEN_RAND,           GPUTokens::TOKEN_RAND},
    {TOKEN_SUM_NEIGHBORS,  GPUTokens::TOKEN_SUM_NEIGHBORS},
    {TOKEN_NEIGHBOR_INDEX, GPUTokens::TOKEN_NEIGHBOR_INDEX},
    {TOKEN_TENSOR_LIT,     GPUTokens::TOKEN_TENSOR_LIT},
    {TOKEN_LT,             GPUTokens::TOKEN_LT},
    {TOKEN_LE,             GPUTokens::TOKEN_LE},
    {TOKEN_GT,             GPUTokens::TOKEN_GT},
    {TOKEN_GE,             GPUTokens::TOKEN_GE},
    {TOKEN_EQ,             GPUTokens::TOKEN_EQ},
    {TOKEN_NE,             GPUTokens::TOKEN_NE},
    {TOKEN_COMP,           GPUTokens::TOKEN_COMP},
};

// ----------------------------------------------------------------------------
// Helper: hash a variable name (for normal variables like x, vx, etc.)
// ----------------------------------------------------------------------------
inline int hashVariableName(const std::string &name)
{
    auto it = s_variableHashMap.find(name);
    if (it != s_variableHashMap.end())
        return it->second;
    throw std::runtime_error("Unknown variable name: " + name);
}

// ----------------------------------------------------------------------------
// Helper: hash an object property name (for p[id].property)
// ----------------------------------------------------------------------------
inline int hashPropertyName(const std::string &name)
{
    auto it = s_propertyHashMap.find(name);
    if (it != s_propertyHashMap.end())
        return it->second;
    throw std::runtime_error("Unknown property name: " + name);
}

// ----------------------------------------------------------------------------
// Helper: get or add a constant to the constant buffer, return its index
// ----------------------------------------------------------------------------
inline int getOrAddConstant(float value,
                            std::unordered_map<float, int> &constantMap,
                            std::vector<float> &outBuffer)
{
    auto it = constantMap.find(value);
    if (it != constantMap.end())
        return it->second;
    int idx = static_cast<int>(outBuffer.size());
    constantMap[value] = idx;
    outBuffer.push_back(value);
    return idx;
}

// ----------------------------------------------------------------------------
// GPU‑serialized equation structure (separate buffers per component)
// ----------------------------------------------------------------------------
struct GPUSerializedEquation
{
    std::vector<int>   tokenBuffer_ax;
    std::vector<float> constantBuffer_ax;
    std::vector<int>   tokenBuffer_ay;
    std::vector<float> constantBuffer_ay;
    std::vector<int>   tokenBuffer_angular;
    std::vector<float> constantBuffer_angular;
    std::vector<int>   tokenBuffer_r;
    std::vector<float> constantBuffer_r;
    std::vector<int>   tokenBuffer_g;
    std::vector<float> constantBuffer_g;
    std::vector<int>   tokenBuffer_b;
    std::vector<float> constantBuffer_b;
    std::vector<int>   tokenBuffer_a;
    std::vector<float> constantBuffer_a;

    void clear()
    {
        tokenBuffer_ax.clear();     constantBuffer_ax.clear();
        tokenBuffer_ay.clear();     constantBuffer_ay.clear();
        tokenBuffer_angular.clear(); constantBuffer_angular.clear();
        tokenBuffer_r.clear();      constantBuffer_r.clear();
        tokenBuffer_g.clear();      constantBuffer_g.clear();
        tokenBuffer_b.clear();      constantBuffer_b.clear();
        tokenBuffer_a.clear();      constantBuffer_a.clear();
    }
};

// ----------------------------------------------------------------------------
// Forward declarations for recursive serialization
// ----------------------------------------------------------------------------
static void serializeTokensToGPU(const std::vector<Token> &tokens,
                                 std::vector<int> &outTokenBuffer,
                                 std::vector<float> &outConstantBuffer,
                                 std::unordered_map<float, int> &constantMap);

// ----------------------------------------------------------------------------
// Serialize a sub‑block (returns token count and merges constants)
// ----------------------------------------------------------------------------
static int serializeSubBlock(const std::vector<Token> &blockTokens,
                             std::vector<int> &outTokenBuffer,
                             std::vector<float> &outConstantBuffer,
                             std::unordered_map<float, int> &constantMap)
{
    std::vector<int> subTokens;
    std::vector<float> subConsts;
    std::unordered_map<float, int> subMap;
    serializeTokensToGPU(blockTokens, subTokens, subConsts, subMap);

    // Remap constant indices from subMap to the global constantMap
    std::unordered_map<int, int> remap;
    for (size_t i = 0; i < subConsts.size(); ++i)
    {
        int newIdx = getOrAddConstant(subConsts[i], constantMap, outConstantBuffer);
        remap[static_cast<int>(i)] = newIdx;
    }

    // Rewrite subTokens to use the global constant indices
    for (size_t i = 0; i < subTokens.size(); ++i)
    {
        if (subTokens[i] == GPUTokens::TOKEN_NUMBER && i + 1 < subTokens.size())
        {
            int oldIdx = subTokens[i + 1];
            auto it = remap.find(oldIdx);
            if (it != remap.end())
                subTokens[i + 1] = it->second;
            i++; // skip the index
        }
    }

    int tokenCount = static_cast<int>(subTokens.size());
    outTokenBuffer.push_back(tokenCount);
    outTokenBuffer.insert(outTokenBuffer.end(), subTokens.begin(), subTokens.end());
    return tokenCount;
}

// ----------------------------------------------------------------------------
// Core serialization function (recursive)
// ----------------------------------------------------------------------------
static void serializeTokensToGPU(const std::vector<Token> &tokens,
                                 std::vector<int> &outTokenBuffer,
                                 std::vector<float> &outConstantBuffer,
                                 std::unordered_map<float, int> &constantMap)
{
    for (const Token &tok : tokens)
    {
        switch (tok.type)
        {
        case TOKEN_NUMBER:
        {
            int idx = getOrAddConstant(tok.numeric_value, constantMap, outConstantBuffer);
            outTokenBuffer.push_back(GPUTokens::TOKEN_NUMBER);
            outTokenBuffer.push_back(idx);
            break;
        }

        case TOKEN_VARIABLE:
        {
            int hash = hashVariableName(tok.variable_name);
            outTokenBuffer.push_back(GPUTokens::TOKEN_VARIABLE);
            outTokenBuffer.push_back(hash);
            break;
        }

        case TOKEN_OBJECT_REF:
        {
            int propHash = hashPropertyName(tok.object_property);
            outTokenBuffer.push_back(GPUTokens::TOKEN_OBJECT_REF);
            outTokenBuffer.push_back(tok.object_index);
            outTokenBuffer.push_back(propHash);
            break;
        }

        case TOKEN_DERIVATIVE:
        {
            int wrtHash = hashVariableName(tok.derivative_wrt);
            outTokenBuffer.push_back(GPUTokens::TOKEN_DERIVATIVE);
            outTokenBuffer.push_back(wrtHash);
            outTokenBuffer.push_back(tok.derivative_order);
            outTokenBuffer.push_back(tok.derivative_method);
            serializeSubBlock(tok.derivative_expr_tokens,
                              outTokenBuffer, outConstantBuffer, constantMap);
            break;
        }

        case TOKEN_TENSOR_LIT:
        {
            outTokenBuffer.push_back(GPUTokens::TOKEN_TENSOR_LIT);
            int rank = static_cast<int>(tok.tensor_shape.size());
            outTokenBuffer.push_back(rank);
            for (int d : tok.tensor_shape)
                outTokenBuffer.push_back(d);
            for (const std::vector<Token> &compList : tok.tensor_components)
                serializeSubBlock(compList, outTokenBuffer, outConstantBuffer, constantMap);
            break;
        }

        case TOKEN_SUM_NEIGHBORS:
        {
            outTokenBuffer.push_back(GPUTokens::TOKEN_SUM_NEIGHBORS);
            serializeSubBlock(tok.sum_weight, outTokenBuffer, outConstantBuffer, constantMap);
            serializeSubBlock(tok.sum_body, outTokenBuffer, outConstantBuffer, constantMap);
            break;
        }

        case TOKEN_SELECT:
        {
            outTokenBuffer.push_back(GPUTokens::TOKEN_SELECT);
            serializeSubBlock(tok.select_cond, outTokenBuffer, outConstantBuffer, constantMap);
            serializeSubBlock(tok.select_a,   outTokenBuffer, outConstantBuffer, constantMap);
            serializeSubBlock(tok.select_b,   outTokenBuffer, outConstantBuffer, constantMap);
            break;
        }

        case TOKEN_COMP:
        {
            outTokenBuffer.push_back(GPUTokens::TOKEN_COMP);
            outTokenBuffer.push_back(tok.comp_indices.size());
            for (int idx : tok.comp_indices)
                outTokenBuffer.push_back(idx);
            // The tensor expression is already serialized as a sub‑block before this token.
            break;
        }

        // Simple tokens that need no extra data
        case TOKEN_DOT:
        case TOKEN_CROSS:
        case TOKEN_NORM:
        case TOKEN_NOISE:
        case TOKEN_RAND:
        case TOKEN_NEIGHBOR_INDEX:
        {
            auto it = s_tokenTypeMap.find(tok.type);
            if (it != s_tokenTypeMap.end())
                outTokenBuffer.push_back(it->second);
            else
                throw std::runtime_error("Unknown simple token type");
            break;
        }

        default:
        {
            auto it = s_tokenTypeMap.find(tok.type);
            if (it != s_tokenTypeMap.end())
                outTokenBuffer.push_back(it->second);
            else
                throw std::runtime_error("Unhandled token type in serialization");
            break;
        }
        }
    }
}

// ----------------------------------------------------------------------------
// Serialize a single ParsedEquation to GPU buffers
// ----------------------------------------------------------------------------
inline GPUSerializedEquation serializeEquationForGPU(const ParsedEquation &eq)
{
    GPUSerializedEquation result;
    std::unordered_map<float, int> maps[7]; // one per component

    auto serializeComponent = [&](const std::vector<Token> &tokens,
                                   std::vector<int> &tokenBuf,
                                   std::vector<float> &constBuf,
                                   std::unordered_map<float, int> &cmap)
    {
        if (!tokens.empty())
            serializeTokensToGPU(tokens, tokenBuf, constBuf, cmap);
    };

    serializeComponent(eq.tokens_ax,       result.tokenBuffer_ax,       result.constantBuffer_ax,       maps[0]);
    serializeComponent(eq.tokens_ay,       result.tokenBuffer_ay,       result.constantBuffer_ay,       maps[1]);
    serializeComponent(eq.tokens_angular,  result.tokenBuffer_angular,  result.constantBuffer_angular,  maps[2]);
    serializeComponent(eq.tokens_r,        result.tokenBuffer_r,        result.constantBuffer_r,        maps[3]);
    serializeComponent(eq.tokens_g,        result.tokenBuffer_g,        result.constantBuffer_g,        maps[4]);
    serializeComponent(eq.tokens_b,        result.tokenBuffer_b,        result.constantBuffer_b,        maps[5]);
    serializeComponent(eq.tokens_a,        result.tokenBuffer_a,        result.constantBuffer_a,        maps[6]);

    return result;
}

// ----------------------------------------------------------------------------
// Batch of equations – aggregates all buffers and mappings
// ----------------------------------------------------------------------------
struct GPUEquationBatch
{
    std::vector<int>   globalTokenBuffer_ax, globalTokenBuffer_ay, globalTokenBuffer_angular,
                       globalTokenBuffer_r, globalTokenBuffer_g, globalTokenBuffer_b, globalTokenBuffer_a;
    std::vector<float> globalConstantBuffer_ax, globalConstantBuffer_ay, globalConstantBuffer_angular,
                       globalConstantBuffer_r, globalConstantBuffer_g, globalConstantBuffer_b, globalConstantBuffer_a;
    std::vector<EquationMapping> mappings;

    void clear()
    {
        globalTokenBuffer_ax.clear();    globalConstantBuffer_ax.clear();
        globalTokenBuffer_ay.clear();    globalConstantBuffer_ay.clear();
        globalTokenBuffer_angular.clear(); globalConstantBuffer_angular.clear();
        globalTokenBuffer_r.clear();     globalConstantBuffer_r.clear();
        globalTokenBuffer_g.clear();     globalConstantBuffer_g.clear();
        globalTokenBuffer_b.clear();     globalConstantBuffer_b.clear();
        globalTokenBuffer_a.clear();     globalConstantBuffer_a.clear();
        mappings.clear();
    }
};

// ----------------------------------------------------------------------------
// Serialize a vector of ParsedEquation into a single batch
// ----------------------------------------------------------------------------
inline GPUEquationBatch serializeEquationBatchForGPU(const std::vector<ParsedEquation> &equations)
{
    GPUEquationBatch batch;
    batch.mappings.reserve(equations.size());

    auto append = [](const std::vector<int>& srcTokens, const std::vector<float>& srcConsts,
                     std::vector<int>& dstTokens, std::vector<float>& dstConsts,
                     int& offT, int& cntT, int& offC) -> void
    {
        offT = static_cast<int>(dstTokens.size());
        offC = static_cast<int>(dstConsts.size());
        cntT = static_cast<int>(srcTokens.size());
        dstTokens.insert(dstTokens.end(), srcTokens.begin(), srcTokens.end());
        dstConsts.insert(dstConsts.end(), srcConsts.begin(), srcConsts.end());
    };

    for (const ParsedEquation &eq : equations)
    {
        GPUSerializedEquation ser = serializeEquationForGPU(eq);
        EquationMapping m;

        append(ser.tokenBuffer_ax, ser.constantBuffer_ax,
               batch.globalTokenBuffer_ax, batch.globalConstantBuffer_ax,
               m.tokenOffset_ax, m.tokenCount_ax, m.constantOffset_ax);

        append(ser.tokenBuffer_ay, ser.constantBuffer_ay,
               batch.globalTokenBuffer_ay, batch.globalConstantBuffer_ay,
               m.tokenOffset_ay, m.tokenCount_ay, m.constantOffset_ay);

        append(ser.tokenBuffer_angular, ser.constantBuffer_angular,
               batch.globalTokenBuffer_angular, batch.globalConstantBuffer_angular,
               m.tokenOffset_angular, m.tokenCount_angular, m.constantOffset_angular);

        append(ser.tokenBuffer_r, ser.constantBuffer_r,
               batch.globalTokenBuffer_r, batch.globalConstantBuffer_r,
               m.tokenOffset_r, m.tokenCount_r, m.constantOffset_r);

        append(ser.tokenBuffer_g, ser.constantBuffer_g,
               batch.globalTokenBuffer_g, batch.globalConstantBuffer_g,
               m.tokenOffset_g, m.tokenCount_g, m.constantOffset_g);

        append(ser.tokenBuffer_b, ser.constantBuffer_b,
               batch.globalTokenBuffer_b, batch.globalConstantBuffer_b,
               m.tokenOffset_b, m.tokenCount_b, m.constantOffset_b);

        append(ser.tokenBuffer_a, ser.constantBuffer_a,
               batch.globalTokenBuffer_a, batch.globalConstantBuffer_a,
               m.tokenOffset_a, m.tokenCount_a, m.constantOffset_a);

        batch.mappings.push_back(m);
    }

    return batch;
}

// ----------------------------------------------------------------------------
// Debug printing utilities
// ----------------------------------------------------------------------------
inline void printGPUSerializedEquation(const GPUSerializedEquation &eq)
{
    std::cout << "=== GPU Serialized Equation ===\n";
    auto print = [](const std::string &name, const auto &tokens, const auto &consts)
    {
        std::cout << name << " Tokens: ";
        for (int t : tokens) std::cout << t << " ";
        std::cout << "\n" << name << " Constants: ";
        for (float c : consts) std::cout << c << " ";
        std::cout << "\n";
    };
    print("AX", eq.tokenBuffer_ax, eq.constantBuffer_ax);
    print("AY", eq.tokenBuffer_ay, eq.constantBuffer_ay);
    print("ANG", eq.tokenBuffer_angular, eq.constantBuffer_angular);
    print("R", eq.tokenBuffer_r, eq.constantBuffer_r);
    print("G", eq.tokenBuffer_g, eq.constantBuffer_g);
    print("B", eq.tokenBuffer_b, eq.constantBuffer_b);
    print("A", eq.tokenBuffer_a, eq.constantBuffer_a);
}

inline void printGPUEquationBatch(const GPUEquationBatch &batch)
{
    std::cout << "=== GPU Equation Batch (size " << batch.mappings.size() << ") ===\n";
    for (size_t i = 0; i < batch.mappings.size(); ++i)
    {
        const EquationMapping &m = batch.mappings[i];
        std::cout << "Eq " << i << ": AX off=" << m.tokenOffset_ax
                  << " cnt=" << m.tokenCount_ax << "\n";
    }
}