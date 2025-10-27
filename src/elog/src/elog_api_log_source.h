#ifndef __ELOG_API_LOG_SOURCE_H__
#define __ELOG_API_LOG_SOURCE_H__

#include "elog_source.h"

namespace elog {

// initialize log source API
extern bool initLogSources();

// terminate log source API
extern void termLogSources();

}  // namespace elog

#endif  // __ELOG_API_LOG_SOURCE_H__