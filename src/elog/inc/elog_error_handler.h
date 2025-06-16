#ifndef __ELOG_ERROR_HANDLER_H__
#define __ELOG_ERROR_HANDLER_H__

#include "elog_def.h"

namespace elog {

/** @brief Error handling interface. */
class ELOG_API ELogErrorHandler {
public:
    ELogErrorHandler(const ELogErrorHandler&) = delete;
    ELogErrorHandler(ELogErrorHandler&&) = delete;
    virtual ~ELogErrorHandler() {}

    virtual void onError(const char* msg) = 0;

    virtual void onWarn(const char* msg) = 0;

    virtual void onTrace(const char* msg) = 0;

    /** @brief Configures elog tracing. */
    virtual void setTraceMode(bool enableTrace) { m_isTraceEnabled = enableTrace; }

    /** @brief Queries whether trace mode is enabled. */
    inline bool isTraceEnabled() { return m_isTraceEnabled; }

protected:
    ELogErrorHandler(bool enableTrace = false) : m_isTraceEnabled(enableTrace) {}

private:
    bool m_isTraceEnabled;
};

}  // namespace elog

#endif  // __ELOG_ERROR_HANDLER_H__