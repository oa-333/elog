#ifndef __ELOG_DB_TARGET_PROVIDER_H__
#define __ELOG_DB_TARGET_PROVIDER_H__

#include "elog_common.h"
#include "elog_db_target.h"

namespace elog {

/** @brief Parent interface for all DB log targets. */
class ELogDbTargetProvider {
public:
    ELogDbTargetProvider(const ELogDbTargetProvider&) = delete;
    ELogDbTargetProvider(ELogDbTargetProvider&&) = delete;
    virtual ~ELogDbTargetProvider() {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @param connString The extracted connection string.
     * @param insertQuery The extracted insert query.
     * @return ELogDbTarget* The resulting DB log target, or null of failed.
     */
    virtual ELogDbTarget* loadTarget(const std::string& logTargetCfg,
                                     const ELogTargetSpec& targetSpec,
                                     const std::string& connString,
                                     const std::string& insertQuery) = 0;

protected:
    ELogDbTargetProvider() {}
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_PROVIDER_H__