#ifndef __ELOG_MON_TARGET_H__
#define __ELOG_MON_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_db_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for monitoring tool log targets. */
class ELOG_API ELogMonTarget : public ELogTarget {
protected:
    ELogMonTarget() : ELogTarget("mon") {}
    ELogMonTarget(const ELogMonTarget&) = delete;
    ELogMonTarget(ELogMonTarget&&) = delete;
    ELogMonTarget& operator=(const ELogMonTarget&) = delete;
    ~ELogMonTarget() override {}
};

}  // namespace elog

#endif  // __ELOG_MON_TARGET_H__