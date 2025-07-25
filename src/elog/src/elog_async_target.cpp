#include "elog_async_target.h"

namespace elog {

ELogAsyncTarget::~ELogAsyncTarget() {
    if (m_subTarget != nullptr) {
        delete m_subTarget;
        m_subTarget = nullptr;
    }
}

ELogAsyncTarget::ELogAsyncTarget(ELogTarget* subTarget)
    : ELogTarget("async"), m_subTarget(subTarget) {
    setNativelyThreadSafe();
    m_subTarget->setExternallyThreadSafe();

    // if no flush policy is set, then the end target is responsible for occasional flush.
    // in this case we avoid the "never" flush policy for performance reasons
}

}  // namespace elog