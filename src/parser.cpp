#include "parser.h"
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <set>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <string>
// I hate my life :`)
// ============================================================================
// PARSER CONTEXT IMPLEMENTATION
// ============================================================================

ParserContext::ParserContext()
{
    // Register spatial variables
    registerVariable("x", DOMAIN_SPATIAL, true);
    registerVariable("y", DOMAIN_SPATIAL, true);
    registerVariable("t", DOMAIN_TIME, true);
    registerVariable("theta", DOMAIN_ROTATIONAL, true);

    // Register color components
    registerVariable("r", DOMAIN_COLOR, true);
    registerVariable("g", DOMAIN_COLOR, true);
    registerVariable("b", DOMAIN_COLOR, true);
    registerVariable("a", DOMAIN_COLOR, true);
    registerVariable("h", DOMAIN_COLOR, true);
    registerVariable("s", DOMAIN_COLOR, true);
    registerVariable("v", DOMAIN_COLOR, true);

    // Register physics properties
    registerVariable("vx", DOMAIN_SPATIAL, true);
    registerVariable("vy", DOMAIN_SPATIAL, true);
    registerVariable("ax", DOMAIN_SPATIAL, true);
    registerVariable("ay", DOMAIN_SPATIAL, true);
    registerVariable("omega", DOMAIN_ROTATIONAL, true);
    registerVariable("alpha", DOMAIN_ROTATIONAL, true);

    // Register constants
    registerVariable("i", DOMAIN_COMPLEX, false);
    registerVariable("pi", DOMAIN_SCALAR, false);
    registerVariable("e", DOMAIN_SCALAR, false);
    registerVariable("k", DOMAIN_SCALAR, false);
    registerVariable("damping", DOMAIN_SCALAR, false);
    registerVariable("gravity", DOMAIN_SCALAR, false);
    registerVariable("mass", DOMAIN_SCALAR, true);
    registerVariable("charge", DOMAIN_SCALAR, false);
    registerVariable("coupling", DOMAIN_SCALAR, false);
    registerVariable("freq", DOMAIN_SCALAR, false);
    registerVariable("amp", DOMAIN_SCALAR, false);
    registerVariable("radius", DOMAIN_SPATIAL, true);

    // Register object type with its properties
    registerObjectType("p", {"x", "y", "vx", "vy", "ax", "ay", "mass", "charge",
                             "theta", "omega", "radius", "width", "height",
                             "data.x", "data.y", "data.z", "data.w",
                             "color.r", "color.g", "color.b", "color.a"});
}

void ParserContext::registerVariable(const std::string &name, VariableDomain domain, bool differentiable)
{
    m_variables[name] = VariableDef(name, domain, differentiable);
}

void ParserContext::registerObjectType(const std::string &type, const std::vector<std::string> &properties)
{
    m_objectTypes[type] = properties;
}

bool ParserContext::isValidVariable(const std::string &name) const
{
    return m_variables.find(name) != m_variables.end();
}

bool ParserContext::isValidDerivativeWRT(const std::string &varName) const
{
    auto it = m_variables.find(varName);
    return it != m_variables.end() && it->second.differentiable;
}

ParserContext::VariableDomain ParserContext::getVariableDomain(const std::string &varName) const
{
    auto it = m_variables.find(varName);
    return it != m_variables.end() ? it->second.domain : DOMAIN_SCALAR;
}

// ============================================================================
// UTILITIES
// ============================================================================

static std::string trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

static bool isRightAssoc(TokenType t) { return t == TOKEN_POW; }

// ============================================================================
// OPERATOR PRECEDENCE & FUNCTION ARITY
// ============================================================================

static const std::unordered_map<TokenType, int> s_precedence = {
    {TOKEN_ADD, 2}, {TOKEN_SUB, 2}, 
    {TOKEN_LT, 2}, {TOKEN_LE, 2}, 
    {TOKEN_GT, 2}, {TOKEN_GE, 2}, 
    {TOKEN_EQ, 2}, {TOKEN_NE, 2}, 
    {TOKEN_MUL, 3}, {TOKEN_DIV, 3}, 
    {TOKEN_POW, 5}};

static const std::unordered_map<TokenType, int> s_arity = {
    {TOKEN_NEG, 1}, {TOKEN_SIN, 1}, 
    {TOKEN_COS, 1}, {TOKEN_TAN, 1}, 
    {TOKEN_SQRT, 1}, {TOKEN_LOG, 1}, 
    {TOKEN_EXP, 1}, {TOKEN_ABS, 1}, 
    {TOKEN_FLOOR, 1}, {TOKEN_CEIL, 1}, 
    {TOKEN_FRAC, 1}, {TOKEN_SIGN, 1}, 
    {TOKEN_STEP, 1}, {TOKEN_REAL, 1}, 
    {TOKEN_IMAG, 1}, {TOKEN_CONJ, 1}, 
    {TOKEN_ARG, 1}, {TOKEN_MIN, 2}, 
    {TOKEN_MAX, 2}, {TOKEN_MOD, 2}, 
    {TOKEN_ATAN2, 2}, {TOKEN_CLAMP, 3}, 
    {TOKEN_DOT, 2}, {TOKEN_CROSS, 2}, 
    {TOKEN_NORM, 1}, {TOKEN_SELECT, 3}, 
    {TOKEN_NOISE, 2}, {TOKEN_RAND, 0}, 
    {TOKEN_SUM_NEIGHBORS, 2}, {TOKEN_COMP, -1}}; // -1 means variable arity

