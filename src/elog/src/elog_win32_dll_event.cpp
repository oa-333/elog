#include "elog_win32_dll_event.h"

#ifdef ELOG_WINDOWS

// on MinGW we need to include windows header
#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <algorithm>
#include <vector>

#include "elog_report.h"

namespace elog {
typedef std::vector<ELogWin32DllListener*> ListenerList;
static ListenerList sListeners;
typedef std::vector<std::pair<elogWin32ThreadDllEventCB, void*>> CallbackList;
static CallbackList sCallbacks;

void registerDllListener(ELogWin32DllListener* listener) { sListeners.push_back(listener); }

void deregisterDllListener(ELogWin32DllListener* listener) {
    ListenerList::iterator itr = std::find(sListeners.begin(), sListeners.end(), listener);
    if (itr != sListeners.end()) {
        sListeners.erase(itr);
    }
}

void registerDllCallback(elogWin32ThreadDllEventCB callback, void* userData) {
    sCallbacks.emplace_back(callback, userData);
}

void deregisterDllCallback(elogWin32ThreadDllEventCB callback) {
    CallbackList::iterator itr =
        std::find_if(sCallbacks.begin(), sCallbacks.end(),
                     [callback](std::pair<elogWin32ThreadDllEventCB, void*>& cbPair) {
                         return cbPair.first == callback;
                     });
    if (itr != sCallbacks.end()) {
        sCallbacks.erase(itr);
    }
}

void* getDllCallbackUserData(elogWin32ThreadDllEventCB callback) {
    CallbackList::iterator itr =
        std::find_if(sCallbacks.begin(), sCallbacks.end(),
                     [callback](std::pair<elogWin32ThreadDllEventCB, void*>& cbPair) {
                         return cbPair.first == callback;
                     });
    if (itr != sCallbacks.end()) {
        return itr->second;
    }
    return nullptr;
}

void purgeDllCallback(ELogWin32DllPurgeFilter* filter) {
    CallbackList::iterator itr = sCallbacks.begin();
    while (itr != sCallbacks.end()) {
        if (filter->purge(itr->first, itr->second)) {
            itr = sCallbacks.erase(itr);
        } else {
            ++itr;
        }
    }
}

static void notifyThreadAttach() {
    for (ELogWin32DllListener* listener : sListeners) {
        listener->onThreadDllAttach();
    }
    for (auto& cbPair : sCallbacks) {
        (*cbPair.first)(ELOG_DLL_THREAD_ATTACH, cbPair.second);
    }
}

static void notifyThreadDetach() {
    for (ELogWin32DllListener* listener : sListeners) {
        listener->onThreadDllDetach();
    }
    for (auto& cbPair : sCallbacks) {
        (*cbPair.first)(ELOG_DLL_THREAD_DETACH, cbPair.second);
    }
}

static void notifyProcessDetach() {
    for (ELogWin32DllListener* listener : sListeners) {
        listener->onProcessDllDetach();
    }
    for (auto& cbPair : sCallbacks) {
        (*cbPair.first)(ELOG_DLL_PROCESS_DETACH, cbPair.second);
    }
}

static void handleWin32DllNotification(HINSTANCE hinstDLL,  // handle to DLL module
                                       DWORD fdwReason,     // reason for calling function
                                       LPVOID lpvReserved)  // reserved
{
    // Perform actions based on the reason for calling.
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            // Initialize once for each new process.
            // Return FALSE to fail DLL load.
            ELOG_REPORT_TRACE("DLL at 0x%p loaded", (void*)hinstDLL);
            break;

        case DLL_THREAD_ATTACH:
            ELOG_REPORT_TRACE("Thread starting");
            elog::notifyThreadAttach();
            break;

        case DLL_THREAD_DETACH:
            ELOG_REPORT_TRACE("Thread terminating");
            elog::notifyThreadDetach();
            break;

        case DLL_PROCESS_DETACH:
            if (lpvReserved != nullptr) {
                ELOG_REPORT_TRACE("Process is shutting down");
                break;  // do not do cleanup if process termination scenario
            }

            // Perform any necessary cleanup.
            ELOG_REPORT_TRACE("DLL at 0x%p unloading", (void*)hinstDLL);
            elog::notifyProcessDetach();
            break;

        default:
            ELOG_REPORT_WARN("Invalid Win32 DLL notification code: %lu", fdwReason);
            break;
    }
}

}  // namespace elog

BOOL WINAPI DllMain(HINSTANCE hinstDLL,  // handle to DLL module
                    DWORD fdwReason,     // reason for calling function
                    LPVOID lpvReserved)  // reserved
{
    elog::handleWin32DllNotification(hinstDLL, fdwReason, lpvReserved);
    return TRUE;  // designates successful DLL_PROCESS_ATTACH.
}

#endif