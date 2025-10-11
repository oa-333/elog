#ifndef __ELOG_MSG_H__
#define __ELOG_MSG_H__

#ifdef ELOG_ENABLE_MSG

#include <msg/msg.h>

#include <cstdint>
#include <vector>

#include "elog_def.h"

// common message ids

/** @def Single record message id. */
#define ELOG_RECORD_MSG_ID 1

/** @def Response status message id. */
#define ELOG_STATUS_MSG_ID 2

/** @def Config level query message id. */
#define ELOG_CONFIG_LEVEL_QUERY_MSG_ID 3

/** @def Config level report message id. */
#define ELOG_CONFIG_LEVEL_REPORT_MSG_ID 4

/** @def Config level update message id. */
#define ELOG_CONFIG_LEVEL_UPDATE_MSG_ID 5

/** @def Config level reply message id. */
#define ELOG_CONFIG_LEVEL_REPLY_MSG_ID 6

namespace elog {

/** @typedef Message buffer type. */
typedef std::vector<char> ELogMsgBuffer;

/**
 * Internal ELog protocol messages.
 */

// TODO: we need a formatter here that will decide which fields will be used
// the intuitive way is to use protobuf, which supports this ability
// otherwise we need to replicate this feature with fields ids or bit sets saying which field is
// used
// this does not make any sense so we will use protobuf as the default binary format

class ELOG_API ELogRecordMsg : public commutil::Serializable {
public:
    ELogRecordMsg() {}
    ELogRecordMsg(const ELogRecordMsg&) = delete;
    ELogRecordMsg(ELogRecordMsg&&) = delete;
    ELogRecordMsg& operator=(const ELogRecordMsg&) = delete;
    ~ELogRecordMsg() override {}
};

class ELOG_API ELogRecordBatchMsg : public commutil::Serializable {
public:
    ELogRecordBatchMsg() {}
    ELogRecordBatchMsg(const ELogRecordBatchMsg&) = delete;
    ELogRecordBatchMsg(ELogRecordBatchMsg&&) = delete;
    ELogRecordBatchMsg& operator=(const ELogRecordBatchMsg&) = delete;
    ~ELogRecordBatchMsg() override {}
};

class ELOG_API ELogStatusMsg : public commutil::Serializable {
public:
    ELogStatusMsg() : m_status(0), m_recordsProcessed(0) {}
    ELogStatusMsg(const ELogStatusMsg&) = delete;
    ELogStatusMsg(ELogStatusMsg&&) = delete;
    ELogStatusMsg& operator=(const ELogStatusMsg&) = delete;
    ~ELogStatusMsg() override {}

    /** @brief Serializes the message header. */
    commutil::ErrorCode serialize(commutil::OutputStream& os) const override;

    /** @brief Deserializes the message header. */
    commutil::ErrorCode deserialize(commutil::InputStream& is) override;

    inline int32_t getStatus() const { return m_status; }
    inline uint64_t getRecordsProcessed() const { return m_recordsProcessed; }

    inline void setStatus(int32_t status) { m_status = status; }
    inline void setRecordsProcessed(uint64_t recordsProcessed) {
        m_recordsProcessed = recordsProcessed;
    }

private:
    int32_t m_status;
    uint64_t m_recordsProcessed;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __ELOG_MSG_H__