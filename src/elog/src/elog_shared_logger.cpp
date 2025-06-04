#include "elog_shared_logger.h"

#include <cassert>

namespace elog {

// ATTENTION: the following thread local variables cannot be class members, because these causes
// some conflict in TLS destruction under MinGW (the logger object in which they might be defined is
// overwritten with dead-land bit pattern when destroyed, so when C runtime TLS destruction runs for
// some thread when thread exits, the destructor of the TLS variable runs with an object whose
// contents is dead-land and the application crashes)

static thread_local ELogRecordBuilder sRecordBuilderHead;
static thread_local ELogRecordBuilder* sRecordBuilder = &sRecordBuilderHead;

ELogRecordBuilder& ELogSharedLogger::getRecordBuilder() {
    assert(sRecordBuilder != nullptr);
    return *sRecordBuilder;
}

const ELogRecordBuilder& ELogSharedLogger::getRecordBuilder() const {
    assert(sRecordBuilder != nullptr);
    return *sRecordBuilder;
}

void ELogSharedLogger::pushRecordBuilder() {
    ELogRecordBuilder* recordBuilder = new (std::nothrow) ELogRecordBuilder(sRecordBuilder);
    if (recordBuilder != nullptr) {
        sRecordBuilder = recordBuilder;
    }
}

void ELogSharedLogger::popRecordBuilder() {
    if (sRecordBuilder != &sRecordBuilderHead) {
        ELogRecordBuilder* next = sRecordBuilder->getNext();
        delete sRecordBuilder;
        sRecordBuilder = next;
    }
}

}  // namespace elog
