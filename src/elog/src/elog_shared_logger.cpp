#include "elog_shared_logger.h"

#include <cassert>

#include "elog_aligned_alloc.h"
#include "elog_report.h"
#include "elog_tls.h"

namespace elog {

// use TLS instead of thread_local due to MinGW bug (static thread_local variable destruction
// sometimes takes place twice, not clear under which conditions)
static ELogTlsKey sRecordBuilderKey = ELOG_INVALID_TLS_KEY;

inline ELogRecordBuilder* allocRecordBuilder() {
    return elogAlignedAllocObject<ELogRecordBuilder>(ELOG_CACHE_LINE);
}

inline void freeRecordBuilder(void* data) {
    ELogRecordBuilder* recordBuilder = (ELogRecordBuilder*)data;
    if (recordBuilder != nullptr) {
        elogAlignedFreeObject(recordBuilder);
    }
}

static ELogRecordBuilder* getOrCreateTlsRecordBuilder() {
    ELogRecordBuilder* recordBuilder = (ELogRecordBuilder*)elogGetTls(sRecordBuilderKey);
    if (recordBuilder == nullptr) {
        recordBuilder = allocRecordBuilder();
        if (recordBuilder == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate thread-local log buffer");
            return nullptr;
        }
        if (!elogSetTls(sRecordBuilderKey, recordBuilder)) {
            ELOG_REPORT_ERROR("Failed to set thread-local log buffer");
            freeRecordBuilder(recordBuilder);
            return nullptr;
        }
    }
    return recordBuilder;
}

// due to MinGW issues with static/thread-local destruction (it crashes sometimes), we use instead
// thread-local pointers
static thread_local ELogRecordBuilder* sRecordBuilderHead = nullptr;
static thread_local ELogRecordBuilder* sRecordBuilder = nullptr;

inline void ensureRecordBuilderExists() {
    if (sRecordBuilder == nullptr) {
        // create on-demand on a per-thread basis
        sRecordBuilder = getOrCreateTlsRecordBuilder();
        assert(sRecordBuilderHead == nullptr);
        sRecordBuilderHead = sRecordBuilder;
    }
}

bool ELogSharedLogger::createRecordBuilderKey() {
    if (sRecordBuilderKey != ELOG_INVALID_TLS_KEY) {
        ELOG_REPORT_ERROR("Cannot create record builder TLS key, already created");
        return false;
    }
    return elogCreateTls(sRecordBuilderKey, freeRecordBuilder);
}

bool ELogSharedLogger::destroyRecordBuilderKey() {
    if (sRecordBuilderKey == ELOG_INVALID_TLS_KEY) {
        // silently ignore the request
        return true;
    }
    bool res = elogDestroyTls(sRecordBuilderKey);
    if (res) {
        sRecordBuilderKey = ELOG_INVALID_TLS_KEY;
    }
    return res;
}

ELogRecordBuilder* ELogSharedLogger::getRecordBuilder() {
    ensureRecordBuilderExists();
    // we cannot afford a failure here, this is fatal
    assert(sRecordBuilder != nullptr);
    return sRecordBuilder;
}

const ELogRecordBuilder* ELogSharedLogger::getRecordBuilder() const {
    ensureRecordBuilderExists();
    // we cannot afford a failure here, this is fatal
    assert(sRecordBuilder != nullptr);
    return sRecordBuilder;
}

ELogRecordBuilder* ELogSharedLogger::pushRecordBuilder() {
    ELogRecordBuilder* recordBuilder =
        elogAlignedAllocObject<ELogRecordBuilder>(ELOG_CACHE_LINE, sRecordBuilder);
    if (recordBuilder != nullptr) {
        sRecordBuilder = recordBuilder;
    }
    return sRecordBuilder;
}

void ELogSharedLogger::popRecordBuilder() {
    if (sRecordBuilder != sRecordBuilderHead) {
        ELogRecordBuilder* next = sRecordBuilder->getNext();
        elogAlignedFreeObject(sRecordBuilder);
        sRecordBuilder = next;
    }
}

}  // namespace elog
