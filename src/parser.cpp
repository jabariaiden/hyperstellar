#include "parser.h"
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include <set>
#include <cmath>

// ============================================================================
// PARSER CONTEXT IMPLEMENTATION
// ============================================================================

ParserContext::ParserContext()
{
    // Register core variables
    registerVariable("x", DOMAIN_SPATIAL);
    registerVariable("y", DOMAIN_SPATIAL);
    registerVariable("t", DOMAIN_TIME);
    registerVariable("theta", DOMAIN_ROTATIONAL, true);

    // Color variables
    registerVariable("r", DOMAIN_COLOR);
    registerVariable("g", DOMAIN_COLOR);
    registerVariable("b", DOMAIN_COLOR);
    registerVariable("a", DOMAIN_COLOR);
    registerVariable("h", DOMAIN_COLOR);
    registerVariable("s", DOMAIN_COLOR);
    registerVariable("v", DOMAIN_COLOR);

    // Velocity and acceleration
    registerVariable("vx", DOMAIN_SPATIAL);
    registerVariable("vy", DOMAIN_SPATIAL);
    registerVariable("ax", DOMAIN_SPATIAL);
    registerVariable("ay", DOMAIN_SPATIAL);

    // Angular variables
    registerVariable("omega", DOMAIN_ROTATIONAL);  // angular velocity
    registerVariable("alpha", DOMAIN_ROTATIONAL);  // angular acceleration

    // Complex variables
    registerVariable("i", DOMAIN_COMPLEX, false);

    // Mathematical constants
    registerVariable("pi", DOMAIN_SCALAR, false);
    registerVariable("e", DOMAIN_SCALAR, false);

    // Physics constants (uniforms from shader)
    registerVariable("k", DOMAIN_SCALAR, false);
    registerVariable("damping", DOMAIN_SCALAR, false);
    registerVariable("gravity", DOMAIN_SCALAR, false);
    registerVariable("mass", DOMAIN_SCALAR, false);
    registerVariable("charge", DOMAIN_SCALAR, false);
    registerVariable("coupling", DOMAIN_SCALAR, false);
    registerVariable("freq", DOMAIN_SCALAR, false);
    registerVariable("amp", DOMAIN_SCALAR, false);

    // Polar coordinates
    registerVariable("radius", DOMAIN_SPATIAL, true);

    // FIXED: Register object types with ALL properties including color
    registerObjectType("p", {
        "x", "y", "vx", "vy", "ax", "ay", "mass", "charge",
        "data.x", "data.y", "data.z", "data.w",
        "color.r", "color.g", "color.b", "color.a"  // NEW: Color properties
    });
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
// OPERATOR PRECEDENCE AND ASSOCIATIVITY
// ============================================================================

namespace
{
    struct OperatorInfo {
        int precedence;
        bool rightAssociative;
    };

    // Operator precedence and associativity
    const std::unordered_map<TokenType, OperatorInfo> s_operatorInfo = {
        {TOKEN_ADD, {2, false}},
        {TOKEN_SUB, {2, false}},
        {TOKEN_MUL, {3, false}},
        {TOKEN_DIV, {3, false}},
        {TOKEN_NEG, {4, true}},   // Unary minus
        {TOKEN_POW, {5, true}},   // RIGHT ASSOCIATIVE: 2^3^2 = 2^(3^2)
    };

    // Function arity
    const std::unordered_map<TokenType, int> s_functionArity = {
        {TOKEN_NEG, 1}, 
        {TOKEN_SIN, 1}, 
        {TOKEN_COS, 1}, 
        {TOKEN_TAN, 1}, 
        {TOKEN_SQRT, 1}, 
        {TOKEN_LOG, 1},
        {TOKEN_EXP, 1},
        {TOKEN_ABS, 1},
        {TOKEN_FLOOR, 1},
        {TOKEN_CEIL, 1},
        {TOKEN_FRAC, 1},
        {TOKEN_SIGN, 1},
        {TOKEN_STEP, 1},
        {TOKEN_REAL, 1}, 
        {TOKEN_IMAG, 1}, 
        {TOKEN_CONJ, 1}, 
        {TOKEN_ARG, 1},
        {TOKEN_MIN, 2},
        {TOKEN_MAX, 2},
        {TOKEN_MOD, 2},
        {TOKEN_ATAN2, 2},
        {TOKEN_CLAMP, 3}
    };

    // Built-in function mapping
    const std::unordered_map<std::string, TokenType> s_functionMap = {
        {"sin", TOKEN_SIN}, 
        {"cos", TOKEN_COS}, 
        {"tan", TOKEN_TAN}, 
        {"sqrt", TOKEN_SQRT}, 
        {"log", TOKEN_LOG}, 
        {"exp", TOKEN_EXP}, 
        {"abs", TOKEN_ABS}, 
        {"min", TOKEN_MIN}, 
        {"max", TOKEN_MAX}, 
        {"clamp", TOKEN_CLAMP}, 
        {"floor", TOKEN_FLOOR}, 
        {"ceil", TOKEN_CEIL}, 
        {"frac", TOKEN_FRAC}, 
        {"mod", TOKEN_MOD}, 
        {"atan2", TOKEN_ATAN2}, 
        {"real", TOKEN_REAL}, 
        {"imag", TOKEN_IMAG}, 
        {"conj", TOKEN_CONJ}, 
        {"arg", TOKEN_ARG}, 
        {"sign", TOKEN_SIGN}, 
        {"step", TOKEN_STEP}
    };

    // Helper function to trim whitespace
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }
}