static const std::unordered_map<std::string, TokenType> s_funcMap = {
    {"sin", TOKEN_SIN}, {"cos", TOKEN_COS}, 
    {"tan", TOKEN_TAN}, {"sqrt", TOKEN_SQRT}, 
    {"log", TOKEN_LOG}, {"exp", TOKEN_EXP}, 
    {"abs", TOKEN_ABS}, {"min", TOKEN_MIN}, 
    {"max", TOKEN_MAX}, {"clamp", TOKEN_CLAMP}, 
    {"floor", TOKEN_FLOOR}, {"ceil", TOKEN_CEIL}, 
    {"frac", TOKEN_FRAC}, {"mod", TOKEN_MOD}, 
    {"atan2", TOKEN_ATAN2}, {"real", TOKEN_REAL}, 
    {"imag", TOKEN_IMAG}, {"conj", TOKEN_CONJ}, 
    {"arg", TOKEN_ARG}, {"sign", TOKEN_SIGN}, 
    {"step", TOKEN_STEP}, {"dot", TOKEN_DOT}, 
    {"cross", TOKEN_CROSS}, {"norm", TOKEN_NORM}, 
    {"length", TOKEN_NORM}, {"select", TOKEN_SELECT}, 
    {"noise", TOKEN_NOISE}, {"rand", TOKEN_RAND}, 
    {"sample_prev_r", TOKEN_SAMPLE_PREV_R},{"sample_prev_g", TOKEN_SAMPLE_PREV_G},
    {"sample_prev_b", TOKEN_SAMPLE_PREV_B},{"sample_prev_a", TOKEN_SAMPLE_PREV_A},
    {"avg_prev_r", TOKEN_AVG_PREV_R},{"avg_prev_g", TOKEN_AVG_PREV_G},
    {"avg_prev_b", TOKEN_AVG_PREV_B},{"avg_prev_a", TOKEN_AVG_PREV_A},
    {"sum_neighbors", TOKEN_SUM_NEIGHBORS}, {"comp", TOKEN_COMP}};

// ============================================================================
// DERIVATIVE PARSER
// ============================================================================

std::vector<Token> parseDerivativeCall(const std::string &expr, size_t start, size_t &end, const ParserContext &ctx)
{
    if (expr.substr(start, 2) != "D(")
        throw std::runtime_error("Expected D(");
    size_t pos = start + 2;
    int depth = 1;
    std::string inner;
    std::string wrt;
    int order = 1;

    // Parse inner expression until comma
    while (pos < expr.size() && depth > 0)
    {
        char c = expr[pos];
        if (c == '(')
            depth++;
        else if (c == ')')
            depth--;
        else if (depth == 1 && c == ',')
            break;
        if (depth > 0)
            inner += c;
        pos++;
    }
    if (depth != 1)
        throw std::runtime_error("Unclosed D(");
    pos++; // skip ','

    // Parse differentiation variable
    while (pos < expr.size() && std::isspace(expr[pos]))
        pos++;
    size_t wrtStart = pos;
    while (pos < expr.size() && expr[pos] != ',' && expr[pos] != ')')
        pos++;
    wrt = trim(expr.substr(wrtStart, pos - wrtStart));
    if (!ctx.isValidDerivativeWRT(wrt))
        throw std::runtime_error("Cannot differentiate wrt " + wrt);

    // Parse optional order
    if (pos < expr.size() && expr[pos] == ',')
    {
        pos++;
        while (pos < expr.size() && std::isspace(expr[pos]))
            pos++;
        size_t ordStart = pos;
        while (pos < expr.size() && expr[pos] != ')')
            pos++;
        order = std::stoi(trim(expr.substr(ordStart, pos - ordStart)));
        if (order < 1 || order > 4)
            throw std::runtime_error("Order 1-4");
    }
    if (pos >= expr.size() || expr[pos] != ')')
        throw std::runtime_error("Missing ) in D(");
    end = pos;

    // Tokenize and convert inner expression
    auto tokens = tokenizeExpression(inner, ctx);
    auto rpn = infixToRPN(tokens);
    Token tok(TOKEN_DERIVATIVE);
    tok.derivative_wrt = wrt;
    tok.derivative_order = order;
    tok.derivative_method = DERIV_METHOD_NUMERICAL;
    tok.derivative_expr_tokens = rpn;
    return {tok};
}

// ============================================================================
// TENSOR LITERAL PARSER (ANY RANK)
// ============================================================================

static std::vector<Token> tokenizeImpl(const std::string &expr, const ParserContext &ctx, bool inSumNeighbors);

/**
 * Parse a tensor literal from a substring that starts with '[' or '('.
 * Returns a TOKEN_TENSOR_LIT with shape and flattened components.
 * Supports arbitrary nesting: scalars, vectors, matrices, and higher-rank tensors.
 */
