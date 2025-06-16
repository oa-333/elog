#ifndef __ELOG_DB_TARGET_PROVIDER_H__
#define __ELOG_DB_TARGET_PROVIDER_H__

#include "elog_db_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all DB log targets. */
class ELOG_API ELogDbTargetProvider {
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
     * @param threadModel The threading model to use with db access.
     * @param maxThreads The maximum number of concurrent threads sending log messages to the
     * database at any given point in time.
     * @param reconnectTimeoutMillis Database reconnect timeout in millisecond.
     * @return ELogDbTarget* The resulting DB log target, or null of failed.
     */
    virtual ELogTarget* loadTarget(const std::string& logTargetCfg,
                                   const ELogTargetSpec& targetSpec, const std::string& connString,
                                   const std::string& insertQuery,
                                   ELogDbTarget::ThreadModel threadModel, uint32_t maxThreads,
                                   uint32_t reconnectTimeoutMillis) = 0;

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration object.
     * @param connString The extracted connection string.
     * @param insertQuery The extracted insert query.
     * @param threadModel The threading model to use with db access.
     * @param maxThreads The maximum number of concurrent threads sending log messages to the
     * database at any given point in time.
     * @param reconnectTimeoutMillis Database reconnect timeout in millisecond.
     * @return ELogDbTarget* The resulting DB log target, or null of failed.
     */
    virtual ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg,
                                   const std::string& connString, const std::string& insertQuery,
                                   ELogDbTarget::ThreadModel threadModel, uint32_t maxThreads,
                                   uint32_t reconnectTimeoutMillis) = 0;

protected:
    ELogDbTargetProvider() {}
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_PROVIDER_H__