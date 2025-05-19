#ifndef __ELOG_SHARED_LOGGER_H__
#define __ELOG_SHARED_LOGGER_H__

#include "elog_logger.h"

namespace elog {

class ELOG_API ELogSharedLogger : public ELogLogger {
public:
    ELogSharedLogger(ELogSource* logSource) : ELogLogger(logSource) {}
    ~ELogSharedLogger() final {}

protected:
    ELogRecordBuilder& getRecordBuilder() final;
    const ELogRecordBuilder& getRecordBuilder() const final;
};
}  // namespace elog

#endif  // __ELOG_SHARED_LOGGER_H__