#include "elog_stack_trace.h"

#ifdef ELOG_ENABLE_STACK_TRACE

#include <algorithm>

#include "os_module_manager.h"

namespace elog {

static void* sELogBaseAddress = nullptr;
static void* sDbgUtilBaseAddress = nullptr;

void initStackTrace() {
    dbgutil::OsModuleInfo modInfo;
#ifdef ELOG_WINDOWS
    const char* elogModName = "elog.dll";
    const char* dbgutilModName = "dbgutil.dll";
#else
    const char* elogModName = "elog.so";
    const char* dbgutilModName = "dbgutil.so";
#endif
    dbgutil::DbgUtilErr rc =
        dbgutil::getModuleManager()->getModuleByName(elogModName, modInfo, true);
    if (rc == DBGUTIL_ERR_OK) {
        sELogBaseAddress = modInfo.m_loadAddress;
    }
    rc = dbgutil::getModuleManager()->getModuleByName(dbgutilModName, modInfo);
    if (rc == DBGUTIL_ERR_OK) {
        sDbgUtilBaseAddress = modInfo.m_loadAddress;
    }
}

bool getStackTraceVector(dbgutil::StackTrace& stackTrace) {
    dbgutil::DbgUtilErr rc = dbgutil::getStackTrace(stackTrace);
    if (rc != DBGUTIL_ERR_OK) {
        return false;
    }

    // remove all frames from dbgutil or elog
    std::erase_if(stackTrace, [](dbgutil::StackEntry& stackEntry) {
        return (stackEntry.m_entryInfo.m_moduleBaseAddress == sELogBaseAddress ||
                stackEntry.m_entryInfo.m_moduleBaseAddress == sDbgUtilBaseAddress);
    });

    // fix frame numbers
    for (uint32_t i = 0; i < stackTrace.size(); ++i) {
        stackTrace[i].m_frameIndex = i;
    }
    return true;
}

bool getStackTraceString(std::string& stackTraceString) {
    dbgutil::StackTrace stackTrace;
    if (!getStackTraceVector(stackTrace)) {
        return false;
    }
    stackTraceString = dbgutil::stackTraceToString(stackTrace);
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_STACK_TRACE