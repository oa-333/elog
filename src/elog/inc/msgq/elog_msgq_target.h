#ifndef __ELOG_MSGQ_TARGET_H__
#define __ELOG_MSGQ_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_props_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for message queue log targets. */
class ELOG_API ELogMsgQTarget : public ELogTarget {
protected:
    ELogMsgQTarget() : ELogTarget("msgq") {}
    ELogMsgQTarget(const ELogMsgQTarget&) = delete;
    ELogMsgQTarget(ELogMsgQTarget&&) = delete;
    ELogMsgQTarget& operator=(const ELogMsgQTarget&) = delete;
    ~ELogMsgQTarget() override {}

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() override { return true; }

    /**
     * @brief Parses the headers loaded from configuration, builds all log record field selectors,
     * and extracts header names.
     * @param headers The headers to parse.
     * @return true If succeeded, otherwise false.
     */
    inline bool parseHeaders(const std::string& headers) { return m_formatter.parseProps(headers); }

    inline const std::string& getHeaderNameAt(uint32_t index) const {
        return m_formatter.getPropNameAt(index);
    }

    inline uint32_t getHeaderCount() const { return m_formatter.getPropCount(); }

    inline const std::vector<std::string>& getHeaderNames() const {
        return m_formatter.getPropNames();
    }

    /**
     * @brief Applies all field selectors to the given log record, so that all headers are filled.
     * @param logRecord The log record to process.
     * @param receptor The receptor that receives log record fields and transfers them to the
     * message headers.
     */
    inline void fillInHeaders(const elog::ELogRecord& logRecord,
                              elog::ELogFieldReceptor* receptor) {
        m_formatter.fillInProps(logRecord, receptor);
    }

private:
    ELogPropsFormatter m_formatter;
};

}  // namespace elog

#endif  // __ELOG_MSGQ_TARGET_H__