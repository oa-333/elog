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

typedef void (*elogWin32ThreadDllEventCB)(int, void*);

class ELogWin32DllListener {
public:
    virtual void onThreadDllAttach() = 0;
    virtual void onThreadDllDetach() = 0;
    virtual void onProcessDllDetach() = 0;
};

class ELogWin32DllPurgeFilter {
public:
    virtual bool purge(elogWin32ThreadDllEventCB callback, void* userData) = 0;
};

// listener API
extern void registerDllListener(ELogWin32DllListener* listener);
extern void deregisterDllListener(ELogWin32DllListener* listener);

// callback API
extern void registerDllCallback(elogWin32ThreadDllEventCB callback, void* userData);
extern void deregisterDllCallback(elogWin32ThreadDllEventCB callback);
extern void* getDllCallbackUserData(elogWin32ThreadDllEventCB callback);
extern void purgeDllCallback(ELogWin32DllPurgeFilter* filter);

}  // namespace elog

#endif  // ELOG_WINDOWS

#endif  // __ELOG_WIN32_DLL_EVENT_H__