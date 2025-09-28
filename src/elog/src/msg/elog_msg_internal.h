#ifndef __ELOG_MSG_INTERNAL_H__
#define __ELOG_MSG_INTERNAL_H__

#ifdef ELOG_ENABLE_MSG

#include "msg/elog_msg.h"

/** @def The default binary format used in ELog net/ipc message connectors. */
#define ELOG_DEFAULT_MSG_BINARY_FORMAT "protobuf"

namespace elog {

extern bool initBinaryFormatProviders();

extern void termBinaryFormatProviders();

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __ELOG_MSG_INTERNAL_H__