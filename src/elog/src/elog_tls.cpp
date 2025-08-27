#include "elog_tls.h"

#include "elog_report.h"

#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#ifdef ELOG_WINDOWS
#include <cstdio>
#include <new>

#include "elog_win32_dll_event.h"
#endif

namespace elog {

#ifdef ELOG_WINDOWS
struct ELogTlsCleanupData {
    ELogTlsCleanupData(elogTlsDestructorFunc dtor, ELogTlsKey key) : m_dtor(dtor), m_key(key) {}
    elogTlsDestructorFunc m_dtor;
    ELogTlsKey m_key;
};

static void elogTlsCleanup(int event, void* userData) {
    ELogTlsCleanupData* cleanupData = (ELogTlsCleanupData*)userData;
    if (event == ELOG_DLL_THREAD_DETACH) {
        // fprintf(stderr, "Running TLS cleanup at %p\n", cleanupData);
        if (cleanupData == nullptr) {
            // no cleanup data, we give up
            return;
        }
        // get the tls value and call cleanup function
        elogTlsDestructorFunc dtor = cleanupData->m_dtor;
        void* tlsValue = elogGetTls(cleanupData->m_key);
        if (tlsValue != nullptr) {
            (*dtor)(tlsValue);
            elogSetTls(cleanupData->m_key, nullptr);
        }
    }
}

class ELogTlsKeyPurge : public ELogWin32DllPurgeFilter {
public:
    ELogTlsKeyPurge(ELogTlsKey key) : m_key(key) {}
    ELogTlsKeyPurge(const ELogTlsKeyPurge&) = delete;
    ELogTlsKeyPurge(ELogTlsKeyPurge&&) = delete;
    ELogTlsKeyPurge& operator=(const ELogTlsKeyPurge&) = delete;
    ~ELogTlsKeyPurge() final {}

    bool purge(elogWin32ThreadDllEventCB callback, void* userData) final {
        ELogTlsCleanupData* cleanupData = (ELogTlsCleanupData*)userData;
        if (cleanupData != nullptr && cleanupData->m_key == m_key) {
            delete cleanupData;
            return true;
        }
        return false;
    }

private:
    ELogTlsKey m_key;
};
#endif

bool elogCreateTls(ELogTlsKey& key, elogTlsDestructorFunc dtor /* = nullptr */) {
#ifdef ELOG_WINDOWS
    key = TlsAlloc();
    if (key == TLS_OUT_OF_INDEXES) {
        ELOG_REPORT_ERROR("Cannot allocate thread local storage slot, out of slots");
        return false;
    }
    if (dtor != nullptr) {
        ELogTlsCleanupData* cleanupData = new (std::nothrow) ELogTlsCleanupData(dtor, key);
        if (cleanupData == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate thread local storage cleanup data, out of memory");
            elogDestroyTls(key);
            return false;
        }
        registerDllCallback(elogTlsCleanup, cleanupData);
    }
#else
    int res = pthread_key_create(&key, dtor);
    if (res != 0) {
        ELOG_REPORT_SYS_ERROR_NUM(pthread_key_create, res,
                                  "Failed to allocate thread local storage slot");
        return false;
    }
#endif
    return true;
}

bool elogDestroyTls(ELogTlsKey key) {
#ifdef ELOG_WINDOWS
    // remove the callback used to cleanup this key in each thread
    // NOTE: caller is expected to call elogDestroyTls after all relevant threads have terminated
    ELogTlsKeyPurge purge(key);
    purgeDllCallback(&purge);
    if (!TlsFree(key)) {
        ELOG_REPORT_WIN32_ERROR(TlsFree, "Failed to free thread local storage slot by key %lu",
                                key);
        return false;
    }
#else
    int res = pthread_key_delete(key);
    if (res != 0) {
        ELOG_REPORT_SYS_ERROR_NUM(pthread_key_delete, res, "Failed to delete thread local key");
        return false;
    }
#endif
    return true;
}

void* elogGetTls(ELogTlsKey key) {
#ifdef ELOG_WINDOWS
    return TlsGetValue(key);
#else
    return pthread_getspecific(key);
#endif
}

bool elogSetTls(ELogTlsKey key, void* value) {
#ifdef ELOG_WINDOWS
    if (!TlsSetValue(key, value)) {
        ELOG_REPORT_WIN32_ERROR(TlsSetValue, "Failed to set thread local storage value");
        return false;
    }
#else
    int res = pthread_setspecific(key, value);
    if (res != 0) {
        ELOG_REPORT_SYS_ERROR(pthread_setspecific, "Failed to set thread local storage value");
        return false;
    }
#endif
    return true;
}

}  // namespace elog
