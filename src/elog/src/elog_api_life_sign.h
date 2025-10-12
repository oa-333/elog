#ifndef __ELOG_API_LIFE_SIGN_H__
#define __ELOG_API_LIFE_SIGN_H__

#ifdef ELOG_ENABLE_LIFE_SIGN

#include <cstdint>

#include "elog_config.h"
#include "elog_record.h"

namespace elog {

extern bool initLifeSignReport();
extern bool termLifeSignReport();
extern void sendLifeSignReport(const ELogRecord& logRecord);
extern bool configLifeSignProps(const ELogPropertySequence& props);
extern bool configLifeSign(const ELogConfigMapNode* cfgMap);
extern bool configLifeSignBasic(const ELogConfigMapNode* cfgMap);

}  // namespace elog

#endif  // ELOG_ENABLE_LIFE_SIGN

#endif