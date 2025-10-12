#ifndef __ELOG_API_CONFIG_SERVICE_H__
#define __ELOG_API_CONFIG_SERVICE_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <cstdint>

#include "elog_config.h"
#include "elog_record.h"

namespace elog {

extern bool initConfigService();
extern void termConfigService();
extern bool configConfigServiceProps(const ELogPropertySequence& props);
extern bool configConfigService(const ELogConfigMapNode* cfgMap);

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif