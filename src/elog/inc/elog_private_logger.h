#ifndef __ELOG_PRIVATE_LOGGER_H__
#define __ELOG_PRIVATE_LOGGER_H__

#include "elog_def.h"
#include "elog_logger.h"

namespace elog {

class ELOG_API ELogPrivateLogger : public ELogLogger {
public:
    ELogPrivateLogger(ELogSource* logSource) : ELogLogger(logSource) {}
    ~ELogPrivateLogger() final {}

protected:
    ELogRecordBuilder& getRecordBuilder() final { return m_recordBuilder; }
    const ELogRecordBuilder& getRecordBuilder() const final { return m_recordBuilder; }

private:
    ELogRecordBuilder m_recordBuilder;
};

}  // namespace elog

#endif  // __ELOG_PRIVATE_LOGGER_H__