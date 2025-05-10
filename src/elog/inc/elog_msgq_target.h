#ifndef __ELOG_MSGQ_TARGET_H__
#define __ELOG_MSGQ_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_msgq_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for message queue log targets. */
class ELogMsgQTarget : public ELogTarget {
public:
    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() override {}

protected:
    ELogMsgQTarget() : ELogTarget("msgq") {}
    ~ELogMsgQTarget() override {}

    /**
     * @brief Parses the headers loaded from configuration, builds all log record field selectors,
     * and extracts header names.
     * @param headers The insert statement to parse.
     * @return true If succeeded, otherwise false.
     */
    inline bool parseHeaders(const std::string& headers) {
        return m_formatter.parseHeaders(headers);
    }

    inline const std::string& getHeaderNameAt(uint32_t index) const {
        return m_formatter.getHeaderNameAt(index);
    }

    inline uint32_t getHeaderCount() const { return m_formatter.getHeaderCount(); }

    inline const std::vector<std::string>& getHeaderNames() const {
        return m_formatter.getHeaderNames();
    }

    /**
     * @brief Applies all field selectors to the given log record, so that all headers are filled.
     * @param logRecord The log record to process.
     * @param receptor The receptor that receives log record fields and transfers them to the
     * message headers.
     */
    inline void fillInHeaders(const elog::ELogRecord& logRecord,
                              elog::ELogFieldReceptor* receptor) {
        m_formatter.fillInHeaders(logRecord, receptor);
    }

private:
    ELogMsgQFormatter m_formatter;
};

}  // namespace elog

#endif  // __ELOG_MSGQ_TARGET_H__