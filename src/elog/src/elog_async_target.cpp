#include "elog_async_target.h"

namespace elog {

ELogAsyncTarget::~ELogAsyncTarget() {
    if (m_endTarget != nullptr) {
        delete m_endTarget;
        m_endTarget = nullptr;
    }
}

ELogAsyncTarget::ELogAsyncTarget(ELogTarget* endTarget)
    : ELogTarget("async"), m_endTarget(endTarget) {
    setNativelyThreadSafe();
    m_endTarget->setExternallyThreadSafe();

    // if no flush policy is set, then the end target is responsible for occasional flush.
    // in this case we avoid the "never" flush policy for performance reasons
}

}  // namespace elog