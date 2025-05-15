#ifndef __ELOG_SPEC_TOKENIZER_H__
#define __ELOG_SPEC_TOKENIZER_H__

#include <cstdint>
#include <string>

namespace elog {

/** @brief Token type constants */
enum class ELogTokenType : uint32_t {
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
    /** @brief A text token. */
    TT_TOKEN
};

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

private:
    std::string m_spec;
    uint32_t m_pos;
};

}  // namespace elog

#endif  // __ELOG_SPEC_TOKENIZER_H__