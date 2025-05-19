#ifndef __ELOG_FIELD_RECEPTOR_H__
#define __ELOG_FIELD_RECEPTOR_H__

#include <cstdint>
#include <sstream>
#include <string>

#include "elog_level.h"

namespace elog {

/** @brief Parent interface for the target receptor of selected log record fields. */
class ELOG_API ELogFieldReceptor {
public:
    virtual ~ELogFieldReceptor() {}

    /** @brief Receives a string log record field. */
    virtual void receiveStringField(const std::string& field, int justify) = 0;

    /** @brief Receives an integer log record field. */
    virtual void receiveIntField(uint64_t field, int justify) = 0;

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr, int justify) = 0;
#else
    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(const timeval& sysTime, const char* timeStr, int justify) = 0;
#endif

    /** @brief Receives a log level log record field. */
    virtual void receiveLogLevelField(ELogLevel logLevel, int justify) = 0;

protected:
    ELogFieldReceptor() {}
};

}  // namespace elog

#endif  // __ELOG_FIELD_RECEPTOR_H__