// ============================================================================
// DERIVATIVE PARSER
// ============================================================================

std::vector<Token> parseDerivativeCall(const std::string &expression, size_t start_pos, size_t &end_pos, const ParserContext &context)
{
    if (start_pos + 1 >= expression.length() || expression.substr(start_pos, 2) != "D(")
    {
        throw std::runtime_error("Invalid derivative syntax: expected 'D('");
    }

    size_t pos = start_pos + 2;
    int paren_depth = 1;
    std::string expr_str;
    std::string wrt_var;
    int order = 1;

    // Parse the expression inside D(...)
    while (pos < expression.length() && paren_depth > 0)
    {
        char c = expression[pos];
        if (c == '(')
        {
            paren_depth++;
            expr_str += c;
        }
        else if (c == ')')
        {
            paren_depth--;
            if (paren_depth > 0)
            {
                expr_str += c;
            }
        }
        else if (paren_depth == 1 && c == ',')
        {
            break; // Found separator between expression and variable
        }
        else
        {
            expr_str += c;
        }
        pos++;
    }

    if (pos >= expression.length() || paren_depth != 1)
    {
        throw std::runtime_error("Unclosed derivative call");
    }

    // Parse the with-respect-to variable
    pos++; // Skip comma
    while (pos < expression.length() && std::isspace(expression[pos]))
        pos++;

    size_t var_start = pos;
    while (pos < expression.length() && expression[pos] != ',' && expression[pos] != ')')
    {
        pos++;
    }
    wrt_var = expression.substr(var_start, pos - var_start);

    // Trim whitespace
    wrt_var = trim(wrt_var);

    // Validate the variable exists and is differentiable
    if (!context.isValidDerivativeWRT(wrt_var))
    {
        throw std::runtime_error("Cannot take derivative with respect to: " + wrt_var);
    }

    // Parse optional order
    if (pos < expression.length() && expression[pos] == ',')
    {
        pos++; // Skip comma
        while (pos < expression.length() && std::isspace(expression[pos]))
            pos++;

        size_t order_start = pos;
        while (pos < expression.length() && expression[pos] != ')')
        {
            pos++;
        }
        std::string order_str = expression.substr(order_start, pos - order_start);
        order_str = trim(order_str);

        try
        {
            order = std::stoi(order_str);
            if (order < 1 || order > 4)
            {
                throw std::runtime_error("Derivative order must be between 1 and 4");
            }
        }
        catch (...)
        {
            throw std::runtime_error("Invalid derivative order: " + order_str);
        }
    }

    if (pos >= expression.length() || expression[pos] != ')')
    {
        throw std::runtime_error("Unclosed derivative call");
    }

    end_pos = pos;

    // Parse the expression to differentiate
    auto expr_tokens = tokenizeExpression(expr_str, context);
    auto rpn_expr = infixToRPN(expr_tokens);

    // Create NUMERICAL derivative instruction token
    Token deriv_token(TOKEN_DERIVATIVE);
    deriv_token.derivative_wrt = wrt_var;
    deriv_token.derivative_order = order;
    deriv_token.derivative_method = DERIV_METHOD_NUMERICAL;
    deriv_token.derivative_expr_tokens = rpn_expr;

    std::vector<Token> result;
    result.push_back(deriv_token);

    return result;
}

// ============================================================================
// TOKENIZER
// ============================================================================