static Token parseTensorLiteral(const std::string &expr, size_t &i, const ParserContext &ctx, bool inSumNeighbors)
{
    char open = expr[i];
    char close = (open == '(') ? ')' : ']';
    i++; // skip opening bracket/paren

    int bracketDepth = 1; // depth of the current bracket type only
    size_t start = i;
    std::vector<std::string> componentStrings;

    while (i < expr.size() && bracketDepth > 0)
    {
        char c = expr[i];
        if (c == open)
            bracketDepth++;
        else if (c == close)
            bracketDepth--;
        else if (c == ',' && bracketDepth == 1)
        {
            // top-level comma separates components
            componentStrings.push_back(expr.substr(start, i - start));
            start = i + 1;
        }
        i++;
    }
    if (bracketDepth != 0)
        throw std::runtime_error("Unmatched bracket/paren in tensor literal");

    // Last component
    componentStrings.push_back(expr.substr(start, i - start - 1));
    // i already points past the closing bracket

    // Parse each component
    std::vector<std::vector<Token>> components;
    for (const auto &compStr : componentStrings)
    {
        std::string trimmed = trim(compStr);
        if (trimmed.empty())
            continue;
        auto compTokens = tokenizeImpl(trimmed, ctx, inSumNeighbors);
        components.push_back(compTokens);
    }

    // Determine shape
    std::vector<int> shape;
    shape.push_back((int)components.size());
    if (!components.empty() && components[0].size() == 1 && components[0][0].type == TOKEN_TENSOR_LIT)
        shape.insert(shape.end(), components[0][0].tensor_shape.begin(), components[0][0].tensor_shape.end());

    Token result(TOKEN_TENSOR_LIT);
    result.tensor_shape = shape;
    result.tensor_components = std::move(components);
    return result;
}

// ============================================================================
// TOKENIZER (RECURSIVE)
// ============================================================================

