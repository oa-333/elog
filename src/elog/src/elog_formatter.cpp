#include "elog_formatter.h"

#include <bit>

#include "elog_string_receptor.h"

namespace elog {

#define ELOG_SPECIAL_FIELDS_SIZE 128

void ELogFormatter::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    // we try to reserve space for the formatted message to avoid repeated memory allocations during
    // message formatting
    // it is advised to pass into this function a reusable string buffer for logMsg
    uint32_t requiredLen = logRecord.m_logMsgLen;
    requiredLen += ELOG_SPECIAL_FIELDS_SIZE;  // approximation for special fields
    // round up to next power of 2
#if __cpp_lib_bitops >= 201907L
    uint32_t usedBits = 32 - std::countl_zero(requiredLen);
    requiredLen = 1 << (usedBits + 1);
#else
    uint32_t pow2 = 1;
    while (pow2 < requiredLen) {
        pow2 = pow2 << 1;
    }
    requiredLen = pow2;
#endif
    logMsg.reserve(requiredLen);

    // unlike the string stream receptor, the string receptor formats directly the resulting log
    // message string, and so we save one or two string copies
    ELogStringReceptor receptor(logMsg);
    applyFieldSelectors(logRecord, &receptor);
}

}  // namespace elog