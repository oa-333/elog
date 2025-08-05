#include "elog_stack_trace.h"

#ifdef ELOG_ENABLE_STACK_TRACE

#include <algorithm>

#include "os_module_manager.h"

namespace elog {

static void* sELogBaseAddress = nullptr;
static void* sDbgUtilBaseAddress = nullptr;

void initStackTrace() {
    dbgutil::OsModuleInfo modInfo;
#ifdef ELOG_MSVC
    const char* elogModName = "elog.dll";
    const char* dbgutilModName = "dbgutil.dll";
#elif defined(ELOG_MINGW)
    const char* elogModName = "libelog.dll";
    const char* dbgutilModName = "libdbgutil.dll";
#else
    const char* elogModName = "libelog.so";
    const char* dbgutilModName = "libdbgutil.so";
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
        // erase dbgutil and elog frames
        // NOTE: if initialization failed then the searched addresses are null and so no frame will
        // be erased
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

bool ELogStackEntryFilter::filterStackEntry(const dbgutil::StackEntry& stackEntry) {
    // discard dbgutil and elog frames
    // NOTE: if initialization failed then the searched addresses are null and so no frame will
    // be discarded
    return (stackEntry.m_entryInfo.m_moduleBaseAddress != sELogBaseAddress &&
            stackEntry.m_entryInfo.m_moduleBaseAddress != sDbgUtilBaseAddress);
}

}  // namespace elog

#endif  // ELOG_ENABLE_STACK_TRACE