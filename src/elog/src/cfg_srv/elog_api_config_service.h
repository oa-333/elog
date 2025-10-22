#ifndef __ELOG_API_CONFIG_SERVICE_H__
#define __ELOG_API_CONFIG_SERVICE_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <cstdint>

#include "elog_config.h"
#include "elog_record.h"

namespace elog {

// initializes the configuration service and start it running
extern bool initConfigService();

// stops the configuration service and terminates it
extern void termConfigService();

// loads configuration service from properties, restarts service if required
extern bool configConfigServiceProps(const ELogPropertySequence& props);

// loads configuration service from configuration node, restarts service if required
extern bool configConfigService(const ELogConfigMapNode* cfgMap);

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif