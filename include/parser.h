#pragma once

#include <string>
#include <vector>
#include <unordered_map>

// ============================================================================
// TOKEN TYPES
// ============================================================================
enum TokenType {
    TOKEN_NUMBER,
    TOKEN_VARIABLE,
    TOKEN_OBJECT_REF,
    TOKEN_ADD,
    TOKEN_SUB,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_NEG,
    TOKEN_POW,
    TOKEN_SIN,
    TOKEN_COS,
    TOKEN_TAN,
    TOKEN_SQRT,
    TOKEN_LOG,
    TOKEN_EXP,
    TOKEN_ABS,
    TOKEN_MIN,
    TOKEN_MAX,
    TOKEN_CLAMP,
    TOKEN_FLOOR,
    TOKEN_CEIL,
    TOKEN_FRAC,
    TOKEN_MOD,
    TOKEN_ATAN2,
    TOKEN_REAL,
    TOKEN_IMAG,
    TOKEN_CONJ,
    TOKEN_ARG,
    TOKEN_SIGN,
    TOKEN_STEP,
    TOKEN_OPEN_PAREN,
    TOKEN_CLOSE_PAREN,
    TOKEN_COMMA,
    TOKEN_DERIVATIVE
};

// ============================================================================
// DERIVATIVE METHODS
// ============================================================================
enum DerivativeMethod {
    DERIV_METHOD_NUMERICAL = 0,
    DERIV_METHOD_SYMBOLIC = 1
};

// ============================================================================
// TOKEN STRUCTURE
// ============================================================================
struct Token {
    TokenType type;
    
    // For TOKEN_NUMBER
    float numeric_value = 0.0f;
    
    // For TOKEN_VARIABLE
    std::string variable_name;
    
    // For TOKEN_OBJECT_REF (e.g., p[0].x)
    std::string object_type;
    int object_index = -1;
    std::string object_property;
    
    // For TOKEN_DERIVATIVE
    std::string derivative_wrt;
    int derivative_order = 1;
    DerivativeMethod derivative_method = DERIV_METHOD_NUMERICAL;
    std::vector<Token> derivative_expr_tokens;  // ADDED: Expression to differentiate
    
    // Constructors
    Token() : type(TOKEN_NUMBER), numeric_value(0.0f) {}
    
    explicit Token(TokenType t) : type(t), numeric_value(0.0f) {}
    
    Token(TokenType t, float value) : type(t), numeric_value(value) {}
    
    Token(TokenType t, const std::string& var) 
        : type(t), numeric_value(0.0f), variable_name(var) {}
};

// ============================================================================
// PARSER CONTEXT
// ============================================================================
class ParserContext {
public:
    enum VariableDomain {
        DOMAIN_SCALAR,
        DOMAIN_SPATIAL,
        DOMAIN_TIME,
        DOMAIN_ROTATIONAL,
        DOMAIN_COLOR,
        DOMAIN_COMPLEX
    };
    
    struct VariableDef {
        std::string name;
        VariableDomain domain;
        bool differentiable;
        
        VariableDef(const std::string& n = "", 
                   VariableDomain d = DOMAIN_SCALAR, 
                   bool diff = true)
            : name(n), domain(d), differentiable(diff) {}
    };
    
    ParserContext();
    
    void registerVariable(const std::string& name, 
                         VariableDomain domain = DOMAIN_SCALAR, 
                         bool differentiable = true);
    
    void registerObjectType(const std::string& type, 
                           const std::vector<std::string>& properties);
    
    bool isValidVariable(const std::string& name) const;
    bool isValidDerivativeWRT(const std::string& varName) const;
    VariableDomain getVariableDomain(const std::string& varName) const;
    
private:
    std::unordered_map<std::string, VariableDef> m_variables;
    std::unordered_map<std::string, std::vector<std::string>> m_objectTypes;
};

// ============================================================================
// PARSED EQUATION RESULT (EXTENDED FOR ROTATION AND COLOR)
// ============================================================================
struct ParsedEquation {
    std::vector<Token> tokens_ax;        // Acceleration X in RPN
    std::vector<Token> tokens_ay;        // Acceleration Y in RPN
    std::vector<Token> tokens_angular;   // NEW: Angular acceleration in RPN
    std::vector<Token> tokens_r;         // NEW: Red color component in RPN
    std::vector<Token> tokens_g;         // NEW: Green color component in RPN
    std::vector<Token> tokens_b;         // NEW: Blue color component in RPN
    std::vector<Token> tokens_a;         // NEW: Alpha color component in RPN
    std::vector<float> constants;        // Numeric constants from all components
    
    // Helper methods to check what's present
    bool hasAngular() const { return !tokens_angular.empty(); }
    bool hasColor() const { 
        return !tokens_r.empty() || !tokens_g.empty() || 
               !tokens_b.empty() || !tokens_a.empty(); 
    }
    
    ParsedEquation() = default;
};

// ============================================================================
// MAIN PARSING FUNCTIONS
// ============================================================================

// Parse derivative call: D(expr, var, order)
std::vector<Token> parseDerivativeCall(
    const std::string& expression, 
    size_t start_pos, 
    size_t& end_pos, 
    const ParserContext& context
);

// Tokenize expression string
std::vector<Token> tokenizeExpression(
    const std::string& expression, 
    const ParserContext& context
);

// Convert infix to RPN (Shunting Yard)
std::vector<Token> infixToRPN(
    const std::vector<Token>& infixTokens
);

// Main entry point - now supports extended format:
// "ax, ay, angular_accel, r, g, b, a"
ParsedEquation ParseEquation(
    const std::string& equation_string, 
    const ParserContext& context
);