static std::vector<Token> tokenizeImpl(const std::string &expr, const ParserContext &ctx, bool inSumNeighbors)
{
    std::vector<Token> tokens;
    std::string cur;
    size_t i = 0;

    // Helper to flush the current accumulated identifier (handles numbers, keywords, variables, and unknown identifiers)
    auto flushIdentifier = [&]()
    {
        if (cur.empty())
            return;
        auto it = s_funcMap.find(cur);
        if (it != s_funcMap.end())
        {
            tokens.push_back(Token(it->second));
        }
        else if (inSumNeighbors && cur == "i")
        {
            tokens.push_back(Token(TOKEN_NEIGHBOR_INDEX));
        }
        else if (cur == "let")
        {
            tokens.push_back(Token(TOKEN_LET));
        }
        else if (cur == "ax")
        {
            tokens.push_back(Token(TOKEN_TARGET_AX));
        }
        else if (cur == "ay")
        {
            tokens.push_back(Token(TOKEN_TARGET_AY));
        }
        else if (cur == "angular")
        {
            tokens.push_back(Token(TOKEN_TARGET_ANGULAR));
        }
        else if (cur == "color.r")
        {
            tokens.push_back(Token(TOKEN_TARGET_COLOR_R));
        }
        else if (cur == "color.g")
        {
            tokens.push_back(Token(TOKEN_TARGET_COLOR_G));
        }
        else if (cur == "color.b")
        {
            tokens.push_back(Token(TOKEN_TARGET_COLOR_B));
        }
        else if (cur == "color.a")
        {
            tokens.push_back(Token(TOKEN_TARGET_COLOR_A));
        }
        else if (cur == "size")
        {
            tokens.push_back(Token(TOKEN_TARGET_SIZE));
        }
        else if (cur == "data.x")
        {
            tokens.push_back(Token(TOKEN_TARGET_DATA_X));
        }
        else if (cur == "data.y")
        {
            tokens.push_back(Token(TOKEN_TARGET_DATA_Y));
        }
        else if (ctx.isValidVariable(cur))
        {
            tokens.push_back(Token(TOKEN_VARIABLE, cur));
        }
        else
        {
            // Try to parse as a number first, otherwise treat as a user variable
            char *end;
            float val = std::strtof(cur.c_str(), &end);
            if (*end == '\0')
                tokens.push_back(Token(TOKEN_NUMBER, val));
            else
                tokens.push_back(Token(TOKEN_VARIABLE, cur));
        }
        cur.clear();
    };

    while (i < expr.size())
    {
        char c = expr[i];
        if (std::isspace(c))
        {
            i++;
            continue;
        }

        // ------------------------------------------------------------------
        // 1. Derivative D( ... )
        // ------------------------------------------------------------------
        if (c == 'D' && i + 1 < expr.size() && expr[i + 1] == '(')
        {
            flushIdentifier();
            size_t endPos;
            auto deriv = parseDerivativeCall(expr, i, endPos, ctx);
            bool neg = !tokens.empty() && tokens.back().type == TOKEN_NEG;
            if (neg)
                tokens.pop_back();
            if (neg)
            {
                tokens.push_back(Token(TOKEN_OPEN_PAREN));
                tokens.push_back(Token(TOKEN_NUMBER, 0.0f));
                tokens.push_back(Token(TOKEN_SUB));
                tokens.insert(tokens.end(), deriv.begin(), deriv.end());
                tokens.push_back(Token(TOKEN_CLOSE_PAREN));
            }
            else
            {
                tokens.insert(tokens.end(), deriv.begin(), deriv.end());
            }
            i = endPos + 1;
            continue;
        }

        // ------------------------------------------------------------------
        // 2. Object reference: identifier[number].property
        // ------------------------------------------------------------------
        if (!cur.empty() && i < expr.size() && expr[i] == '[')
        {
            // Parse index
            i++; // skip '['
            size_t idx_start = i;
            while (i < expr.size() && isdigit(expr[i]))
                i++;
            if (i >= expr.size() || expr[i] != ']')
                throw std::runtime_error("Expected ']' after index in object reference");
            int objIndex = std::stoi(expr.substr(idx_start, i - idx_start));
            i++; // skip ']'
            if (i >= expr.size() || expr[i] != '.')
                throw std::runtime_error("Expected '.' after object index");
            i++; // skip '.'
            size_t prop_start = i;
            while (i < expr.size() && (isalnum(expr[i]) || expr[i] == '.'))
                i++;
            std::string prop = expr.substr(prop_start, i - prop_start);
            Token tok(TOKEN_OBJECT_REF);
            tok.object_type = cur;
            tok.object_index = objIndex;
            tok.object_property = prop;
            tokens.push_back(tok);
            cur.clear();
            continue;
        }

        // ------------------------------------------------------------------
        // 3. Function call detection (only when identifier and next char is '(')
        // ------------------------------------------------------------------
        if (!cur.empty())
        {
            auto f = s_funcMap.find(cur);
            if (f != s_funcMap.end() && i < expr.size() && expr[i] == '(')
            {
                tokens.push_back(Token(f->second));
                cur.clear();
                // parse arguments
                auto parseArgs = [&](size_t &pos) -> std::vector<std::vector<Token>>
                {
                    if (expr[pos] != '(')
                        throw std::runtime_error("Expected (");
                    pos++;
                    std::vector<std::vector<Token>> args;
                    int argDepth = 1;
                    size_t argStart = pos;
                    while (pos < expr.size() && argDepth > 0)
                    {
                        char ch = expr[pos];
                        if (ch == '(')
                            argDepth++;
                        else if (ch == ')')
                            argDepth--;
                        else if (ch == ',' && argDepth == 1)
                        {
                            std::string argStr = expr.substr(argStart, pos - argStart);
                            args.push_back(tokenizeImpl(argStr, ctx, inSumNeighbors));
                            argStart = pos + 1;
                        }
                        pos++;
                    }
                    std::string lastArg = expr.substr(argStart, pos - argStart - 1);
                    args.push_back(tokenizeImpl(lastArg, ctx, inSumNeighbors));
                    return args;
                };
                size_t pos = i;
                auto args = parseArgs(pos);
                i = pos;
                // For TOKEN_COMP, we need to capture the integer indices as part of the token.
                if (f->second == TOKEN_COMP)
                {
                    // Expect at least 2 arguments: tensor + indices.
                    if (args.size() < 2)
                        throw std::runtime_error("comp() requires at least a tensor and one index");
                    // First argument is the tensor expression (any expression that yields a tensor).
                    // The remaining arguments must be integer literals.
                    Token compTok(TOKEN_COMP);
                    // Store the tensor argument as a sub‑block (its tokens).
                    compTok.comp_tensor_tokens = args[0];
                    // Store indices as integers (convert to 0‑based)
                    for (size_t idx = 1; idx < args.size(); ++idx)
                    {
                        if (args[idx].size() != 1 || args[idx][0].type != TOKEN_NUMBER)
                            throw std::runtime_error("comp() indices must be integer constants");
                        int index = (int)args[idx][0].numeric_value - 1; // convert user's 1‑based to 0‑based
                        if (index < 0)
                            throw std::runtime_error("comp() index must be >= 1");
                        compTok.comp_indices.push_back(index);
                    }
                    tokens.push_back(compTok);
                }
                else
                {
                    // Normal function: push all arguments as sub‑blocks (already done by tokenizer)
                    for (auto &arg : args)
                    {
                        tokens.insert(tokens.end(), arg.begin(), arg.end());
                    }
                }
                continue;
            }
        }

        // ------------------------------------------------------------------
        // 4. Tensor literal: '[' only when no pending identifier
        // ------------------------------------------------------------------
        if (c == '[' && cur.empty())
        {
            Token tensor = parseTensorLiteral(expr, i, ctx, inSumNeighbors);
            tokens.push_back(tensor);
            continue;
        }

        // ------------------------------------------------------------------
        // Multi-character operators (comparisons)
        // ------------------------------------------------------------------
        if (c == '<' || c == '>' || c == '=' || c == '!')
        {
            flushIdentifier();

            // Now check for two-character operators
            if (c == '<' && i + 1 < expr.size() && expr[i + 1] == '=')
            {
                tokens.push_back(Token(TOKEN_LE));
                i += 2;
                continue;
            }
            else if (c == '>' && i + 1 < expr.size() && expr[i + 1] == '=')
            {
                tokens.push_back(Token(TOKEN_GE));
                i += 2;
                continue;
            }
            else if (c == '=' && i + 1 < expr.size() && expr[i + 1] == '=')
            {
                tokens.push_back(Token(TOKEN_EQ));
                i += 2;
                continue;
            }
            else if (c == '!' && i + 1 < expr.size() && expr[i + 1] == '=')
            {
                tokens.push_back(Token(TOKEN_NE));
                i += 2;
                continue;
            }
            else if (c == '<')
            {
                tokens.push_back(Token(TOKEN_LT));
                i++;
                continue;
            }
            else if (c == '>')
            {
                tokens.push_back(Token(TOKEN_GT));
                i++;
                continue;
            }
            else if (c == '=')
            {
                // single '=' is not a comparison; it's assignment. Fall through.
            }
        }

        // ------------------------------------------------------------------
        // 5. Operators and punctuation
        // ------------------------------------------------------------------
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' ||
            c == '(' || c == ')' || c == '[' || c == ']' || c == ',' || c == '=' || c == ';')
        {
            flushIdentifier();
            switch (c)
            {
            case '+':
                tokens.push_back(Token(TOKEN_ADD));
                break;
            case '-':
            {
                bool unary = tokens.empty() || tokens.back().type == TOKEN_OPEN_PAREN ||
                             tokens.back().type == TOKEN_COMMA ||
                             tokens.back().type == TOKEN_ADD || tokens.back().type == TOKEN_SUB ||
                             tokens.back().type == TOKEN_MUL || tokens.back().type == TOKEN_DIV ||
                             tokens.back().type == TOKEN_POW;
                tokens.push_back(Token(unary ? TOKEN_NEG : TOKEN_SUB));
                break;
            }
            case '*':
                tokens.push_back(Token(TOKEN_MUL));
                break;
            case '/':
                tokens.push_back(Token(TOKEN_DIV));
                break;
            case '^':
                tokens.push_back(Token(TOKEN_POW));
                break;
            case '(':
                tokens.push_back(Token(TOKEN_OPEN_PAREN));
                break;
            case ')':
                tokens.push_back(Token(TOKEN_CLOSE_PAREN));
                break;
            case '[':
            case ']':
                throw std::runtime_error("Unexpected bracket");
            case ',':
                tokens.push_back(Token(TOKEN_COMMA));
                break;
            case '=':
                tokens.push_back(Token(TOKEN_EQUAL));
                break;
            case ';':
                tokens.push_back(Token(TOKEN_SEMICOLON));
                break;
            }
            i++;
            continue;
        }

        // ------------------------------------------------------------------
        // 6. Otherwise accumulate into current identifier
        // ------------------------------------------------------------------
        cur += c;
        i++;
    }

    // Flush final identifier
    flushIdentifier();
    return tokens;
}

