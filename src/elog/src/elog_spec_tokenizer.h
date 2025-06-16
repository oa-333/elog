#ifndef __ELOG_SPEC_TOKENIZER_H__
#define __ELOG_SPEC_TOKENIZER_H__

#include <cstdint>
#include <string>

namespace elog {

/** @brief Token type constants */
enum class ELogTokenType : uint32_t {
    /** @brief Invalid token type. */
    TT_INVALID,

    /** @brief An open brace token. */
    TT_OPEN_BRACE,

    /** @brief An close brace token. */
    TT_CLOSE_BRACE,

    /** @brief An open square bracket (array subscript) token. */
    TT_OPEN_BRACKET,

    /** @brief An close square bracket (array subscript) token. */
    TT_CLOSE_BRACKET,

    /** @brief A comma (property separator). */
    TT_COMMA,

    /** @brief An equal sign token. */
    TT_EQUAL_SIGN,

    /** @brief A colon sign token. */
    TT_COLON_SIGN,

    /** @brief A text token. */
    TT_TOKEN
};

// TODO: rename to ELogStringTokenizer

class ELogSpecTokenizer {
public:
    ELogSpecTokenizer(const std::string& spec);
    ELogSpecTokenizer(const ELogSpecTokenizer&) = delete;
    ELogSpecTokenizer(ELogSpecTokenizer&&) = delete;
    ~ELogSpecTokenizer() {}

    inline bool hasMoreTokens() const { return m_pos < m_spec.length(); }

    inline uint32_t getPos() const { return m_pos; }

    inline void rewind(uint32_t pos) { m_pos = pos; }

    inline const char* getSpec() const { return m_spec.c_str(); }

    std::string getErrLocStr(uint32_t tokenPos) const;

    bool nextToken(ELogTokenType& tokenType, std::string& token, uint32_t& tokenPos);

    ELogTokenType peekNextTokenType();

    bool parseExpectedToken(ELogTokenType expectedTokenType, std::string& token,
                            const char* expectedStr);

    bool parseExpectedToken2(ELogTokenType expectedTokenType1, ELogTokenType expectedTokenType2,
                             ELogTokenType& tokenType, std::string& token, uint32_t& tokenPos,
                             const char* expectedStr1, const char* expectedStr2);

    bool parseExpectedToken3(ELogTokenType expectedTokenType1, ELogTokenType expectedTokenType2,
                             ELogTokenType expectedTokenType3, ELogTokenType& tokenType,
                             std::string& token, uint32_t& tokenPos, const char* expectedStr1,
                             const char* expectedStr2, const char* expectedStr3);

private:
    std::string m_spec;
    uint32_t m_pos;
};

}  // namespace elog

#endif  // __ELOG_SPEC_TOKENIZER_H__