std::vector<Token> tokenizeExpression(const std::string &expression, const ParserContext &context)
{
    std::vector<Token> tokens;
    std::string currentLexeme;

    auto flushLexeme = [&]()
    {
        if (currentLexeme.empty())
            return;

        // Check if it's a function
        auto funcIt = s_functionMap.find(currentLexeme);
        if (funcIt != s_functionMap.end())
        {
            tokens.push_back(Token(funcIt->second));
            currentLexeme.clear();
            return;
        }

        // Check if it's a known variable
        if (context.isValidVariable(currentLexeme))
        {
            tokens.push_back(Token(TOKEN_VARIABLE, currentLexeme));
            currentLexeme.clear();
            return;
        }

        // Check if it's a number
        try
        {
            float value = std::stof(currentLexeme);
            tokens.push_back(Token(TOKEN_NUMBER, value));
            currentLexeme.clear();
            return;
        }
        catch (...)
        {
            // Not a number
        }

        throw std::runtime_error("Unknown token: " + currentLexeme);
    };

    for (size_t i = 0; i < expression.length(); ++i)
    {
        char c = expression[i];

        if (std::isspace(c))
        {
            flushLexeme();
            continue;
        }

        // Handle object references (p[0].x)
        if (c == 'p' && i + 1 < expression.length() && expression[i + 1] == '[')
        {
            flushLexeme();

            size_t bracketEnd = expression.find(']', i + 2);
            if (bracketEnd == std::string::npos)
            {
                throw std::runtime_error("Unclosed bracket in object reference");
            }

            std::string indexStr = expression.substr(i + 2, bracketEnd - i - 2);
            int index = std::stoi(indexStr);

            if (bracketEnd + 1 >= expression.length() || expression[bracketEnd + 1] != '.')
            {
                throw std::runtime_error("Missing property in object reference");
            }

            size_t propStart = bracketEnd + 2;
            size_t propEnd = propStart;
            while (propEnd < expression.length() && (std::isalnum(expression[propEnd]) || expression[propEnd] == '.'))
            {
                propEnd++;
            }

            std::string property = expression.substr(propStart, propEnd - propStart);

            Token objToken(TOKEN_OBJECT_REF);
            objToken.object_type = "p";
            objToken.object_index = index;
            objToken.object_property = property;
            tokens.push_back(objToken);

            i = propEnd - 1;
            continue;
        }

        // Handle derivative calls D(expr, var, order)
        if (c == 'D' && i + 1 < expression.length() && expression[i + 1] == '(')
        {
            // Make sure D is not part of a larger identifier
            if (i > 0 && (std::isalnum(expression[i - 1]) || expression[i - 1] == '_'))
            {
                currentLexeme += c;
                continue;
            }

            flushLexeme();

            // Check if we need to handle negation
            bool shouldNegate = false;
            if (!tokens.empty() && tokens.back().type == TOKEN_NEG)
            {
                tokens.pop_back();
                shouldNegate = true;
            }

            try
            {
                size_t end_pos;
                auto derivative_tokens = parseDerivativeCall(expression, i, end_pos, context);
                
                if (shouldNegate) {
                    // Create infix: ( 0 - D(...) )
                    // The Shunting Yard will handle converting to proper RPN
                    tokens.push_back(Token(TOKEN_OPEN_PAREN));
                    tokens.push_back(Token(TOKEN_NUMBER, 0.0f));
                    tokens.push_back(Token(TOKEN_SUB));  // SUB goes between 0 and D
                    tokens.insert(tokens.end(), derivative_tokens.begin(), derivative_tokens.end());
                    tokens.push_back(Token(TOKEN_CLOSE_PAREN));
                } else {
                    tokens.insert(tokens.end(), derivative_tokens.begin(), derivative_tokens.end());
                }
                
                i = end_pos;
                continue;
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error(std::string("Derivative parsing failed: ") + e.what());
            }
        }

        // Handle operators and punctuation
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' ||
            c == '(' || c == ')' || c == ',')
        {
            flushLexeme();

            switch (c)
            {
            case '+':
                tokens.push_back(Token(TOKEN_ADD));
                break;
            case '-':
            {
                // Determine if this is unary negation or binary subtraction
                bool isNegation = tokens.empty() ||
                                  tokens.back().type == TOKEN_OPEN_PAREN ||
                                  tokens.back().type == TOKEN_COMMA ||
                                  tokens.back().type == TOKEN_ADD ||
                                  tokens.back().type == TOKEN_SUB ||
                                  tokens.back().type == TOKEN_MUL ||
                                  tokens.back().type == TOKEN_DIV ||
                                  tokens.back().type == TOKEN_POW;
                tokens.push_back(Token(isNegation ? TOKEN_NEG : TOKEN_SUB));
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
            case ',':
                tokens.push_back(Token(TOKEN_COMMA));
                break;
            }
        }
        else
        {
            currentLexeme += c;
        }
    }

    flushLexeme();
    return tokens;
}