std::vector<Token> tokenizeExpression(const std::string &expr, const ParserContext &ctx)
{
    return tokenizeImpl(expr, ctx, false);
}

// ============================================================================
// INFIX TO RPN (SHUNTING YARD)
// ============================================================================

// Operator info structure with precedence and associativity
struct OperatorInfo
{
    int precedence;
    bool rightAssociative;
};

static const std::unordered_map<TokenType, OperatorInfo> s_operatorInfo = {
    {TOKEN_ADD, {2, false}},
    {TOKEN_SUB, {2, false}},
    {TOKEN_MUL, {3, false}},
    {TOKEN_DIV, {3, false}},
    {TOKEN_POW, {5, true}}, // right-associative
    {TOKEN_LT, {2, false}},
    {TOKEN_LE, {2, false}},
    {TOKEN_GT, {2, false}},
    {TOKEN_GE, {2, false}},
    {TOKEN_EQ, {2, false}},
    {TOKEN_NE, {2, false}},
    {TOKEN_DOT, {3, false}}, // dot product has same precedence as multiplication
    {TOKEN_CROSS, {3, false}},
};

std::vector<Token> infixToRPN(const std::vector<Token> &infix)
{
    std::vector<Token> output;
    std::vector<Token> stack;

    for (const auto &token : infix)
    {
        // Operands go directly to output
        if (token.type == TOKEN_NUMBER || token.type == TOKEN_VARIABLE ||
            token.type == TOKEN_OBJECT_REF || token.type == TOKEN_DERIVATIVE ||
            token.type == TOKEN_TENSOR_LIT || token.type == TOKEN_NEIGHBOR_INDEX)
        {
            output.push_back(token);
        }
        // Functions (including unary operators) go on stack
        else if (s_arity.count(token.type))
        {
            stack.push_back(token);
        }
        // Comma: pop until we hit open paren
        else if (token.type == TOKEN_COMMA)
        {
            while (!stack.empty() && stack.back().type != TOKEN_OPEN_PAREN)
            {
                output.push_back(stack.back());
                stack.pop_back();
            }
            if (stack.empty())
            {
                throw std::runtime_error("Misplaced comma or mismatched parentheses");
            }
        }
        // Open paren: push to stack
        else if (token.type == TOKEN_OPEN_PAREN)
        {
            stack.push_back(token);
        }
        // Close paren: pop until matching open paren
        else if (token.type == TOKEN_CLOSE_PAREN)
        {
            while (!stack.empty() && stack.back().type != TOKEN_OPEN_PAREN)
            {
                output.push_back(stack.back());
                stack.pop_back();
            }
            if (stack.empty())
            {
                throw std::runtime_error("Mismatched parentheses");
            }
            stack.pop_back(); // Remove the open paren

            // If there's a function on top of stack, pop it too
            if (!stack.empty() && s_arity.count(stack.back().type))
            {
                output.push_back(stack.back());
                stack.pop_back();
            }
        }
        // Binary operators
        else
        {
            auto op_it = s_operatorInfo.find(token.type);
            if (op_it == s_operatorInfo.end())
            {
                // Unknown operator, just add to output (should not happen)
                output.push_back(token);
                continue;
            }

            int currentPrec = op_it->second.precedence;
            bool rightAssoc = op_it->second.rightAssociative;

            // Pop operators with higher precedence (or equal if left-associative)
            while (!stack.empty())
            {
                auto stack_op_it = s_operatorInfo.find(stack.back().type);
                if (stack_op_it == s_operatorInfo.end())
                    break;

                int stackPrec = stack_op_it->second.precedence;
                bool shouldPop;
                if (rightAssoc)
                {
                    shouldPop = (stackPrec > currentPrec);
                }
                else
                {
                    shouldPop = (stackPrec >= currentPrec);
                }
                if (!shouldPop)
                    break;

                output.push_back(stack.back());
                stack.pop_back();
            }
            stack.push_back(token);
        }
    }

    // Pop remaining operators
    while (!stack.empty())
    {
        if (stack.back().type == TOKEN_OPEN_PAREN)
        {
            throw std::runtime_error("Mismatched parentheses");
        }
        output.push_back(stack.back());
        stack.pop_back();
    }

    return output;
}

