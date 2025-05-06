#ifndef __ELOG_MSGQ_TARGET_H__
#define __ELOG_MSGQ_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

// #include "elog_db_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for message queue log targets. */
class ELogMsgQTarget : public ELogTarget {
public:
    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final {}

protected:
    ELogMsgQTarget() {}
    ~ELogMsgQTarget() override {}
};

}  // namespace elog

#endif  // __ELOG_MSGQ_TARGET_H__