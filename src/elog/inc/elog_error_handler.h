#ifndef __ELOG_ERROR_HANDLER_H__
#define __ELOG_ERROR_HANDLER_H__

namespace elog {

/** @brief Error handling interface. */
class ELogErrorHandler {
public:
    ELogErrorHandler(const ELogErrorHandler&) = delete;
    ELogErrorHandler(ELogErrorHandler&&) = delete;
    virtual ~ELogErrorHandler() {}

    virtual void onError(const char* msg) = 0;

protected:
    ELogErrorHandler() {}
};

}  // namespace elog

#endif  // __ELOG_ERROR_HANDLER_H__