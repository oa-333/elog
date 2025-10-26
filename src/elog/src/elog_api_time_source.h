#ifndef __ELOG_API_TIME_SOURCE_H__
#define __ELOG_API_TIME_SOURCE_H__

#include "elog_config.h"

namespace elog {

// initializes the lazy time source and start it running
extern void initTimeSource();

// stops the lazy time source and terminates it
extern void termTimeSource();

// loads time source from properties, restarts time source if required
extern bool configTimeSourceProps(const ELogPropertySequence& props);

// loads time source from configuration node, restarts time source if required
extern bool configTimeSource(const ELogConfigMapNode* cfgMap);

}  // namespace elog

#endif  // __ELOG_API_TIME_SOURCE_H__