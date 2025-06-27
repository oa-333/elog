#include "elog_private_logger.h"

#include "elog_aligned_alloc.h"

namespace elog {

void ELogPrivateLogger::pushRecordBuilder() {
    ELogRecordBuilder* recordBuilder =
        elogAlignedAllocObject<ELogRecordBuilder>(ELOG_CACHE_LINE, m_recordBuilder);
    if (recordBuilder != nullptr) {
        m_recordBuilder = recordBuilder;
    }
}

void ELogPrivateLogger::popRecordBuilder() {
    if (m_recordBuilder != &m_recordBuilderHead) {
        ELogRecordBuilder* next = m_recordBuilder->getNext();
        elogAlignedFreeObject(m_recordBuilder);
        m_recordBuilder = next;
    }
}

}  // namespace elog