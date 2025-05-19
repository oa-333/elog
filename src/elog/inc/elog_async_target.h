#ifndef __ELOG_ASYNC_TARGET_H__
#define __ELOG_ASYNC_TARGET_H__

#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for asynchronous log targets. */
class ELOG_API ELogAsyncTarget : public ELogTarget {
public:
    ~ELogAsyncTarget() override {
        if (m_endTarget != nullptr) {
            delete m_endTarget;
            m_endTarget = nullptr;
        }
    }

protected:
    ELogAsyncTarget(ELogTarget* endTarget) : ELogTarget("async"), m_endTarget(endTarget) {}

    ELogTarget* getEndLogTarget() { return m_endTarget; }

private:
    ELogTarget* m_endTarget;
};

}  // namespace elog

#endif  // __ELOG_ASYNC_TARGET_H__