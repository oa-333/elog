#include "msg/elog_msg.h"

#ifdef ELOG_ENABLE_MSG

namespace elog {

commutil::ErrorCode ELogStatusMsg::serialize(commutil::OutputStream& os) const {
    COMM_SERIALIZE_INT32(os, m_status);
    COMM_SERIALIZE_UINT64(os, m_recordsProcessed);
    return commutil::ErrorCode::E_OK;
}

commutil::ErrorCode ELogStatusMsg::deserialize(commutil::InputStream& is) {
    COMM_DESERIALIZE_INT32(is, m_status);
    COMM_DESERIALIZE_UINT64(is, m_recordsProcessed);
    return commutil::ErrorCode::E_OK;
}

}  // namespace elog

#endif  // ELOG_ENABLE_MSG
