#ifndef __ELOG_TARGET_H__
#define __ELOG_TARGET_H__

#include <string>
#include <vector>

#include "elog_def.h"
#include "elog_record.h"

namespace elog {

class ELogFlushPolicy;

/**
 * @interface Parent interface for all log targets. Used to decouple log formatting from logging.
 * Possible log targets could be:
 * - Log file (possibly segmented)
 * - External logging system (adapter to containing application)
 * - Message Queue of some message broker system
 */
class DLL_EXPORT ELogTarget {
public:
    virtual ~ELogTarget() {}

    /** @brief Order the log target to start (required for threaded targets). */
    virtual bool start() = 0;

    /** @brief Order the log target to stop (required for threaded targets). */
    virtual bool stop() = 0;

    /** @brief Sends a log record to a log target. */
    virtual void log(const ELogRecord& logRecord) = 0;

    /** @brief Orders a buffered log target to flush it log messages. */
    virtual void flush() = 0;

protected:
    ELogTarget() {}
};

/** @class Combined log target. Dispatches to multiple log targets. */
class DLL_EXPORT ELogCombinedTarget : public ELogTarget {
public:
    ELogCombinedTarget() {}
    ~ELogCombinedTarget() final {}

    inline void addLogTarget(ELogTarget* target) { m_logTargets.push_back(target); }

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

private:
    std::vector<ELogTarget*> m_logTargets;
};

/**
 * @class Abstract parent class for chained lgo target. Log targets can be chained together, so that
 * filtering/batching/grouping can take place.
 */
class DLL_EXPORT ELogChainedTarget : public ELogTarget {
public:
    ~ELogChainedTarget() override {}

protected:
    ELogChainedTarget(ELogTarget* chainedTarget) : m_chainedTarget(chainedTarget) {}

    ELogTarget* m_chainedTarget;
};

/**
 * @class Abstract log target, providing partial implementation to the log target interface. In
 * particular, it implements log filtering and formatting, as well as applying a given flush policy.
 * This might not suite all log targets, as log formatting might take place on a later occasion.
 */
class DLL_EXPORT ELogAbstractTarget : public ELogTarget {
public:
    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) override;

protected:
    ELogAbstractTarget(ELogFlushPolicy* flushPolicy) : m_flushPolicy(flushPolicy) {}
    ~ELogAbstractTarget() override {}

    /** @brief Log a formatted message. */
    virtual void log(const std::string& formattedLogMsg) = 0;

private:
    ELogFlushPolicy* m_flushPolicy;
};

}  // namespace elog

#endif  // __ELOG_TARGET_H__