#ifndef __ELOG_EXPRESSION_TOKENIZER_H__
#define __ELOG_EXPRESSION_TOKENIZER_H__

#include <cstdint>

#include "elog_expression.h"

namespace elog {

/** @brief Token type constants */
enum class ELogExprTokenType : uint32_t {
    /** @brief Invalid token type. */
    TT_INVALID,

    /** @brief An open parenthesis token. */
    TT_OPEN_PAREN,

    /** @brief A close parenthesis token. */
    TT_CLOSE_PAREN,

    /** @brief AND token. */
    TT_AND,

    /** @brief OR token. */
    TT_OR,

    /** @brief NOT token. */
    TT_NOT,

    /** @brief Equals operator token. */
    TT_EQ_OP,

    /** @brief Not-equals operator token. */
    TT_NEQ_OP,

    /** @brief Less-than operator token. */
    TT_LT_OP,

    /** @brief Less-than or equals-to operator token. */
    TT_LE_OP,

    /** @brief Greater-than operator token. */
    TT_GT_OP,

    /** @brief Greater-than or equals-to operator token. */
    TT_GE_OP,

    /** @brief LIKE operator token. */
    TT_LIKE_OP,

    /** @brief CONTAINS operator token. */
    TT_CONTAINS_OP,

    /** @brief A text token. */
    TT_TOKEN
};

class ELogExpressionTokenizer {
public:
    ELogExpressionTokenizer(const std::string& sourceStr);
    ELogExpressionTokenizer(const ELogExpressionTokenizer&) = delete;
    ELogExpressionTokenizer(ELogExpressionTokenizer&&) = delete;
    ~ELogExpressionTokenizer() {}

    inline bool hasMoreTokens() const { return m_pos < m_sourceStr.length(); }

    inline uint32_t getPos() const { return m_pos; }

    inline void rewind(uint32_t pos) { m_pos = pos; }

    inline const char* getSourceStr() const { return m_sourceStr.c_str(); }

    std::string getErrLocStr(uint32_t tokenPos) const;

    bool nextToken(ELogExprTokenType& tokenType, std::string& token, uint32_t& tokenPos);

    ELogExprTokenType peekNextTokenType();

    bool isOpToken(ELogExprTokenType tokenType);

    bool parseExpectedToken(ELogExprTokenType expectedTokenType, std::string& token,
                            const char* expectedStr);

    bool parseExpectedToken2(ELogExprTokenType expectedTokenType1,
                             ELogExprTokenType expectedTokenType2, ELogExprTokenType& tokenType,
                             std::string& token, uint32_t& tokenPos, const char* expectedStr1,
                             const char* expectedStr2);

    bool parseExpectedToken3(ELogExprTokenType expectedTokenType1,
                             ELogExprTokenType expectedTokenType2,
                             ELogExprTokenType expectedTokenType3, ELogExprTokenType& tokenType,
                             std::string& token, uint32_t& tokenPos, const char* expectedStr1,
                             const char* expectedStr2, const char* expectedStr3);

private:
    std::string m_sourceStr;
    uint32_t m_pos;
};

}  // namespace elog

#endif  // __ELOG_EXPRESSION_TOKENIZER_H__