#ifndef __ELOG_FIELD_RECEPTOR_H__
#define __ELOG_FIELD_RECEPTOR_H__

#include <cstdint>
#include <string>

#include "elog_def.h"
#ifndef ELOG_MSVC
#include <sys/time.h>
#endif

#include "elog_level.h"

namespace elog {

/** @brief Parent interface for the target receptor of selected log record fields. */
class ELOG_API ELogFieldReceptor {
public:
    virtual ~ELogFieldReceptor() {}

    /** @brief Receives a string log record field. */
    virtual void receiveStringField(uint32_t typeId, const std::string& value, int justify) = 0;

    /** @brief Receives an integer log record field. */
    virtual void receiveIntField(uint32_t typeId, uint64_t value, int justify) = 0;

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(uint32_t typeId, const SYSTEMTIME& sysTime, const char* timeStr,
                                  int justify) = 0;
#else
    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(uint32_t typeId, const timeval& sysTime, const char* timeStr,
                                  int justify) = 0;
#endif

    /** @brief Receives a log level log record field. */
    virtual void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel, int justify) = 0;

protected:
    ELogFieldReceptor() {}
};

}  // namespace elog

#endif  // __ELOG_FIELD_RECEPTOR_H__