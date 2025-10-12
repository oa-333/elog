#ifndef __ELOG_API_RELOAD_CONFIG_H__
#define __ELOG_API_RELOAD_CONFIG_H__

#ifdef ELOG_ENABLE_RELOAD_CONFIG

#include <cstdint>

#include "elog_config.h"

namespace elog {

extern void startReloadConfigThread();
extern void stopReloadConfigThread();

}  // namespace elog

#endif  // ELOG_ENABLE_RELOAD_CONFIG

#endif