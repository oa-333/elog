#include "elog_record.h"

#include <cstdio>
#include <ctime>

#include "elog_logger.h"
#include "elog_report.h"

namespace elog {

static const char ELOG_ROOT_NAME[] = "elog_root";
static const char ELOG_MODULE_NAME[] = "elog";

const char* getLogSourceName(const ELogRecord& logRecord, size_t& length) {
    const char* logSourceName = logRecord.m_logger->getLogSource()->getQualifiedName();
    length = logRecord.m_logger->getLogSource()->getQualifiedNameLength();
    if (logSourceName == nullptr || *logSourceName == 0) {
        logSourceName = ELOG_ROOT_NAME;
        length = sizeof(ELOG_ROOT_NAME) - 1;  // do not include terminating null
    }
    return logSourceName;
}

const char* getLogModuleName(const ELogRecord& logRecord, size_t& length) {
    const char* moduleName = logRecord.m_logger->getLogSource()->getModuleName();
    length = logRecord.m_logger->getLogSource()->getModuleNameLength();
    if (moduleName == nullptr || *moduleName == 0) {
        moduleName = ELOG_MODULE_NAME;
        length = sizeof(ELOG_MODULE_NAME) - 1;  // do not include terminating null
    }
    return moduleName;
}

}  // namespace elog
