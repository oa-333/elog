#ifndef __ELOG_TYPE_CODEC_H__
#define __ELOG_TYPE_CODEC_H__

#ifdef ELOG_ENABLE_FMT_LIB

#include "elog_buffer.h"
#include "elog_def.h"
#include "elog_fmt_lib.h"
#include "elog_read_buffer.h"

namespace elog {

/** @brief Generic template function for getting unique type code. */
template <typename T>
inline uint8_t getTypeCode() {
    static_assert(false, "Missing type code specialization, required for binary logging");
    return 0;
}

/** @def Macro for declaring type codes (for internal use only). */
#define ELOG_DECLARE_TYPE_CODE(Type, TypeCode) \
    template <>                                \
    inline uint8_t getTypeCode<Type>() {       \
        return TypeCode;                       \
    }

/** @def Macro for declaring type codes. */
#define ELOG_DECLARE_TYPE_CODE_EX(Type, TypeCode) \
    namespace elog {                              \
    ELOG_DECLARE_TYPE_CODE(Type, TypeCode)        \
    }

/**
 * @brief Encodes a type into a buffer. Implementation should not encode type code, but only
 * required information to be able to decode the value from the buffer.
 *
 * @param buffer The output buffer.
 * @return True if encoding succeeded, otherwise false.
 */
template <typename T>
inline bool encodeType(const T& value, ELogBuffer& buffer) {
    static_assert(false, "Missing type encoding specialization, required for binary logging");
    return 0;
}

/** @def Macro for declaring type codes (for internal use only). */
#define ELOG_DECLARE_TYPE_ENCODE(Type, TypeCode) \
    ELOG_DECLARE_TYPE_CODE(Type, TypeCode)       \
    template <>                                  \
    inline bool encodeType<Type>(const Type& value, ELogBuffer& buffer);

/** @def Macro for declaring type codes. */
#define ELOG_DECLARE_TYPE_ENCODE_EX(Type, TypeCode) \
    namespace elog {                                \
    ELOG_DECLARE_TYPE_ENCODE(Type, TypeCode)        \
    }

/**
 * @brief Decodes a type from a read buffer, and store an fmtlib formattable type in the store.
 *
 * @param typeCode The type code (must be properly registered).
 * @param readBuffer The input read buffer.
 * @param store The output argument store.
 * @return True if type code is registered and deserialization succeeded, otherwise false.
 */
extern ELOG_API bool decodeType(uint8_t typeCode, ELogReadBuffer& readBuffer,
                                fmt::dynamic_format_arg_store<fmt::format_context>& store);

/** @brief parent class for type codecs. */
class ELOG_API ELogTypeDecoder {
public:
    virtual ~ELogTypeDecoder() {}

    /**
     * @brief Decodes a type from a read buffer, and store  fmtlib formattable type in the argument
     * store.
     * @param readBuffer The input read buffer. This is the serialized binary logging buffer.
     * @param store The output store. This is used for fmtlib formatting the user's log message.
     * @return Operation result.
     */
    virtual bool decodeType(ELogReadBuffer& readBuffer,
                            fmt::dynamic_format_arg_store<fmt::format_context>& store) = 0;

protected:
    ELogTypeDecoder() {}
    ELogTypeDecoder(const ELogTypeDecoder&) = delete;
    ELogTypeDecoder(ELogTypeDecoder&&) = delete;
    ELogTypeDecoder& operator=(const ELogTypeDecoder&) = delete;
};

/**
 * @brief Registers a type codec for binary logging.
 *
 * @param typeCode The type code. Must be unique.
 * @param codec The type codec.
 * @return ELOG_API
 */
extern ELOG_API bool registerTypeDecoder(uint32_t typeCode, ELogTypeDecoder* codec);

/** @brief Declares a type codec for a given type (for internal use only). */
#define ELOG_DECLARE_TYPE_DECODE(Type, TypeCode)                                                 \
    class TypeDecoder##Type : public elog::ELogTypeDecoder {                                     \
    public:                                                                                      \
        TypeDecoder##Type() : elog::ELogTypeDecoder() {}                                         \
        TypeDecoder##Type(const TypeDecoder##Type&) = delete;                                    \
        TypeDecoder##Type(TypeDecoder##Type&&) = delete;                                         \
        TypeDecoder##Type& operator=(const TypeDecoder##Type&) = delete;                         \
        ~TypeDecoder##Type() final {}                                                            \
        bool decodeType(elog::ELogReadBuffer& readBuffer,                                        \
                        fmt::dynamic_format_arg_store<fmt::format_context>& store) final;        \
    };                                                                                           \
    class TypeDecoder##Type##RegistryHelper {                                                    \
    private:                                                                                     \
        TypeDecoder##Type##RegistryHelper() {                                                    \
            elog::registerTypeDecoder(TypeCode, &m_typeDecoder);                                 \
        }                                                                                        \
        ~TypeDecoder##Type##RegistryHelper() {}                                                  \
        TypeDecoder##Type##RegistryHelper(const TypeDecoder##Type##RegistryHelper&) = delete;    \
        TypeDecoder##Type##RegistryHelper(TypeDecoder##Type##RegistryHelper&&) = delete;         \
        TypeDecoder##Type##RegistryHelper& operator=(const TypeDecoder##Type##RegistryHelper&) = \
            delete;                                                                              \
        TypeDecoder##Type m_typeDecoder;                                                         \
        static TypeDecoder##Type##RegistryHelper sRegistryHelper;                                \
    };

