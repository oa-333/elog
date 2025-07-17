#ifndef __ELOG_ERROR_HANDLER_H__
#define __ELOG_ERROR_HANDLER_H__

#include "elog_def.h"

namespace elog {

/**
 * @brief Error handling interface. User can derive, implement and pass to ELog initialization
 * functions.
 */
class ELOG_API ELogErrorHandler {
public:
    /** @brief Disable copy constructor. */
    ELogErrorHandler(const ELogErrorHandler&) = delete;

    /** @brief Disable move constructor. */
    ELogErrorHandler(ELogErrorHandler&&) = delete;

    /** @brief Disable assignment operator. */
    ELogErrorHandler& operator=(const ELogErrorHandler&) = delete;

    /** @brief Destructor. */
    virtual ~ELogErrorHandler() {}

    /** @brief React to internal ELog error. */
    virtual void onError(const char* msg) = 0;

    /** @brief React to internal ELog warning. */
    virtual void onWarn(const char* msg) = 0;

    /** @brief React to internal ELog trace (only if trace mode is enabled). */
    virtual void onTrace(const char* msg) = 0;

    /** @brief Configures elog tracing. */
    virtual void setTraceMode(bool enableTrace) { m_isTraceEnabled = enableTrace; }

    /** @brief Queries whether trace mode is enabled. */
    inline bool isTraceEnabled() { return m_isTraceEnabled; }

protected:
    /** @brief Constructor. */
    ELogErrorHandler(bool enableTrace = false) : m_isTraceEnabled(enableTrace) {}

private:
    bool m_isTraceEnabled;
};

}  // namespace elog

#endif  // __ELOG_ERROR_HANDLER_H__