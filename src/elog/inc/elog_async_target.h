#ifndef __ELOG_ASYNC_TARGET_H__
#define __ELOG_ASYNC_TARGET_H__

#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for asynchronous log targets. */
class ELOG_API ELogAsyncTarget : public ELogTarget {
public:
    ~ELogAsyncTarget() override;

    ELogTarget* getEndLogTarget() override { return m_endTarget; }

protected:
    ELogAsyncTarget(ELogTarget* endTarget);

    ELogTarget* m_endTarget;
};

}  // namespace elog

#endif  // __ELOG_ASYNC_TARGET_H__