/** @brief Declares a type codec for a given type. */
#define ELOG_DECLARE_TYPE_DECODE_EX(Type, TypeCode) ELOG_DECLARE_TYPE_DECODE(Type, TypeCode)

/** @brief Declares a type codec for a given type. */
#define ELOG_DECLARE_TYPE_ENCODE_DECODE_EX(Type, TypeCode) \
    ELOG_DECLARE_TYPE_ENCODE_EX(Type, TypeCode)            \
    ELOG_DECLARE_TYPE_DECODE_EX(Type, TypeCode)

// helper macro for implementing a type encoding (for internal use only)
// must be followed by implementation of encodeType, placed in curly braces
// this must be placed in a place visible by any binary logging code that uses the type

/** @brief helper macro for implementing a type encoding (for internal use only). */
#define ELOG_IMPLEMENT_TYPE_ENCODE(Type) \
    template <>                          \
    inline bool encodeType<Type>(const Type& value, ELogBuffer& buffer)

/** @brief helper macro for implementing a type encoding. */
#define ELOG_BEGIN_IMPLEMENT_TYPE_ENCODE_EX(Type) \
    namespace elog {                              \
    ELOG_IMPLEMENT_TYPE_ENCODE(Type)

/** @brief helper macro for implementing a type encoding. */
#define ELOG_END_IMPLEMENT_TYPE_ENCODE_EX() }

// helper macro for implementing a type decoding
// must be followed by implementation of decodeType, placed in curly braces
// this can be placed in source file

/** @brief helper macro for implementing a type decoding (for internal use only). */
#define ELOG_IMPLEMENT_TYPE_DECODE(Type)                                                  \
    TypeDecoder##Type##RegistryHelper TypeDecoder##Type##RegistryHelper::sRegistryHelper; \
    bool TypeDecoder##Type::decodeType(elog::ELogReadBuffer& readBuffer,                  \
                                       fmt::dynamic_format_arg_store<fmt::format_context>& store)

/** @brief helper macro for implementing a type decoding. */
#define ELOG_IMPLEMENT_TYPE_DECODE_EX(Type) ELOG_IMPLEMENT_TYPE_DECODE(Type)

/** @def Special codes for primitive type. */
#define ELOG_UINT8_CODE ((uint8_t)0x01)
#define ELOG_UINT16_CODE ((uint8_t)0x02)
#define ELOG_UINT32_CODE ((uint8_t)0x03)
#define ELOG_UINT64_CODE ((uint8_t)0x04)
#define ELOG_INT8_CODE ((uint8_t)0x05)
#define ELOG_INT16_CODE ((uint8_t)0x06)
#define ELOG_INT32_CODE ((uint8_t)0x07)
#define ELOG_INT64_CODE ((uint8_t)0x08)
#define ELOG_FLOAT_CODE ((uint8_t)0x09)
#define ELOG_DOUBLE_CODE ((uint8_t)0x0A)
#define ELOG_BOOL_CODE ((uint8_t)0x0B)
#define ELOG_STRING_CODE ((uint8_t)0x0C)
#define ELOG_CONST_STRING_CODE ((uint8_t)0x0D)

// UDTs codes start at 0x10
#define ELOG_UDT_CODE_BASE ((uint8_t)0x10)

// declare primitive type encoding
ELOG_DECLARE_TYPE_ENCODE(uint8_t, ELOG_UINT8_CODE)
ELOG_DECLARE_TYPE_ENCODE(uint16_t, ELOG_UINT16_CODE)
ELOG_DECLARE_TYPE_ENCODE(uint32_t, ELOG_UINT32_CODE)
ELOG_DECLARE_TYPE_ENCODE(uint64_t, ELOG_UINT64_CODE)
ELOG_DECLARE_TYPE_ENCODE(int8_t, ELOG_INT8_CODE)
ELOG_DECLARE_TYPE_ENCODE(int16_t, ELOG_INT16_CODE)
ELOG_DECLARE_TYPE_ENCODE(int32_t, ELOG_INT32_CODE)
ELOG_DECLARE_TYPE_ENCODE(int64_t, ELOG_INT64_CODE)
ELOG_DECLARE_TYPE_ENCODE(float, ELOG_FLOAT_CODE)
ELOG_DECLARE_TYPE_ENCODE(double, ELOG_DOUBLE_CODE)
ELOG_DECLARE_TYPE_ENCODE(bool, ELOG_BOOL_CODE)

// encoding implementation for primitive types
#define ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(Type)                        \
    template <>                                                           \
    inline bool encodeType<Type>(const Type& value, ELogBuffer& buffer) { \
        return buffer.appendData(value);                                  \
    }

ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(uint8_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(uint16_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(uint32_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(uint64_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(int8_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(int16_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(int32_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(int64_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(float)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(double)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_ENCODE(bool)

// string type codes
ELOG_DECLARE_TYPE_CODE(char*, ELOG_STRING_CODE)
ELOG_DECLARE_TYPE_CODE(const char*, ELOG_CONST_STRING_CODE)

// string type encoding
template <>
inline bool encodeType<>(char* const& value, ELogBuffer& buffer) {
    // we must append also terminating null (so that decoding can use pointer and avoid copying)
    return buffer.append(value, strlen(value) + 1);
}

// string type encoding
template <>
inline bool encodeType<>(const char* const& value, ELogBuffer& buffer) {
    // we must append also terminating null (so that decoding can use pointer and avoid copying)
    return buffer.append(value, strlen(value) + 1);
}

}  // namespace elog

#endif  // ELOG_ENABLE_FMT_LIB

#endif  // __ELOG_TYPE_CODEC_H__