// ============================================================================
// RECURSIVE LET INLINING (supports tensors)
// ============================================================================

struct Symbol
{
    enum Kind
    {
        CONSTANT,
        EXPR,
        TENSOR // a tensor literal
    } kind;
    float value;            // for CONSTANT
    std::vector<Token> rpn; // for EXPR
    Token tensorValue;      // for TENSOR (a TOKEN_TENSOR_LIT)
};

static std::vector<Token> substituteTokens(const std::vector<Token> &tokens,
                                           const std::unordered_map<std::string, Symbol> &symtab)
{
    std::vector<Token> result;
    for (const auto &tok : tokens)
    {
        if (tok.type == TOKEN_VARIABLE && symtab.count(tok.variable_name))
        {
            const Symbol &sym = symtab.at(tok.variable_name);
            if (sym.kind == Symbol::CONSTANT)
            {
                result.push_back(Token(TOKEN_NUMBER, sym.value));
            }
            else if (sym.kind == Symbol::EXPR)
            {
                // Directly substitute the RPN tokens without wrapping in parentheses, a bug that took me too long to find >_<
                auto inlined = substituteTokens(sym.rpn, symtab);
                result.insert(result.end(), inlined.begin(), inlined.end());
            }
            else if (sym.kind == Symbol::TENSOR)
            {
                // Replace with the stored tensor literal
                result.push_back(sym.tensorValue);
            }
        }
        else if (tok.type == TOKEN_TENSOR_LIT)
        {
            Token newTok = tok;
            newTok.tensor_components.clear();
            for (const auto &compList : tok.tensor_components)
            {
                newTok.tensor_components.push_back(substituteTokens(compList, symtab));
            }
            result.push_back(newTok);
        }
        else if (tok.type == TOKEN_SUM_NEIGHBORS)
        {
            Token newTok = tok;
            newTok.sum_weight = substituteTokens(tok.sum_weight, symtab);
            newTok.sum_body = substituteTokens(tok.sum_body, symtab);
            result.push_back(newTok);
        }
        else if (tok.type == TOKEN_SELECT)
        {
            Token newTok = tok;
            newTok.select_cond = substituteTokens(tok.select_cond, symtab);
            newTok.select_a = substituteTokens(tok.select_a, symtab);
            newTok.select_b = substituteTokens(tok.select_b, symtab);
            result.push_back(newTok);
        }
        else if (tok.type == TOKEN_DERIVATIVE)
        {
            Token newTok = tok;
            newTok.derivative_expr_tokens = substituteTokens(tok.derivative_expr_tokens, symtab);
            result.push_back(newTok);
        }
        else if (tok.type == TOKEN_COMP)
        {
            Token newTok = tok;
            // Substitute inside the tensor argument (first sub‑block)
            newTok.comp_tensor_tokens = substituteTokens(tok.comp_tensor_tokens, symtab);

            newTok.comp_indices = tok.comp_indices;
            result.push_back(newTok);
        }
        else
        {
            result.push_back(tok);
        }
    }
    return result;
}

