#pragma once

#include <string>
#include <vector>
#include <unordered_map>

// ============================================================================
// TOKEN TYPES
// ============================================================================
enum TokenType
{
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
    TOKEN_DERIVATIVE,

    // Tensors (any rank)
    TOKEN_TENSOR_LIT,

    // Functions / operators
    TOKEN_DOT,
    TOKEN_CROSS,
    TOKEN_NORM,
    TOKEN_SELECT,
    TOKEN_NOISE,
    TOKEN_RAND,
    TOKEN_SUM_NEIGHBORS,
    TOKEN_NEIGHBOR_INDEX,
    TOKEN_COMP, //extract components

    // DSL statements
    TOKEN_LET,
    TOKEN_EQUAL,
    TOKEN_SEMICOLON,
    TOKEN_TARGET_AX,
    TOKEN_TARGET_AY,
    TOKEN_TARGET_ANGULAR,
    TOKEN_TARGET_COLOR_R,
    TOKEN_TARGET_COLOR_G,
    TOKEN_TARGET_COLOR_B,
    TOKEN_TARGET_COLOR_A,
    TOKEN_TARGET_SIZE,
    TOKEN_TARGET_DATA_X,
    TOKEN_TARGET_DATA_Y,

    TOKEN_SAMPLE_PREV_R,
    TOKEN_SAMPLE_PREV_G,
    TOKEN_SAMPLE_PREV_B,
    TOKEN_SAMPLE_PREV_A,
    TOKEN_AVG_PREV_R,
    TOKEN_AVG_PREV_G,
    TOKEN_AVG_PREV_B,
    TOKEN_AVG_PREV_A,

    // Comparison operators
    TOKEN_LT, // <
    TOKEN_LE, // <=
    TOKEN_GT, // >
    TOKEN_GE, // >=
    TOKEN_EQ, // ==
    TOKEN_NE  // !=
};
// ============================================================================
// DERIVATIVE METHODS
// ============================================================================
enum DerivativeMethod
{
    DERIV_METHOD_NUMERICAL = 0,
    DERIV_METHOD_SYMBOLIC = 1
};

// ============================================================================
// TOKEN STRUCTURE
// ============================================================================
struct Token
{
    TokenType type;

    // For TOKEN_NUMBER
    float numeric_value = 0.0f;

    // For TOKEN_VARIABLE
    std::string variable_name;

    // For TOKEN_OBJECT_REF
    std::string object_type;
    int object_index = -1;
    std::string object_property;

    // For TOKEN_DERIVATIVE
    std::string derivative_wrt;
    int derivative_order = 1;
    DerivativeMethod derivative_method = DERIV_METHOD_NUMERICAL;
    std::vector<Token> derivative_expr_tokens;

    // For TOKEN_TENSOR_LIT (any rank)
    std::vector<int> tensor_shape;                      // e.g., {2,3}
    std::vector<std::vector<Token>> tensor_components; // flattened, row‑major

    // For TOKEN_SUM_NEIGHBORS
    std::vector<Token> sum_weight;
    std::vector<Token> sum_body;

    // For TOKEN_SELECT
    std::vector<Token> select_cond;
    std::vector<Token> select_a;
    std::vector<Token> select_b;

    // For TOKEN_COMP
    std::vector<int> comp_indices;                     // e.g., {1} for a vector, {2,0} for a matrix
    std::vector<Token> comp_tensor_tokens;             // the tensor expression to extract from

    // Constructors
    Token() : type(TOKEN_NUMBER), numeric_value(0.0f) {}
    explicit Token(TokenType t) : type(t), numeric_value(0.0f) {}
    Token(TokenType t, float v) : type(t), numeric_value(v) {}
    Token(TokenType t, const std::string &var) : type(t), variable_name(var) {}
};
// ============================================================================
// PARSER CONTEXT
// ============================================================================
class ParserContext
{
public:
    enum VariableDomain
    {
        DOMAIN_SCALAR,
        DOMAIN_SPATIAL,
        DOMAIN_TIME,
        DOMAIN_ROTATIONAL,
        DOMAIN_COLOR,
        DOMAIN_COMPLEX
    };

    struct VariableDef
    {
        std::string name;
        VariableDomain domain;
        bool differentiable;

        VariableDef(const std::string &n = "",
                    VariableDomain d = DOMAIN_SCALAR,
                    bool diff = true)
            : name(n), domain(d), differentiable(diff) {}
    };

    ParserContext();

    void registerVariable(const std::string &name,
                          VariableDomain domain = DOMAIN_SCALAR,
                          bool differentiable = true);

    void registerObjectType(const std::string &type,
                            const std::vector<std::string> &properties);

    bool isValidVariable(const std::string &name) const;
    bool isValidDerivativeWRT(const std::string &varName) const;
    VariableDomain getVariableDomain(const std::string &varName) const;

private:
    std::unordered_map<std::string, VariableDef> m_variables;
    std::unordered_map<std::string, std::vector<std::string>> m_objectTypes;
};

// ============================================================================
// STATEMENT STRUCTURE (for new DSL)
// ============================================================================
struct Statement
{
    enum Type
    {
        LET,
        ASSIGN,
        EXPR
    } type;
    std::string lhs;        // variable name for LET, target name for ASSIGN
    int targetCode = 0;     // one of TOKEN_TARGET_* for ASSIGN
    std::vector<Token> rpn; // RPN of the right‑hand side
    // For LET: the inferred type of the variable (0=scalar, 1=vector, 2=matrix, etc.)
    int tensorRank = -1;       // -1 means not a tensor, 0 scalar, 1 vector, 2 matrix
    std::vector<int> tensorShape; // e.g., {2} for vector, {2,2} for matrix
};

// ============================================================================
// PARSED EQUATION RESULT
// ============================================================================
struct ParsedEquation
{
    std::vector<Token> tokens_ax;      // Acceleration X in RPN
    std::vector<Token> tokens_ay;      // Acceleration Y in RPN
    std::vector<Token> tokens_angular; // Angular acceleration in RPN
    std::vector<Token> tokens_r;       // Red color component in RPN
    std::vector<Token> tokens_g;       // Green color component in RPN
    std::vector<Token> tokens_b;       // Blue color component in RPN
    std::vector<Token> tokens_a;       // Alpha color component in RPN
    std::vector<float> constants;      // Numeric constants from all components

    std::vector<Statement> statements;

    bool hasAngular() const { return !tokens_angular.empty(); }
    bool hasColor() const
    {
        return !tokens_r.empty() || !tokens_g.empty() ||
               !tokens_b.empty() || !tokens_a.empty();
    }

    ParsedEquation() = default;
};

// ============================================================================
// MAIN PARSING FUNCTIONS
// ============================================================================

std::vector<Token> parseDerivativeCall(
    const std::string &expression,
    size_t start_pos,
    size_t &end_pos,
    const ParserContext &context);

std::vector<Token> tokenizeExpression(
    const std::string &expression,
    const ParserContext &context);

std::vector<Token> infixToRPN(
    const std::vector<Token> &infixTokens);

ParsedEquation ParseEquation(
    const std::string &equation_string,
    const ParserContext &context);