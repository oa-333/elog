#include "elog_type_codec.h"

#include "elog_report.h"

#ifdef ELOG_ENABLE_FMT_LIB

/** @def The maximum number of flush policies types that can be defined in the system. */
#define ELOG_MAX_TYPE_COUNT UINT8_MAX

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogTypeCodec)

static ELogTypeDecoder* sTypeDecoders[ELOG_MAX_TYPE_COUNT] = {};

bool registerTypeDecoder(uint32_t typeCode, ELogTypeDecoder* decoder) {
    if (sTypeDecoders[typeCode] != nullptr) {
        ELOG_REPORT_ERROR("Cannot register type decoder by type code %u, duplicate registration",
                          (unsigned)typeCode);
        return false;
    }
    sTypeDecoders[typeCode] = decoder;
    ELOG_REPORT_TRACE("Registered type decoder for type code %u", (unsigned)typeCode);
    return true;
}

bool decodeType(uint8_t typeCode, ELogReadBuffer& readBuffer,
                fmt::dynamic_format_arg_store<fmt::format_context>& store) {
    ELogTypeDecoder* decoder = sTypeDecoders[typeCode];
    if (decoder == nullptr) {
        ELOG_REPORT_ERROR("Cannot decode type by code %u, not registered", (unsigned)typeCode);
        return false;
    }
    return decoder->decodeType(readBuffer, store);
}

ELOG_DECLARE_TYPE_DECODE(uint8_t, ELOG_UINT8_CODE)
ELOG_DECLARE_TYPE_DECODE(uint16_t, ELOG_UINT16_CODE)
ELOG_DECLARE_TYPE_DECODE(uint32_t, ELOG_UINT32_CODE)
ELOG_DECLARE_TYPE_DECODE(uint64_t, ELOG_UINT64_CODE)
ELOG_DECLARE_TYPE_DECODE(int8_t, ELOG_INT8_CODE)
ELOG_DECLARE_TYPE_DECODE(int16_t, ELOG_INT16_CODE)
ELOG_DECLARE_TYPE_DECODE(int32_t, ELOG_INT32_CODE)
ELOG_DECLARE_TYPE_DECODE(int64_t, ELOG_INT64_CODE)
ELOG_DECLARE_TYPE_DECODE(float, ELOG_FLOAT_CODE)
ELOG_DECLARE_TYPE_DECODE(double, ELOG_DOUBLE_CODE)
ELOG_DECLARE_TYPE_DECODE(bool, ELOG_BOOL_CODE)

// need typedef due to name concatenation in macro
typedef char* ELogStrType;
typedef char* const ELogConstStrType;
ELOG_DECLARE_TYPE_DECODE(ELogStrType, ELOG_STRING_CODE)
ELOG_DECLARE_TYPE_DECODE(ELogConstStrType, ELOG_CONST_STRING_CODE)

#define ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(Type)                      \
    ELOG_IMPLEMENT_TYPE_DECODE(Type) {                                  \
        Type argValue = (Type)0;                                        \
        if (!readBuffer.read(argValue)) {                               \
            ELOG_REPORT_ERROR("Failed to read parameter of type " #Type \
                              " in binary log buffer, end of stream");  \
            return false;                                               \
        }                                                               \
        store.push_back(argValue);                                      \
        return true;                                                    \
    }

ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(uint8_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(uint16_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(uint32_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(uint64_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(int8_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(int16_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(int32_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(int64_t)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(float)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(double)
ELOG_IMPLEMENT_PRIMITIVE_TYPE_DECODE(bool)

// special needed for string types
#define ELOG_IMPLEMENT_STRING_TYPE_DECODE(StrType)                                     \
    ELOG_IMPLEMENT_TYPE_DECODE(StrType) {                                              \
        const char* arg = readBuffer.getPtr();                                         \
        store.push_back(arg);                                                          \
        if (!readBuffer.advanceOffset(strlen(arg) + 1)) {                              \
            ELOG_REPORT_ERROR(                                                         \
                "Failed to skip string argument in binary log buffer, end of stream"); \
            return false;                                                              \
        }                                                                              \
        return true;                                                                   \
    }

ELOG_IMPLEMENT_STRING_TYPE_DECODE(ELogStrType)
ELOG_IMPLEMENT_STRING_TYPE_DECODE(ELogConstStrType)

}  // namespace elog

#endif  // ELOG_ENABLE_FMT_LIB