#ifndef __ELOG_SCHEMA_HANDLER_INTERNAL_H__
#define __ELOG_SCHEMA_HANDLER_INTERNAL_H__

#include "elog_report.h"
#include "elog_schema_handler.h"

namespace elog {

template <typename T>
inline bool initTargetProvider(ELogReportLogger& logger, ELogSchemaHandler* schemaHandler,
                               const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELOG_REPORT_EX(logger, ELEVEL_ERROR,
                       "Failed to create %s/%s target provider, out of memory",
                       schemaHandler->getSchemeName(), name);
        return false;
    }
    if (!schemaHandler->registerTargetProvider(name, provider)) {
        ELOG_REPORT_EX(logger, ELEVEL_ERROR,
                       "Failed to register %s/%s target provider, duplicate name",
                       schemaHandler->getSchemeName(), name);
        delete provider;
        return false;
    }
    return true;
}

template <typename T>
inline bool initNamedTargetProvider(ELogReportLogger& logger, ELogSchemaHandler* schemaHandler,
                                    const char* name) {
    T* provider = new (std::nothrow) T(name);
    if (provider == nullptr) {
        ELOG_REPORT_EX(logger, ELEVEL_ERROR,
                       "Failed to create %s/%s target provider, out of memory",
                       schemaHandler->getSchemeName(), name);
        return false;
    }
    if (!schemaHandler->registerTargetProvider(name, provider)) {
        ELOG_REPORT_EX(logger, ELEVEL_ERROR,
                       "Failed to register %s/%s target provider, duplicate name",
                       schemaHandler->getSchemeName(), name);
        delete provider;
        return false;
    }
    return true;
}

}  // namespace elog

#endif  // __ELOG_SCHEMA_HANDLER_INTERNAL_H__