// ============================================================================
// SHUNTING YARD ALGORITHM (WITH RIGHT-ASSOCIATIVE POWER)
// ============================================================================

std::vector<Token> infixToRPN(const std::vector<Token> &infixTokens)
{
    std::vector<Token> output;
    std::vector<Token> stack;

    for (const auto &token : infixTokens)
    {
        // Operands go directly to output
        if (token.type == TOKEN_NUMBER || token.type == TOKEN_VARIABLE ||
            token.type == TOKEN_OBJECT_REF || token.type == TOKEN_DERIVATIVE)
        {
            output.push_back(token);
        }
        // Functions (including unary operators) go on stack
        else if (s_functionArity.count(token.type))
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
            if (!stack.empty() && s_functionArity.count(stack.back().type))
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
                // Unknown operator, just add to output
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
                
                // For right-associative: only pop if stack has STRICTLY higher precedence
                // For left-associative: pop if stack has higher OR EQUAL precedence
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
// MAIN PARSER FUNCTION (EXTENDED FOR ROTATION AND COLOR)
// ============================================================================

ParsedEquation ParseEquation(const std::string &equation_string, const ParserContext &context)
{
    ParsedEquation result;

    // Smart comma splitting that respects parentheses
    std::vector<std::string> expressions;
    std::string current;
    int depth = 0;
    
    for (char c : equation_string) {
        if (c == '(') {
            depth++;
            current += c;
        }
        else if (c == ')') {
            depth--;
            current += c;
        }
        else if (c == ',' && depth == 0) {
            // Only split on commas at depth 0 (top-level commas between components)
            expressions.push_back(trim(current));
            current.clear();
        }
        else {
            current += c;
        }
    }
    
    // Don't forget the last expression
    if (!current.empty()) {
        expressions.push_back(trim(current));
    }

    // Parse AX expression (required)
    if (expressions.size() > 0 && !expressions[0].empty())
    {
        auto tokens = tokenizeExpression(expressions[0], context);
        result.tokens_ax = infixToRPN(tokens);
    }

    // Parse AY expression (required)
    if (expressions.size() > 1 && !expressions[1].empty())
    {
        auto tokens = tokenizeExpression(expressions[1], context);
        result.tokens_ay = infixToRPN(tokens);
    }

    // Parse Angular Acceleration (optional)
    if (expressions.size() > 2 && !expressions[2].empty())
    {
        auto tokens = tokenizeExpression(expressions[2], context);
        result.tokens_angular = infixToRPN(tokens);
    }

    // Parse Color R (optional)
    if (expressions.size() > 3 && !expressions[3].empty())
    {
        auto tokens = tokenizeExpression(expressions[3], context);
        result.tokens_r = infixToRPN(tokens);
    }

    // Parse Color G (optional)
    if (expressions.size() > 4 && !expressions[4].empty())
    {
        auto tokens = tokenizeExpression(expressions[4], context);
        result.tokens_g = infixToRPN(tokens);
    }

    // Parse Color B (optional)
    if (expressions.size() > 5 && !expressions[5].empty())
    {
        auto tokens = tokenizeExpression(expressions[5], context);
        result.tokens_b = infixToRPN(tokens);
    }

    // Parse Color A (optional)
    if (expressions.size() > 6 && !expressions[6].empty())
    {
        auto tokens = tokenizeExpression(expressions[6], context);
        result.tokens_a = infixToRPN(tokens);
    }

    // Extract constants from all components
    auto extractConstants = [&result](const std::vector<Token>& tokens) {
        for (const auto &token : tokens) {
            if (token.type == TOKEN_NUMBER) {
                result.constants.push_back(token.numeric_value);
            }
        }
    };

    extractConstants(result.tokens_ax);
    extractConstants(result.tokens_ay);
    extractConstants(result.tokens_angular);
    extractConstants(result.tokens_r);
    extractConstants(result.tokens_g);
    extractConstants(result.tokens_b);
    extractConstants(result.tokens_a);

    return result;
}