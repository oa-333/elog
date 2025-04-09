#ifndef __ELOG_SHARED_LOGGER_H__
#define __ELOG_SHARED_LOGGER_H__

#include "elog_def.h"
#include "elog_logger.h"

namespace elog {

class DLL_EXPORT ELogSharedLogger : public ELogLogger {
public:
    ELogSharedLogger(ELogSource* logSource) : ELogLogger(logSource) {}
    ~ELogSharedLogger() final {}

protected:
    ELogRecordBuilder& getRecordBuilder() final;
    const ELogRecordBuilder& getRecordBuilder() const final;
};
}  // namespace elog

#endif  // __ELOG_SHARED_LOGGER_H__