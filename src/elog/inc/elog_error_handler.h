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

    virtual void onTrace(const char* msg) = 0;

protected:
    ELogErrorHandler() {}
};

}  // namespace elog

#endif  // __ELOG_ERROR_HANDLER_H__