static void inlineLetStatements(ParsedEquation &eq)
{
    std::unordered_map<std::string, Symbol> symtab;

    // First pass: collect all let bindings
    for (const auto &stmt : eq.statements)
    {
        if (stmt.type == Statement::LET)
        {
            Symbol sym;
            // Check if the RPN is a single tensor literal
            if (stmt.rpn.size() == 1 && stmt.rpn[0].type == TOKEN_TENSOR_LIT)
            {
                sym.kind = Symbol::TENSOR;
                sym.tensorValue = stmt.rpn[0];
                // Substitute already-known symbols into the tensor components
                Token substituted = sym.tensorValue;
                substituted.tensor_components.clear();
                for (const auto &comp : sym.tensorValue.tensor_components)
                    substituted.tensor_components.push_back(substituteTokens(comp, symtab));
                sym.tensorValue = substituted;
            }
            else if (stmt.rpn.size() == 1 && stmt.rpn[0].type == TOKEN_NUMBER)
            {
                sym.kind = Symbol::CONSTANT;
                sym.value = stmt.rpn[0].numeric_value;
            }
            else
            {
                sym.kind = Symbol::EXPR;
                sym.rpn = substituteTokens(stmt.rpn, symtab); // inline bound vars
            }
            symtab[stmt.lhs] = sym;
        }
    }

    // Second pass: substitute in all non-let statements
    for (auto &stmt : eq.statements)
    {
        if (stmt.type == Statement::ASSIGN || stmt.type == Statement::EXPR)
        {
            stmt.rpn = substituteTokens(stmt.rpn, symtab);
        }
    }
}

// ============================================================================
// LOWER STATEMENTS TO OLD BUFFERS (backward compatibility)
// ============================================================================

static void lowerToOldBuffers(const ParsedEquation &eq,
                              std::vector<Token> &out_ax, std::vector<Token> &out_ay,
                              std::vector<Token> &out_ang,
                              std::vector<Token> &out_r, std::vector<Token> &out_g,
                              std::vector<Token> &out_b, std::vector<Token> &out_a)
{
    out_ax.clear();
    out_ay.clear();
    out_ang.clear();
    out_r.clear();
    out_g.clear();
    out_b.clear();
    out_a.clear();

    for (const auto &s : eq.statements)
    {
        if (s.type == Statement::ASSIGN)
        {
            switch (s.targetCode)
            {
            case TOKEN_TARGET_AX:
                out_ax = s.rpn;
                break;
            case TOKEN_TARGET_AY:
                out_ay = s.rpn;
                break;
            case TOKEN_TARGET_ANGULAR:
                out_ang = s.rpn;
                break;
            case TOKEN_TARGET_COLOR_R:
                out_r = s.rpn;
                break;
            case TOKEN_TARGET_COLOR_G:
                out_g = s.rpn;
                break;
            case TOKEN_TARGET_COLOR_B:
                out_b = s.rpn;
                break;
            case TOKEN_TARGET_COLOR_A:
                out_a = s.rpn;
                break;
            default:
                break;
            }
        }
    }
}

// ============================================================================
// STATEMENT PARSER (LET, ASSIGN, EXPR)
// ============================================================================

static std::vector<Statement> parseStatements(const std::string &code, const ParserContext &ctx)
{
    std::vector<Statement> stmts;
    std::string remaining = code;

    while (!remaining.empty())
    {
        int depth = 0;
        size_t end = 0;

        // Find statement delimiter (; or newline at depth 0)
        // Track both parentheses and brackets to avoid false delimiters
        for (; end < remaining.size(); ++end)
        {
            char c = remaining[end];
            if (c == '(' || c == '[')
                depth++;
            else if (c == ')' || c == ']')
                depth--;
            else if (depth == 0 && (c == ';' || c == '\n'))
                break;
        }

        std::string stmt = trim(remaining.substr(0, end));
        if (end < remaining.size())
            remaining = remaining.substr(end + 1);
        else
            remaining.clear();
        if (stmt.empty())
            continue;

        // Parse 'let var = expr'
        if (stmt.substr(0, 3) == "let" && (stmt.size() == 3 || !std::isalnum(stmt[3])))
        {
            size_t eq = stmt.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("Invalid let");
            std::string lhs = trim(stmt.substr(3, eq - 3));
            std::string rhs = trim(stmt.substr(eq + 1));
            auto tokens = tokenizeExpression(rhs, ctx);
            auto rpn = infixToRPN(tokens);
            Statement s;
            s.type = Statement::LET;
            s.lhs = lhs;
            s.rpn = rpn;
            stmts.push_back(s);
            continue;
        }

        // Parse assignment 'target = expr'
        if (stmt.find('=') != std::string::npos)
        {
            size_t eq = stmt.find('=');
            std::string lhs = trim(stmt.substr(0, eq));
            std::string rhs = trim(stmt.substr(eq + 1));
            auto tokens = tokenizeExpression(rhs, ctx);
            auto rpn = infixToRPN(tokens);
            Statement s;
            s.type = Statement::ASSIGN;
            s.lhs = lhs;

            // Map target names to token codes
            if (lhs == "ax")
                s.targetCode = TOKEN_TARGET_AX;
            else if (lhs == "ay")
                s.targetCode = TOKEN_TARGET_AY;
            else if (lhs == "angular")
                s.targetCode = TOKEN_TARGET_ANGULAR;
            else if (lhs == "color.r")
                s.targetCode = TOKEN_TARGET_COLOR_R;
            else if (lhs == "color.g")
                s.targetCode = TOKEN_TARGET_COLOR_G;
            else if (lhs == "color.b")
                s.targetCode = TOKEN_TARGET_COLOR_B;
            else if (lhs == "color.a")
                s.targetCode = TOKEN_TARGET_COLOR_A;
            else if (lhs == "size")
                s.targetCode = TOKEN_TARGET_SIZE;
            else if (lhs == "data.x")
                s.targetCode = TOKEN_TARGET_DATA_X;
            else if (lhs == "data.y")
                s.targetCode = TOKEN_TARGET_DATA_Y;
            else
                throw std::runtime_error("Unknown target: " + lhs);

            s.rpn = rpn;
            stmts.push_back(s);
            continue;
        }

        // Parse standalone expression
        auto tokens = tokenizeExpression(stmt, ctx);
        auto rpn = infixToRPN(tokens);
        Statement s;
        s.type = Statement::EXPR;
        s.rpn = rpn;
        stmts.push_back(s);
    }
    return stmts;
}

