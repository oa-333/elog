#ifndef __ELOG_WIN32_DLL_EVENT_H__
#define __ELOG_WIN32_DLL_EVENT_H__

#include "elog_def.h"
#ifdef ELOG_WINDOWS

#include <cstdint>

namespace elog {

#define ELOG_DLL_PROCESS_ATTACH 1
#define ELOG_DLL_PROCESS_DETACH 2
#define ELOG_DLL_THREAD_ATTACH 3
#define ELOG_DLL_THREAD_DETACH 4

typedef void (*elogWin32ThreadDllEventCB)(int /* event */, void* /* userData */);

class ELogWin32DllListener {
public:
    virtual ~ELogWin32DllListener() {}

    virtual void onThreadDllAttach() = 0;
    virtual void onThreadDllDetach() = 0;
    virtual void onProcessDllDetach() = 0;

protected:
    ELogWin32DllListener() {}
    ELogWin32DllListener(const ELogWin32DllListener&) = delete;
    ELogWin32DllListener(ELogWin32DllListener&&) = delete;
    ELogWin32DllListener& operator=(const ELogWin32DllListener&) = delete;
};

class ELogWin32DllPurgeFilter {
public:
    virtual ~ELogWin32DllPurgeFilter() {}

    virtual bool purge(elogWin32ThreadDllEventCB callback, void* userData) = 0;

protected:
    ELogWin32DllPurgeFilter() {}
    ELogWin32DllPurgeFilter(const ELogWin32DllPurgeFilter&) = delete;
    ELogWin32DllPurgeFilter(ELogWin32DllPurgeFilter&&) = delete;
    ELogWin32DllPurgeFilter& operator=(const ELogWin32DllPurgeFilter&) = delete;
};

// listener API
extern void registerDllListener(ELogWin32DllListener* listener);
extern void deregisterDllListener(ELogWin32DllListener* listener);

// callback API
extern void registerDllCallback(elogWin32ThreadDllEventCB callback, void* userData);
extern void deregisterDllCallback(elogWin32ThreadDllEventCB callback);
extern void* getDllCallbackUserData(elogWin32ThreadDllEventCB callback);

// remove callbacks according to filter
extern void purgeDllCallback(ELogWin32DllPurgeFilter* filter);

}  // namespace elog

#endif  // ELOG_WINDOWS

#endif  // __ELOG_WIN32_DLL_EVENT_H__