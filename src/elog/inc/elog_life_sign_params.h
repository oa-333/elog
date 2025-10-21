#ifndef __ELOG_LIFE_SIGN_PARAMS_H__
#define __ELOG_LIFE_SIGN_PARAMS_H__

#ifdef ELOG_ENABLE_LIFE_SIGN

#include <cstdint>

#include "elog_common_def.h"
#include "elog_def.h"

namespace elog {

/** @struct Life sign configuration parameters. */
struct ELOG_API ELogLifeSignParams {
    /**
     * @brief Specifies whether life sign reports are to be used.
     * This member is valid only when building ELog with ELOG_ENABLE_LIFE_SIGN.
     * By default, if ELOG_ENABLE_LIFE_SIGN is enabled, then life-sign reports are enabled.
     * This flag exists so that users of ELog library that was compiled with
     * ELOG_ENABLE_LIFE_SIGN, would still have the ability to disable life-sign reports.
     */
    bool m_enableLifeSignReport;

    /**
     * @brief The period in milliseconds of each life-sign background garbage collection task,
     * which wakes up and recycles all objects ready for recycling.
     */
    uint32_t m_lifeSignGCPeriodMillis;

    /**
     * @brief The number of life-sign background garbage collection tasks.
     */
    uint32_t m_lifeSignGCTaskCount;

    ELogLifeSignParams()
        : m_enableLifeSignReport(ELOG_DEFAULT_ENABLE_LIFE_SIGN),
          m_lifeSignGCPeriodMillis(ELOG_DEFAULT_LIFE_SIGN_GC_PERIOD_MILLIS),
          m_lifeSignGCTaskCount(ELOG_DEFAULT_LIFE_SIGN_GC_TASK_COUNT) {}
    ELogLifeSignParams(const ELogLifeSignParams&) = default;
    ELogLifeSignParams(ELogLifeSignParams&&) = default;
    ELogLifeSignParams& operator=(const ELogLifeSignParams&) = default;
    ~ELogLifeSignParams() {}
};

}  // namespace elog

#endif  // ELOG_ENABLE_LIFE_SIGN

#endif  // __ELOG_LIFE_SIGN_PARAMS_H__