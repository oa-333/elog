#ifndef __ELOG_ASYNC_TARGET_H__
#define __ELOG_ASYNC_TARGET_H__

#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for asynchronous log targets. */
class ELOG_API ELogAsyncTarget : public ELogTarget {
public:
    ~ELogAsyncTarget() override;

    /** @brief Retrieves the subordinate log target. */
    ELogTarget* getSubTarget() { return m_subTarget; }

    /** @brief Retrieves the final subordinate log target in case of chain of several targets. */
    ELogTarget* getEndLogTarget() override { return m_subTarget->getEndLogTarget(); }

protected:
    ELogAsyncTarget(ELogTarget* subTarget);
    ELogAsyncTarget() = delete;
    ELogAsyncTarget(const ELogAsyncTarget&) = delete;
    ELogAsyncTarget(ELogAsyncTarget&&) = delete;
    ELogAsyncTarget& operator=(const ELogAsyncTarget&) = delete;

    ELogTarget* m_subTarget;

    /** @brief Creates a statistics object. */
    ELogStats* createStats() override { return new (std::nothrow) AsyncStats(); }

    struct AsyncStats : public ELogStats {
        AsyncStats() {}
        AsyncStats(const AsyncStats&) = delete;
        AsyncStats(AsyncStats&&) = delete;
        AsyncStats& operator=(const AsyncStats&) = delete;
        ~AsyncStats() final {}

        void toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg = "") override {
            ELogStats::toString(buffer, logTarget, msg);
            ELogAsyncTarget* asyncTarget = (ELogAsyncTarget*)logTarget;
            ELogTarget* subTarget = asyncTarget->getSubTarget();
            subTarget->getStats()->toString(buffer, subTarget, "sub-target statistics");
        }
    };
};

}  // namespace elog

#endif  // __ELOG_ASYNC_TARGET_H__