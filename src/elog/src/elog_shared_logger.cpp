#include "elog_shared_logger.h"

namespace elog {

// ATTENTION: the following thread local variables cannot be class members, because these causes
// some conflict in TLS destruction under MinGW (the logger object in which they might be defined is
// overwritten with dead-land bit pattern when destroyed, so when C runtime TLS destruction runs for
// some thread when thread exits, the destructor of the TLS variable runs with an object whose
// contents is dead-land and the application crashes)

static thread_local ELogRecordBuilder sRecordData;

ELogRecordBuilder& ELogSharedLogger::getRecordBuilder() { return sRecordData; }

const ELogRecordBuilder& ELogSharedLogger::getRecordBuilder() const { return sRecordData; }

}  // namespace elog
