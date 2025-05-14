#ifndef __ELOG_ASYNC_TARGET_PROVIDER_H__
#define __ELOG_ASYNC_TARGET_PROVIDER_H__

#include "elog_async_target.h"
#include "elog_common.h"

namespace elog {

/** @brief Parent interface for all asynchronous log target providers. */
class ELogAsyncTargetProvider {
public:
    ELogAsyncTargetProvider(const ELogAsyncTargetProvider&) = delete;
    ELogAsyncTargetProvider(ELogAsyncTargetProvider&&) = delete;
    virtual ~ELogAsyncTargetProvider() {}

    /**
     * @brief Loads a target from configuration (URL style).
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @return ELogAsyncTarget* The resulting DB log target, or null of failed.
     */
    virtual ELogAsyncTarget* loadTarget(const std::string& logTargetCfg,
                                        const ELogTargetSpec& targetSpec) = 0;

    /**
     * @brief Loads a target from configuration (nested style).
     * @param logTargetCfg The configuration string.
     * @param targetNestedSpec The parsed configuration string.
     * @return ELogAsyncTarget* The resulting DB log target, or null of failed.
     */
    virtual ELogAsyncTarget* loadTarget(const std::string& logTargetCfg,
                                        const ELogTargetNestedSpec& targetSpec) = 0;

protected:
    ELogAsyncTargetProvider() {}

    ELogTarget* loadNestedTarget(const std::string& logTargetCfg,
                                 const ELogTargetNestedSpec& targetSpec);

    ELogTarget* loadSingleSubTarget(const std::string& logTargetCfg,
                                    const ELogTargetNestedSpec& targetSpec);
};

}  // namespace elog

#endif  // __ELOG_ASYNC_TARGET_PROVIDER_H__