// ============================================================================
// MAIN PARSE EQUATION
// ============================================================================

/**
 * Parse an equation string into a ParsedEquation structure.
 * Supports both legacy comma-separated format and new statement-based format.
 *
 * Legacy format: "ax_expr, ay_expr, angular_expr, r_expr, g_expr, b_expr, a_expr"
 * New format: statements separated by ';' or newlines, supporting 'let' bindings and assignments.
 */
ParsedEquation ParseEquation(const std::string &str, const ParserContext &ctx)
{
    ParsedEquation result;
    bool newStyle = false;
    int depth = 0;

    // Detect if this is new-style (contains '=' or 'let' at top level)
    for (size_t i = 0; i < str.size(); ++i)
    {
        char c = str[i];
        if (c == '(')
            depth++;
        else if (c == ')')
            depth--;
        else if (depth == 0)
        {
            if (c == '=')
            {
                newStyle = true;
                break;
            }
            if (c == 'l' && i + 3 <= str.size() && str.substr(i, 3) == "let" &&
                (i + 3 == str.size() || !std::isalnum(str[i + 3])))
            {
                newStyle = true;
                break;
            }
        }
    }

    if (newStyle)
    {
        // Parse new statement-based format
        result.statements = parseStatements(str, ctx);
        inlineLetStatements(result);
        lowerToOldBuffers(result,
                          result.tokens_ax, result.tokens_ay, result.tokens_angular,
                          result.tokens_r, result.tokens_g, result.tokens_b, result.tokens_a);
    }
    else
    {
        // Parse legacy comma-separated format
        std::vector<std::string> parts;
        std::string cur;
        depth = 0;
        for (char c : str)
        {
            if (c == '(')
                depth++;
            else if (c == ')')
                depth--;
            else if (c == ',' && depth == 0)
            {
                parts.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (!cur.empty())
            parts.push_back(trim(cur));

        // Map legacy parts to target buffers (ax, ay, angular, r, g, b, a)
        if (parts.size() > 0 && !parts[0].empty())
        {
            auto t = tokenizeExpression(parts[0], ctx);
            result.tokens_ax = infixToRPN(t);
        }
        if (parts.size() > 1 && !parts[1].empty())
        {
            auto t = tokenizeExpression(parts[1], ctx);
            result.tokens_ay = infixToRPN(t);
        }
        if (parts.size() > 2 && !parts[2].empty())
        {
            auto t = tokenizeExpression(parts[2], ctx);
            result.tokens_angular = infixToRPN(t);
        }
        if (parts.size() > 3 && !parts[3].empty())
        {
            auto t = tokenizeExpression(parts[3], ctx);
            result.tokens_r = infixToRPN(t);
        }
        if (parts.size() > 4 && !parts[4].empty())
        {
            auto t = tokenizeExpression(parts[4], ctx);
            result.tokens_g = infixToRPN(t);
        }
        if (parts.size() > 5 && !parts[5].empty())
        {
            auto t = tokenizeExpression(parts[5], ctx);
            result.tokens_b = infixToRPN(t);
        }
        if (parts.size() > 6 && !parts[6].empty())
        {
            auto t = tokenizeExpression(parts[6], ctx);
            result.tokens_a = infixToRPN(t);
        }
    }

    // Extract all numeric constants for external use
    auto extract = [&](const std::vector<Token> &tokens)
    {
        for (const auto &t : tokens)
            if (t.type == TOKEN_NUMBER)
                result.constants.push_back(t.numeric_value);
    };
    extract(result.tokens_ax);
    extract(result.tokens_ay);
    extract(result.tokens_angular);
    extract(result.tokens_r);
    extract(result.tokens_g);
    extract(result.tokens_b);
    extract(result.tokens_a);

    return result;
}